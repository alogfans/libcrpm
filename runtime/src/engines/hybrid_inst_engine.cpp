//
// Created by Feng Ren on 2021/1/23.
//

#include <sys/mman.h>
#include <algorithm>
#include <sys/time.h>
#include <future>
#include "internal/common.h"
#include "internal/engines/hybrid_inst_engine.h"

void address_buffer_clear_all();

#define USE_SYNCHRONOUS_CHECKPOINT

namespace crpm {
    extern bool process_instrumented;

    HybridInstEngine::Registry::Registry() : default_engine(nullptr) {}

    void HybridInstEngine::Registry::do_register(HybridInstEngine *engine) {
        std::lock_guard<std::mutex> guard(mutex);
        engines.insert(engine);
        if (!default_engine) {
            default_engine = engine;
        }
    }

    void HybridInstEngine::Registry::do_unregister(HybridInstEngine *engine) {
        std::lock_guard<std::mutex> guard(mutex);
        engines.erase(engine);
        if (engine == default_engine) {
            default_engine = nullptr;
        }
    }

    HybridInstEngine *HybridInstEngine::Registry::find(const void *addr) {
        uintptr_t u_addr = (uintptr_t) addr;
        if (likely(default_engine != nullptr)) {
            auto &range = default_engine->address_range;
            if (likely(u_addr >= range.first && u_addr < range.second)) {
                return default_engine;
            }
        }
        if (engines.size() == 1) {
            return nullptr;
        }
        for (auto engine : engines) {
            auto &range = engine->address_range;
            if (u_addr >= range.first && u_addr < range.second) {
                return engine;
            }
        }
        return nullptr;
    }

    void HybridInstEngine::Registry::hook_routine(const void *addr) {
        HybridInstEngine *engine = find(addr);
        if (likely(engine != nullptr)) {
            engine->hook_routine(addr);
        }
    }

    void HybridInstEngine::Registry::hook_routine(const void *addr, size_t len) {
        HybridInstEngine *engine = find(addr);
        if (likely(engine != nullptr)) {
            engine->hook_routine(addr, len);
        }
    }

    HybridInstEngine *HybridInstEngine::Registry::get_engine() const {
        if (engines.size() != 1) {
            return nullptr;
        } else {
            return default_engine;
        }
    }

    bool HybridInstEngine::create_checkpoint_image(const char *path, size_t user_capacity,
                                                   void *hint_addr, int flags,
                                                   const MemoryPoolOption &option) {
        capacity = user_capacity;
        nr_segments = capacity >> kSegmentShift;
        uint64_t nr_back_segments = nr_segments * option.shadow_capacity_factor;
        nr_blocks = capacity >> kBlockShift;
        uint64_t fs_size = CheckpointImage::CalculateFileSize(nr_segments, nr_back_segments);
        int ret = fs.create(path, fs_size, flags, hint_addr);
        if (!ret) {
            return false;
        }

        void *base_addr = fs.rel_to_abs(0);
        image = CheckpointImage::Open(base_addr, nr_segments, nr_back_segments, true);
        if (!image) {
            fs.close();
            return false;
        }
        return true;
    }

    bool HybridInstEngine::open_checkpoint_image(const char *path, void *hint_addr, int flags) {
        if (!fs.open(path, flags, hint_addr)) {
            return false;
        }
        void *base_addr = fs.rel_to_abs(0);
        image = CheckpointImage::Open(base_addr, 0, 0, false);
        if (!image) {
            fs.close();
            return false;
        }
        nr_segments = image->get_nr_main_segments();
        nr_blocks = nr_segments * kBlocksPerSegment;
        capacity = nr_segments * kSegmentSize;
        return true;
    }

    HybridInstEngine *HybridInstEngine::Open(const char *path,
                                             const MemoryPoolOption &option) {
        HybridInstEngine *impl = new HybridInstEngine();
        bool ret;
        int flags = 0;
        bool create = false;
        void *hint_addr = nullptr;

        if (option.create) {
            create = (option.truncate || !FileSystem::Exist(path));
        }

        if (create) {
            if (!option.capacity) {
                fprintf(stderr, "use default capacity for pool allocation\n");
            }
            uint64_t capacity = std::max(option.capacity, kMinContainerSize);
            if (capacity & kSegmentMask) {
                capacity = (capacity & ~kSegmentMask) + kSegmentSize;
            }
            if (!impl->create_checkpoint_image(path, capacity, hint_addr, flags, option)) {
                delete impl;
                return nullptr;
            }
        } else {
            if (!impl->open_checkpoint_image(path, hint_addr, flags)) {
                delete impl;
                return nullptr;
            }
        }

        impl->segment_dirty[0].allocate(impl->nr_segments);
        impl->segment_dirty[1].allocate(impl->nr_segments);
        impl->block_dirty[0].allocate(impl->nr_blocks);
        impl->block_dirty[1].allocate(impl->nr_blocks);
        if (option.fixed_base_address) {
            hint_addr = (void *) option.fixed_base_address;
        }

        impl->working_memory = mmap(hint_addr, impl->capacity, PROT_READ | PROT_WRITE,
                                    MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
        if (impl->working_memory == MAP_FAILED) {
            perror("mmap");
            exit(EXIT_FAILURE);
        }

        if (!create) {
            uint64_t a = ReadTSC();
            impl->image->recovery(CheckpointImage::SS_Main);
            impl->prepare_working_memory();
            impl->has_snapshot = impl->exist_snapshot();
            uint64_t b = ReadTSC();
            printf("%.3lf ms\n", (b - a) / 2400000.0);
        }

        impl->address_range.first = (uintptr_t) impl->get_address(0);
        impl->address_range.second = impl->address_range.first + impl->capacity;
        Registry::Get()->do_register(impl);
        impl->verbose = option.verbose_output;
        impl->has_init = true;
#ifndef USE_SYNCHRONOUS_CHECKPOINT
        impl->write_back_thread = std::thread(&WriteBackThreadRoutine, impl);
#endif //USE_SYNCHRONOUS_CHECKPOINT
        return impl;
    }

    HybridInstEngine::HybridInstEngine() :
            has_init(false),
            has_snapshot(false),
            next_thread_id(0),
            checkpoint_traffic(0),
            flush_latency(0),
            write_back_latency(0),
            write_back_thread_running(true),
            checkpoint_in_progress(false),
            write_back_state(WB_IDLE),
            next_back_segment(0),
            epoch(1),
            verbose(false) {
        for (uint64_t i = 0; i < kMaxThreads; ++i) {
            flush_blocks[i] = (volatile uint64_t *)
                    malloc(sizeof(uint64_t) * kMaxFlushBlocks);
            flush_blocks_count[i] = 0;
        }
        write_back_thread_lock.clear(std::memory_order_relaxed);
        back_memory_lock.clear(std::memory_order_relaxed);
    }

    HybridInstEngine::~HybridInstEngine() {
        if (has_init) {
#ifndef USE_SYNCHRONOUS_CHECKPOINT
            write_back_thread_running = false;
            write_back_thread.join();
#endif //USE_SYNCHRONOUS_CHECKPOINT
            for (uint64_t i = 0; i < kMaxThreads; ++i) {
                free((void *) flush_blocks[i]);
            }
            delete image;
            munmap(working_memory, capacity);
            fs.close();
            Registry::Get()->do_unregister(this);
            if (verbose) {
                printf("checkpoint_traffic: %.3lf MiB\n",
                       checkpoint_traffic / 1000000.0);
                printf("flush_latency: %.3lf ms\n",
                       flush_latency / 2400000.0);
                printf("write_back_latency: %.3lf ms\n",
                       write_back_latency / 2400000.0);
            }
        }
    }

    void HybridInstEngine::prepare_working_memory() {
#ifdef USE_MULTI_THREADS_RECOVERY
        const static size_t kThreads = 8;
        std::thread threads[kThreads];
        for (int tid = 0; tid < kThreads; tid++) {
            threads[tid] = std::thread([=]{
                uint8_t *work_base = (uint8_t *) working_memory + tid * kSegmentSize;
                uint8_t *main_base = image->get_main_segment(tid);
                for (uint64_t seg_id = tid; seg_id < nr_segments; seg_id += kThreads) {
                    uint8_t state = image->get_segment_state(seg_id);
                    if (state != CheckpointImage::SS_Initial) {
                        NonTemporalCopyWithWriteElimination(work_base, main_base, kSegmentSize);
                    }
                    work_base += kThreads * kSegmentSize;
                    main_base += kThreads * kSegmentSize;
                }
            });
        }
        for (int i = 0; i < kThreads; i++) {
            threads[i].join();
        }
#else
        uint64_t traffic = 0;
        uint8_t *work_base = (uint8_t *) working_memory;
        uint8_t *main_base = image->get_main_segment(0);
        for (uint64_t seg_id = 0; seg_id < nr_segments; ++seg_id) {
            uint8_t state = image->get_segment_state(seg_id);
            if (state != CheckpointImage::SS_Initial) {
                NonTemporalCopyWithWriteElimination(work_base, main_base, kSegmentSize);
                traffic += kSegmentSize;
            }
            work_base += kSegmentSize;
            main_base += kSegmentSize;
        }
        // printf("fill_dram_traffic: %.3lf MiB\n", traffic / 1000000.0);
#endif
    }

    void HybridInstEngine::determine_flush_mode() {
        prev_flush_mode = flush_mode;
        uint64_t total_blocks = 0;
        bool all_empty = true, has_full = false;
        for (size_t i = 0; i < kMaxThreads; ++i) {
            size_t size = flush_blocks_count[i];
            if (size != 0) {
                all_empty = false;
            }
            total_blocks += size;
            if (total_blocks >= kMaxFlushBlocks) {
                has_full = true;
            }
        }
        if (all_empty) {
            flush_mode = FMODE_NO_ACTION;
            return;
        } else if (has_full) {
            flush_mode = FMODE_WBINVD;
        } else {
            flush_mode = FMODE_USE_FLUSH_BLOCKS;
        }
    }

    void HybridInstEngine::checkpoint(uint64_t nr_threads) {
        uint64_t start_clock, persist_clock;
        int tid = next_thread_id.fetch_add(1, std::memory_order_relaxed);
        bool is_leader = (tid == 0);

        barrier.barrier(nr_threads, tid);
        if (is_leader) {
            start_clock = ReadTSC();
            address_buffer_clear_all();
        }

        std::atomic_thread_fence(std::memory_order_release);
        barrier.barrier(nr_threads, tid);
        if (is_leader) {
            std::atomic_thread_fence(std::memory_order_acquire);
            determine_flush_mode();
            if (flush_mode == FMODE_NO_ACTION) {
                next_thread_id.store(0, std::memory_order_relaxed);
            } else {
                checkpoint_in_progress.store(true, std::memory_order_relaxed);
                AcquireLock(write_back_thread_lock);
            }
            latch.latch_add(tid);
        }
        latch.latch_wait(tid);

        if (flush_mode == FMODE_NO_ACTION) {
            return;
        }

#ifdef USE_SYNCHRONOUS_CHECKPOINT
        if (prev_flush_mode == FMODE_WBINVD) {
            apply_nvm_to_nvm_parallel(tid, nr_threads);
        }
#endif //USE_SYNCHRONOUS_CHECKPOINT
        if (epoch) {
            apply_dram_to_back_parallel(tid, nr_threads);
        } else {
            apply_dram_to_main_parallel(tid, nr_threads);
        }
        barrier.barrier(nr_threads, tid);

        if (flush_mode == FMODE_WBINVD) {
            if (is_leader) {
                uint8_t state = epoch ? CheckpointImage::SS_Back : CheckpointImage::SS_Main;
                commit_layout_state(state);
                clear_dirty_bits_last_epoch();
                persist_clock = ReadTSC();
                next_thread_id.store(0, std::memory_order_relaxed);
                flush_latency.fetch_add(persist_clock - start_clock,
                                        std::memory_order_relaxed);
                if (unlikely(!has_snapshot)) {
                    image->set_attributes(kAttributeHasSnapshot);
                    has_snapshot = true;
                }
                write_back_state.store(WB_STARTING, std::memory_order_relaxed);
                for (uint64_t i = 0; i < kMaxThreads; ++i) {
                    flush_blocks_count[i] = 0;
                }
                checkpoint_in_progress.store(false, std::memory_order_relaxed);
                epoch = 1 - epoch;
                ReleaseLock(write_back_thread_lock);
                latch.latch_add(tid);
            }
            std::atomic_thread_fence(std::memory_order_release);
            latch.latch_wait(tid);
        } else {
            if (is_leader) {
                uint8_t state = epoch ? CheckpointImage::SS_Back : CheckpointImage::SS_Main;
                commit_layout_state(state);
                latch.latch_add(tid);
            }
            latch.latch_wait(tid);
            if (epoch) {
                apply_dram_to_main_parallel(tid, nr_threads);
            } else {
                apply_dram_to_back_parallel(tid, nr_threads);
            }
            barrier.barrier(nr_threads, tid);
            if (is_leader) {
                clear_dirty_bits_last_epoch();
                persist_clock = ReadTSC();
                next_thread_id.store(0, std::memory_order_relaxed);
                flush_latency.fetch_add(persist_clock - start_clock,
                                        std::memory_order_relaxed);
                if (unlikely(!has_snapshot)) {
                    image->set_attributes(kAttributeHasSnapshot);
                    has_snapshot = true;
                }
                for (uint64_t i = 0; i < kMaxThreads; ++i) {
                    flush_blocks_count[i] = 0;
                }
                checkpoint_in_progress.store(false, std::memory_order_relaxed);
                epoch = 1 - epoch;
                ReleaseLock(write_back_thread_lock);
                latch.latch_add(tid);
            }
            std::atomic_thread_fence(std::memory_order_release);
            latch.latch_wait(tid);
        }
    }

    void HybridInstEngine::commit_layout_state(uint8_t state) {
#if 0
        if (flush_mode == FMODE_WBINVD) {
            uint64_t e = epoch;
            image->begin_segment_state_update();
            for (uint64_t seg_id = 0; seg_id < nr_segments; seg_id += AtomicBitSet::kBitWidth) {
                uint64_t bitset = segment_dirty[e].test_all(seg_id);
                while (bitset != 0) {
                    uint64_t t = bitset & -bitset;
                    int i = __builtin_ctzll(bitset); // i == first set index
                    bitset ^= t;
                    image->set_segment_state(seg_id + i, state);
                }
            }
            image->commit_segment_state_update();
        } else {
            image->begin_segment_state_update();
            for (size_t id = 0; id < kMaxThreads; ++id) {
                auto &bucket = flush_blocks[id];
                uint64_t bucket_size = flush_blocks_count[id];
                for (uint64_t i = 0; i != bucket_size; ++i) {
                    uint64_t block_id = bucket[i];
                    uint64_t segment_id = block_id >> (kSegmentShift - kBlockShift);
                    image->set_segment_state(segment_id, state);
                }
            }
            image->commit_segment_state_update();
        }
#endif
        uint64_t e = epoch;
        image->begin_segment_state_update();
        for (uint64_t seg_id = 0; seg_id < nr_segments; seg_id += AtomicBitSet::kBitWidth) {
            uint64_t bitset = segment_dirty[e].test_all(seg_id);
            uint64_t bitset_last = segment_dirty[1 - e].test_all(seg_id);
            bitset |= bitset_last;
            while (bitset != 0) {
                uint64_t t = bitset & -bitset;
                int i = __builtin_ctzll(bitset); // i == first set index
                bitset ^= t;
                image->set_segment_state(seg_id + i, state);
            }
        }
        image->commit_segment_state_update();
    }

    bool HybridInstEngine::exist_snapshot() {
        return image->get_attributes() & kAttributeHasSnapshot;
    }

    void *HybridInstEngine::get_address(uint64_t offset) {
        return (uint8_t *) working_memory + offset;
    }

    size_t HybridInstEngine::get_capacity() {
        return capacity;
    }

    void HybridInstEngine::wait_for_background_task() {
#ifndef USE_SYNCHRONOUS_CHECKPOINT
        while (write_back_state.load(std::memory_order_relaxed) != WB_IDLE) {
            _mm_pause();
        }
#endif //USE_SYNCHRONOUS_CHECKPOINT
        checkpoint_traffic = 0;
        flush_latency = 0;
        write_back_latency = 0;
    }

    void HybridInstEngine::apply_nvm_to_nvm_parallel(int tid, int nr_threads) {
        uint64_t traffic = 0;
        uint64_t main_id = tid;
        uint64_t start_clock = ReadTSC();
        uint64_t e = 1 - epoch; // previous epoch;
        while (main_id < nr_segments) {
            if (!segment_dirty[e].test(main_id)) {
                main_id += nr_threads;
                continue;
            }

            const uint64_t start_block_id = main_id * kBlocksPerSegment;
            const uint64_t stop_block_id = std::min(nr_blocks, start_block_id + kBlocksPerSegment);
            assert(start_block_id % AtomicBitSet::kBitWidth == 0);
            uint64_t back_id = image->get_main_to_back(main_id);
            if (back_id == kNullSegmentIndex) {
                main_id += nr_threads;
                continue;
            }

            uint8_t *work_base = (uint8_t *) working_memory + (main_id << kSegmentShift);
            uint8_t *back_base = image->get_back_segment(back_id);
            uint8_t *main_base = image->get_main_segment(main_id);
            for (uint64_t block_id = start_block_id;
                 block_id < stop_block_id;
                 block_id += AtomicBitSet::kBitWidth) {
                uint64_t bitset = block_dirty[e].test_all(block_id);
                uint64_t bitset_cur = block_dirty[1 - e].test_all(block_id);
                bitset &= ~bitset_cur;
                while (bitset != 0) {
                    uint64_t t = bitset & -bitset;
                    int i = __builtin_ctzll(bitset); // i == first set index
                    bitset ^= t;
                    // assert(block_dirty.test(block_id + i));
                    uint8_t *work_addr = work_base + (i << kBlockShift);
                    uint8_t *back_addr = back_base + (i << kBlockShift);
                    uint8_t *main_addr = main_base + (i << kBlockShift);
#ifdef USE_SYNCHRONOUS_CHECKPOINT
                    if (epoch) {
                        NonTemporalCopyWithWriteElimination(back_addr, work_addr, kBlockSize);
                    } else {
                        NonTemporalCopyWithWriteElimination(main_addr, work_addr, kBlockSize);
                    }
#else
                    if (epoch) {
                        NonTemporalCopyWithWriteElimination(back_addr, main_addr, kBlockSize);
                    } else {
                        NonTemporalCopyWithWriteElimination(main_addr, back_addr, kBlockSize);
                    }
#endif
                    traffic += kBlockSize;
                }
                work_base += AtomicBitSet::kBitWidth * kBlockSize;
                back_base += AtomicBitSet::kBitWidth * kBlockSize;
                main_base += AtomicBitSet::kBitWidth * kBlockSize;
            }
            main_id += nr_threads;
        }
        checkpoint_traffic.fetch_add(traffic);
#ifndef USE_SYNCHRONOUS_CHECKPOINT
        uint64_t write_back_clock = ReadTSC();
        write_back_latency.fetch_add(write_back_clock - start_clock);
#endif //USE_SYNCHRONOUS_CHECKPOINT
    }

    uint64_t HybridInstEngine::apply_dram_to_back_parallel(int tid, int nr_threads) {
        uint64_t traffic = 0;
        if (flush_mode == FMODE_WBINVD) {
            uint64_t main_id = tid;
            while (main_id < nr_segments) {
                if (!segment_dirty[epoch].test(main_id)) {
                    main_id += nr_threads;
                    continue;
                }

                bool created;
                uint64_t back_id = find_back_segment(main_id, created);
                const uint64_t start_block_id = main_id * kBlocksPerSegment;
                const uint64_t stop_block_id =
                        std::min(nr_blocks, start_block_id + kBlocksPerSegment);
                assert(start_block_id % AtomicBitSet::kBitWidth == 0);

                uint8_t *work_base = (uint8_t *) working_memory + (main_id << kSegmentShift);
                uint8_t *back_base = image->get_back_segment(back_id);
                if (created && image->get_segment_state(main_id) != CheckpointImage::SS_Initial) {
                    NonTemporalCopy256(back_base, work_base, kSegmentSize);
                    traffic += kSegmentSize;
                } else {
                    for (uint64_t block_id = start_block_id;
                         block_id < stop_block_id;
                         block_id += AtomicBitSet::kBitWidth) {
                        uint64_t bitset = block_dirty[epoch].test_all(block_id);
                        while (bitset != 0) {
                            uint64_t t = bitset & -bitset;
                            int i = __builtin_ctzll(bitset); // i == first set index
                            bitset ^= t;
                            // assert(block_dirty.test(block_id + i));
                            uint8_t *work_addr = work_base + (i << kBlockShift);
                            uint8_t *back_addr = back_base + (i << kBlockShift);
                            NonTemporalCopy256(back_addr, work_addr, kBlockSize);
                            traffic += kBlockSize;
                        }
                        work_base += AtomicBitSet::kBitWidth * kBlockSize;
                        back_base += AtomicBitSet::kBitWidth * kBlockSize;
                    }
                }
                main_id += nr_threads;
            }
        } else {
            if (tid == 0) {
                size_t id = 0;
                while (id < kMaxThreads) {
                    auto &bucket = flush_blocks[id];
                    uint64_t bucket_size = flush_blocks_count[id];
                    for (uint64_t i = 0; i != bucket_size; ++i) {
                        uint64_t block_id = bucket[i];
                        bool created;
                        uint64_t back_block_id = find_back_block(block_id, created);
                        uint64_t main_id = (block_id / kBlocksPerSegment);
                        if (created && image->get_segment_state(main_id) != CheckpointImage::SS_Initial) {
                            uint8_t *work_addr = (uint8_t *) working_memory +
                                    (main_id << kSegmentShift);
                            uint8_t *back_addr = image->get_back_segment(
                                    back_block_id / kBlocksPerSegment);
                            NonTemporalCopy256(back_addr, work_addr, kSegmentSize);
                            traffic += kSegmentSize;
                        } else {
                            uint8_t *work_addr =
                                    (uint8_t *) working_memory + (block_id << kBlockShift);
                            uint8_t *back_addr = image->get_back_block(back_block_id);
                            NonTemporalCopy256(back_addr, work_addr, kBlockSize);
                            traffic += kBlockSize;
                        }
                    }
                    id++;
                }
            }
        }
        checkpoint_traffic.fetch_add(traffic);
        StoreFence();
        return 0;
    }

    uint64_t HybridInstEngine::apply_dram_to_main_parallel(int tid, int nr_threads) {
        uint64_t traffic = 0;
        uint64_t segments = 0;
        if (flush_mode == FMODE_WBINVD) {
            uint64_t main_id = tid;
            while (main_id < nr_segments) {
                if (!segment_dirty[epoch].test(main_id)) {
                    main_id += nr_threads;
                    continue;
                }
                segments++;
                if (image->get_segment_state(main_id) == CheckpointImage::SS_Main) {
                    bool created = false;
                    uint64_t back_id = find_back_segment(main_id, created);
                    if (created) {
                        uint8_t *main_base = image->get_main_segment(main_id);
                        uint8_t *back_base = image->get_back_segment(back_id);
                        NonTemporalCopy256(main_base, back_base, kSegmentSize);
                        traffic += kSegmentSize;
                        image->set_segment_state_atomic(main_id, CheckpointImage::SS_Back);
                    }
                }

                const uint64_t start_block_id = main_id * kBlocksPerSegment;
                const uint64_t stop_block_id = std::min(nr_blocks,
                                                        start_block_id + kBlocksPerSegment);
                assert(start_block_id % AtomicBitSet::kBitWidth == 0);

                uint8_t *work_base = (uint8_t *) working_memory + (main_id << kSegmentShift);
                uint8_t *main_base = image->get_main_segment(main_id);
                for (uint64_t block_id = start_block_id;
                     block_id < stop_block_id;
                     block_id += AtomicBitSet::kBitWidth) {
                    uint64_t bitset = block_dirty[epoch].test_all(block_id);
                    while (bitset != 0) {
                        uint64_t t = bitset & -bitset;
                        int i = __builtin_ctzll(bitset); // i == first set index
                        bitset ^= t;
                        // assert(block_dirty.test(block_id + i));
                        uint8_t *work_addr = work_base + (i << kBlockShift);
                        uint8_t *main_addr = main_base + (i << kBlockShift);
                        NonTemporalCopy256(main_addr, work_addr, kBlockSize);
                        traffic += kBlockSize;
                    }
                    work_base += AtomicBitSet::kBitWidth * kBlockSize;
                    main_base += AtomicBitSet::kBitWidth * kBlockSize;
                }
                main_id += nr_threads;
            }
        } else {
            size_t id = tid;
            while (id < kMaxThreads) {
                auto &bucket = flush_blocks[id];
                uint64_t bucket_size = flush_blocks_count[id];
                for (uint64_t i = 0; i != bucket_size; ++i) {
                    uint64_t block_id = bucket[i];
                    uint64_t main_id = (block_id / kBlocksPerSegment);
                    if (image->get_segment_state(main_id) == CheckpointImage::SS_Main) {
                        bool created = false;
                        uint64_t back_id = find_back_segment(main_id, created);
                        if (created) {
                            uint8_t *main_base = image->get_main_segment(main_id);
                            uint8_t *back_base = image->get_back_segment(back_id);
                            NonTemporalCopy256(main_base, back_base, kSegmentSize);
                            traffic += kSegmentSize;
                            image->set_segment_state_atomic(main_id, CheckpointImage::SS_Back);
                        }
                    }
                    uint8_t *work_addr = (uint8_t *) working_memory + (block_id << kBlockShift);
                    uint8_t *main_addr = (uint8_t *) image->get_main_block(block_id);
                    NonTemporalCopy256(main_addr, work_addr, kBlockSize);
                    traffic += kBlockSize;
                }
                id += nr_threads;
            }
        }
        StoreFence();
        checkpoint_traffic.fetch_add(traffic);
        return 0;
    }

    uint64_t HybridInstEngine::find_back_segment(uint64_t segment_id, bool &created) {
        uint64_t back_seg_id = image->get_main_to_back(segment_id);
        created = (back_seg_id == kNullSegmentIndex);
        if (created) {
            allocate_back_segment(segment_id);
            back_seg_id = image->get_main_to_back(segment_id);
            if (back_seg_id == kNullSegmentIndex) {
                fprintf(stderr, "no free back segments\n");
                exit(EXIT_FAILURE);
            }
        }
        return back_seg_id;
    }

    uint64_t HybridInstEngine::find_back_block(uint64_t block_id, bool &created) {
        uint64_t back_seg_id = find_back_segment(block_id / kBlocksPerSegment, created);
        return (back_seg_id * kBlocksPerSegment) + (block_id % kBlocksPerSegment);
    }

    void HybridInstEngine::clear_dirty_bits_last_epoch() {
        uint64_t segment_id = 0;
        uint64_t e = 1 - epoch;
        while (segment_id < nr_segments) {
            if (!segment_dirty[e].test(segment_id)) {
                segment_id++;
                continue;
            }
            uint64_t start_block_id = segment_id * kBlocksPerSegment;
            uint64_t stop_block_id = std::min(nr_blocks, start_block_id + kBlocksPerSegment);
            block_dirty[e].clear_region(start_block_id, stop_block_id);
            segment_dirty[e].clear(segment_id);
            segment_id++;
        }
    }

    void HybridInstEngine::allocate_back_segment(uint64_t segment_id) {
        const size_t kNumBackSegments = image->get_nr_back_segments();
        AcquireLock(back_memory_lock);
        uint64_t loop_count = 0;
        while (loop_count < kNumBackSegments) {
            uint64_t main_segment = image->get_back_to_main(next_back_segment);
            if (main_segment == kNullSegmentIndex) {
                image->bind_back_segment(segment_id, next_back_segment);
                advance_next_back_segment();
                ReleaseLock(back_memory_lock);
                return;
            }
            if (segment_dirty[epoch].test(main_segment)) {
                advance_next_back_segment();
                loop_count++;
                continue;
            }

            image->bind_back_segment(segment_id, next_back_segment);
            advance_next_back_segment();
            ReleaseLock(back_memory_lock);
            return;
        }
    }

    void HybridInstEngine::hook_routine(const void *addr, size_t len) {
        uint64_t offset = (uint64_t) addr - address_range.first;
        uint64_t last_segment_id = UINT64_MAX;
        for (uintptr_t delta = offset & ~kBlockMask; delta < offset + len; delta += kBlockSize) {
            uint64_t block_id = delta >> kBlockShift;
            uint64_t segment_id = delta >> kSegmentShift;
            if (block_dirty[epoch].test(block_id, std::memory_order_acquire)) {
                continue;
            }

            block_dirty[epoch].set(block_id, std::memory_order_release);

            thread_local unsigned int tid = tl_thread_info.get_thread_id();
            auto &bucket_size = flush_blocks_count[tid];
            if (likely(bucket_size != kMaxFlushBlocks)) {
                flush_blocks[tid][bucket_size] = block_id;
                ++bucket_size;
            }

            if (segment_id != last_segment_id) {
                if (!segment_dirty[epoch].test(segment_id, std::memory_order_acquire)) {
                    segment_dirty[epoch].set(segment_id, std::memory_order_release);
                }
                last_segment_id = segment_id;
            }
        }
    }

    void HybridInstEngine::hook_routine(const void *addr) {
        uint64_t delta = (uint64_t) addr - address_range.first;
        uint64_t block_id = delta >> kBlockShift;
        uint64_t segment_id = delta >> kSegmentShift;
        if (block_dirty[epoch].test(block_id, std::memory_order_acquire)) {
            return;
        }

        block_dirty[epoch].set(block_id, std::memory_order_release);

        thread_local unsigned int tid = tl_thread_info.get_thread_id();
        auto &bucket_size = flush_blocks_count[tid];
        if (likely(bucket_size != kMaxFlushBlocks)) {
            flush_blocks[tid][bucket_size] = block_id;
            ++bucket_size;
        }

        if (segment_dirty[epoch].test(segment_id, std::memory_order_acquire)) {
            return;
        }

        segment_dirty[epoch].set(segment_id, std::memory_order_release);
    }

    void HybridInstEngine::WriteBackThreadRoutine(HybridInstEngine *engine) {
        BindSingleSocket();
        assert(engine);
        WriteBackState state;
        struct timespec delay;
        delay.tv_sec = 0;
        delay.tv_nsec = 10000; // 10us

        while (engine->write_back_thread_running) {
            state = engine->write_back_state.load(std::memory_order_relaxed);
            switch (state) {
                case WB_STARTING:
                    AcquireLock(engine->write_back_thread_lock);
                    engine->write_back_state.store(WB_RUNNING, std::memory_order_relaxed);
                    break;
                case WB_RUNNING:
#ifndef USE_SYNCHRONOUS_CHECKPOINT
                    engine->apply_nvm_to_nvm_parallel(0, 1);
#endif
                    engine->write_back_state.store(WB_EXITING, std::memory_order_relaxed);
                    break;
                case WB_EXITING:
                    ReleaseLock(engine->write_back_thread_lock);
                    engine->write_back_state.store(WB_IDLE, std::memory_order_release);
                    break;
                case WB_IDLE:
                    nanosleep(&delay, nullptr);
                    // sched_yield();
                    break;
                default:
                    assert(0);
            }
        }
        if (state == WB_STARTING || state == WB_RUNNING) {
            ReleaseLock(engine->write_back_thread_lock);
        }
    }

#ifdef USE_MPI_EXTENSION
    HybridInstEngine * HybridInstEngine::OpenForMPI(const char *path,
                                              const MemoryPoolOption &option,
                                              MPI_Comm comm) {
        HybridInstEngine *impl = new HybridInstEngine();
        bool ret;
        int flags = 0;
        int create = 0, all_create, comm_size;
        void *hint_addr = nullptr;

        if (option.create) {
            create = (option.truncate || !FileSystem::Exist(path)) ? 1 : 0;
        }

        MPI_Comm_size(comm, &comm_size);
        MPI_Allreduce(&create, &all_create, 1, MPI_INT, MPI_SUM, comm);
        if (all_create != 0 && all_create != comm_size) {
            fprintf(stderr, "Not all processes have the same checkpoint\n");
            MPI_Abort(comm, 1);
        }

        if (create) {
            if (!option.capacity) {
                fprintf(stderr, "use default capacity for pool allocation\n");
            }
            uint64_t capacity = std::max(option.capacity, kMinContainerSize);
            if (capacity & kSegmentMask) {
                capacity = (capacity & ~kSegmentMask) + kSegmentSize;
            }
            if (!impl->create_checkpoint_image(path, capacity, hint_addr, flags, option)) {
                delete impl;
                return nullptr;
            }
        } else {
            if (!impl->open_checkpoint_image(path, hint_addr, flags)) {
                delete impl;
                return nullptr;
            }
        }

        impl->segment_dirty[0].allocate(impl->nr_segments);
        impl->segment_dirty[1].allocate(impl->nr_segments);
        impl->block_dirty[0].allocate(impl->nr_blocks);
        impl->block_dirty[1].allocate(impl->nr_blocks);
        if (option.fixed_base_address) {
            hint_addr = (void *) option.fixed_base_address;
        }

        impl->working_memory = mmap(hint_addr, impl->capacity, PROT_READ | PROT_WRITE,
                                    MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
        if (impl->working_memory == MAP_FAILED) {
            perror("mmap");
            exit(EXIT_FAILURE);
        }

        if (!create) {
            struct timeval tv_begin, tv_mid, tv_end;
            gettimeofday(&tv_begin, nullptr);
            uint64_t my_epoch = impl->image->get_committed_epoch();
            uint64_t min_epoch;
            MPI_Allreduce(&my_epoch, &min_epoch, 1, MPI_UNSIGNED_LONG, MPI_MIN, comm);
            if (my_epoch - min_epoch >= 2) {
                fprintf(stderr, "unrecoverable\n");
                exit(EXIT_FAILURE);
            }
            if (min_epoch != my_epoch) {
                impl->image->reset_committed_epoch(min_epoch);
            }
            impl->image->recovery(CheckpointImage::SS_Main);
            gettimeofday(&tv_mid, nullptr);
            impl->prepare_working_memory();
            impl->has_snapshot = impl->exist_snapshot();
            gettimeofday(&tv_end, nullptr);
            double t1_elapsed = (tv_mid.tv_sec - tv_begin.tv_sec) * 1000.0 +
                   (tv_mid.tv_usec - tv_begin.tv_usec) / 1000.0;
            double t2_elapsed = (tv_end.tv_sec - tv_mid.tv_sec) * 1000.0 +
                   (tv_end.tv_usec - tv_mid.tv_usec) / 1000.0;
            printf("Recovery time = %.3lf (%.3lf + %.3lf) \n",
                   t1_elapsed + t2_elapsed, t1_elapsed, t2_elapsed);
        }

        impl->address_range.first = (uintptr_t) impl->get_address(0);
        impl->address_range.second = impl->address_range.first + impl->capacity;
        Registry::Get()->do_register(impl);
        impl->verbose = option.verbose_output;
        impl->has_init = true;
#ifndef USE_SYNCHRONOUS_CHECKPOINT
        impl->write_back_thread = std::thread(&WriteBackThreadRoutine, impl);
#endif //USE_SYNCHRONOUS_CHECKPOINT
        return impl;
    }

    void HybridInstEngine::checkpoint_for_mpi(uint64_t nr_threads, MPI_Comm comm) {
        uint64_t start_clock, persist_clock;
        int tid = next_thread_id.fetch_add(1, std::memory_order_relaxed);
        bool is_leader = (tid == 0);

        barrier.barrier(nr_threads, tid);
        if (is_leader) {
            start_clock = ReadTSC();
            address_buffer_clear_all();
        }

        std::atomic_thread_fence(std::memory_order_release);
        barrier.barrier(nr_threads, tid);
        if (is_leader) {
            std::atomic_thread_fence(std::memory_order_acquire);
            determine_flush_mode();
            if (flush_mode == FMODE_NO_ACTION) {
                next_thread_id.store(0, std::memory_order_relaxed);
            } else {
                checkpoint_in_progress.store(true, std::memory_order_relaxed);
                AcquireLock(write_back_thread_lock);
            }
            latch.latch_add(tid);
        }
        latch.latch_wait(tid);

        if (flush_mode == FMODE_NO_ACTION) {
            return;
        }
#ifdef USE_SYNCHRONOUS_CHECKPOINT
        apply_nvm_to_nvm_parallel(tid, nr_threads);
#endif //USE_SYNCHRONOUS_CHECKPOINT
        if (epoch) {
            apply_dram_to_back_parallel(tid, nr_threads);
        } else {
            apply_dram_to_main_parallel(tid, nr_threads);
        }
        barrier.barrier(nr_threads, tid);

        if (is_leader) {
            uint8_t state = epoch ? CheckpointImage::SS_Back : CheckpointImage::SS_Main;
            commit_layout_state_for_mpi(state, comm);
            clear_dirty_bits_last_epoch();
            persist_clock = ReadTSC();
            next_thread_id.store(0, std::memory_order_relaxed);
            flush_latency.fetch_add(persist_clock - start_clock,
                                    std::memory_order_relaxed);
            if (unlikely(!has_snapshot)) {
                image->set_attributes(kAttributeHasSnapshot);
                has_snapshot = true;
            }
            write_back_state.store(WB_STARTING, std::memory_order_relaxed);
            for (uint64_t i = 0; i < kMaxThreads; ++i) {
                flush_blocks_count[i] = 0;
            }
            checkpoint_in_progress.store(false, std::memory_order_relaxed);
            epoch = 1 - epoch;
            ReleaseLock(write_back_thread_lock);
            latch.latch_add(tid);
        }
        std::atomic_thread_fence(std::memory_order_release);
        latch.latch_wait(tid);

        // printf("checkpoint_traffic: %.3lf MiB\n", checkpoint_traffic / 1000000.0);
    }

    void HybridInstEngine::commit_layout_state_for_mpi(uint8_t state, MPI_Comm comm) {
        uint64_t e = epoch;
        image->begin_segment_state_update();
        for (uint64_t seg_id = 0; seg_id < nr_segments; seg_id += AtomicBitSet::kBitWidth) {
            uint64_t bitset = segment_dirty[e].test_all(seg_id);
            uint64_t bitset_last = segment_dirty[1 - e].test_all(seg_id);
            bitset |= bitset_last;
            while (bitset != 0) {
                uint64_t t = bitset & -bitset;
                int i = __builtin_ctzll(bitset); // i == first set index
                bitset ^= t;
                image->set_segment_state(seg_id + i, state);
            }
        }
        image->commit_segment_state_update_for_mpi(comm);

#if 0
        if (flush_mode == FMODE_WBINVD) {
            image->begin_segment_state_update();
            for (uint64_t seg_id = 0; seg_id < nr_segments; seg_id += AtomicBitSet::kBitWidth) {
                uint64_t bitset = segment_dirty[epoch].test_all(seg_id);
                while (bitset != 0) {
                    uint64_t t = bitset & -bitset;
                    int i = __builtin_ctzll(bitset); // i == first set index
                    bitset ^= t;
                    image->set_segment_state(seg_id + i, state);
                }
            }
            image->commit_segment_state_update_for_mpi(comm);
        } else {
            image->begin_segment_state_update();
            for (size_t id = 0; id < kMaxThreads; ++id) {
                auto &bucket = flush_blocks[id];
                uint64_t bucket_size = flush_blocks_count[id];
                for (uint64_t i = 0; i != bucket_size; ++i) {
                    uint64_t block_id = bucket[i];
                    uint64_t segment_id = block_id >> (kSegmentShift - kBlockShift);
                    image->set_segment_state(segment_id, state);
                }
            }
            image->commit_segment_state_update_for_mpi(comm);
        }
#endif
    }

#endif // USE_MPI_EXTENSION
}

#ifdef USE_HYBRID_INST_ENGINE
extern "C" {
__attribute__((noinline))
void __crpm_hook_rt_init();

void __crpm_hook_rt_fini();

void __crpm_hook_rt_store(void *addr);

void __crpm_hook_rt_range_store(void *addr, size_t length);
}

#ifdef LEGACY_HOOK_FUNCTION
void address_buffer_clear_all() { }
alignas(64) uint64_t stack_start_addr, stack_end_addr;
#else
const static size_t kNumBufferedAddresses = 504;
struct AddressBuffer {
    volatile uint64_t length;
    std::atomic_flag spinlock;
    uint64_t padding[6];
    volatile uint64_t ptr[kNumBufferedAddresses];
};

static_assert(sizeof(AddressBuffer) == 4096, "wrong address buffer size");

alignas(4096) AddressBuffer address_buffer[crpm::kMaxThreads];
alignas(64) uint64_t stack_start_addr, stack_end_addr;

#define INLINE_HOOK

__attribute__((noinline))
void address_buffer_clear(AddressBuffer *buffer) {
    std::atomic_thread_fence(std::memory_order_acquire);
    while (buffer->spinlock.test_and_set(std::memory_order_relaxed)) {}
    auto registry = crpm::HybridInstEngine::Registry::Get();
    uint64_t length = buffer->length;
#ifdef INLINE_HOOK
    auto engine = registry->get_engine();
    if (engine) {
        uintptr_t start_addr = (uintptr_t) engine->get_address(0);
        uintptr_t end_addr = start_addr + engine->get_capacity();
        for (uint64_t i = 0; i != length; ++i) {
            uintptr_t addr = buffer->ptr[i];
            if (unlikely(addr < start_addr || addr >= end_addr)) {
                continue;
            }
            engine->hook_routine((const void *) addr);
        }
    } else {
        for (uint64_t i = 0; i != length; ++i) {
            uintptr_t addr = buffer->ptr[i];
            registry->hook_routine((const void *) addr);
        }
    }
#else
    for (uint64_t i = 0; i != length; ++i) {
        uintptr_t addr = buffer->ptr[i];
        registry->hook_copy_on_write_routine((const void *) addr);
        registry->hook_routine((const void *) addr);
    }
#endif
    assert(buffer->length == length);
    buffer->length = 0;
    buffer->spinlock.clear(std::memory_order_release);
}

void address_buffer_clear_all() {
    for (uint64_t i = 0; i < crpm::kMaxThreads; ++i) {
        address_buffer_clear(&address_buffer[i]);
    }
}

#endif

// uint64_t __crpm_cnt_store = 0, __crpm_cnt_range_store = 0, __crpm_cnt_blocks;

void __crpm_hook_rt_init() {
    // store_counter = 0;
    crpm::GetStackAddressSpace(stack_start_addr, stack_end_addr);
#ifndef LEGACY_HOOK_FUNCTION
    for (uint64_t i = 0; i < crpm::kMaxThreads; ++i) {
        address_buffer[i].length = 0;
        address_buffer[i].spinlock.clear(std::memory_order_relaxed);
    }
#endif
    crpm::process_instrumented = true;
}

void __crpm_hook_rt_fini() {
    // printf("[fini] store_counter = %ld\n", store_counter);
    /*
    printf("[fini] insts = %ld (%ld+%ld) blocks=%ld\n", 
            __crpm_cnt_store + __crpm_cnt_range_store, 
            __crpm_cnt_store, 
            __crpm_cnt_range_store, 
            __crpm_cnt_blocks);
    */
    
}

void __crpm_hook_rt_store(void *addr) {
    // store_counter++;
    //__crpm_cnt_store++;
    //__crpm_cnt_blocks++;
#ifdef LEGACY_HOOK_FUNCTION
    auto registry = crpm::HybridInstEngine::Registry::Get();
    registry->hook_routine(addr);
#else
    thread_local AddressBuffer *bucket =
            &address_buffer[crpm::tl_thread_info.get_thread_id()];
    uint64_t length = bucket->length;
    bucket->ptr[length] = (uintptr_t) addr;
    length++;
    bucket->length = length;
    if (length == kNumBufferedAddresses) {
        address_buffer_clear(bucket);
    }
#endif
}

void __crpm_hook_rt_range_store(void *addr, size_t length) {
    // store_counter++;
    if ((((uint64_t) addr & crpm::kBlockMask) + length) <= crpm::kBlockSize) {
        __crpm_hook_rt_store(addr);
    } else {
        auto registry = crpm::HybridInstEngine::Registry::Get();
        registry->hook_routine(addr, length);
        //__crpm_cnt_range_store++;
        //__crpm_cnt_blocks += (length + 255) / 256;
    }
}

void AnnotateCheckpointRegion(void *addr, size_t length) {
    if (crpm::process_instrumented) {
        __crpm_hook_rt_range_store(addr, length);
    }
}

#endif // USE_HYBRID_INST_ENGINE
