//
// Created by Feng Ren on 2021/1/23.
//

#include <sys/mman.h>
#include <algorithm>
#include "internal/common.h"
#include "internal/engines/nvm_inst_engine.h"

void address_buffer_clear_all();

// #define LEGACY_HOOK_FUNCTION

namespace crpm {
    extern bool process_instrumented;

    NvmInstEngine::Registry::Registry() : default_engine(nullptr) {}

    void NvmInstEngine::Registry::do_register(NvmInstEngine *engine) {
        std::lock_guard<std::mutex> guard(mutex);
        engines.insert(engine);
        if (!default_engine) {
            default_engine = engine;
        }
    }

    void NvmInstEngine::Registry::do_unregister(NvmInstEngine *engine) {
        std::lock_guard<std::mutex> guard(mutex);
        engines.erase(engine);
        if (engine == default_engine) {
            default_engine = nullptr;
        }
    }

    NvmInstEngine *NvmInstEngine::Registry::find(const void *addr) {
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

    NvmInstEngine *NvmInstEngine::Registry::find_address_space(const void *addr) {
        uintptr_t u_addr = (uintptr_t) addr;
        for (auto engine : engines) {
            uintptr_t start = (uintptr_t) engine->image->get_start_address();
            uintptr_t end = (uintptr_t) engine->image->get_end_address();
            if (u_addr >= start && u_addr < end) {
                return engine;
            }
        }
        return nullptr;
    }

    void NvmInstEngine::Registry::hook_routine(const void *addr) {
        NvmInstEngine *engine = find(addr);
        if (likely(engine != nullptr)) {
            engine->hook_routine(addr);
        }
    }

    void NvmInstEngine::Registry::hook_routine(const void *addr, size_t len) {
        NvmInstEngine *engine = find(addr);
        if (likely(engine != nullptr)) {
            engine->hook_routine(addr, len);
        }
    }

    void NvmInstEngine::Registry::hook_copy_on_write_routine(const void *addr) {
        NvmInstEngine *engine = find(addr);
        if (likely(engine != nullptr)) {
            engine->hook_copy_on_write_routine(addr);
        }
    }

    void NvmInstEngine::Registry::hook_copy_on_write_routine(const void *addr, size_t len) {
        NvmInstEngine *engine = find(addr);
        if (likely(engine != nullptr)) {
            engine->hook_copy_on_write_routine(addr, len);
        }
    }

    NvmInstEngine *NvmInstEngine::Registry::get_unique_engine() const {
        if (engines.size() != 1) {
            return nullptr;
        } else {
            return default_engine;
        }
    }

    bool NvmInstEngine::create_checkpoint_image(const char *path, size_t user_capacity,
                                                void *hint_addr, int flags,
                                                const MemoryPoolOption &option) {
        capacity = user_capacity;
        nr_segments = capacity >> kSegmentShift;
        nr_back_segments = nr_segments * option.shadow_capacity_factor;
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

    bool NvmInstEngine::open_checkpoint_image(const char *path, void *hint_addr, int flags) {
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
        nr_back_segments = image->get_nr_back_segments();
        nr_blocks = nr_segments * kBlocksPerSegment;
        capacity = nr_segments * kSegmentSize;
        return true;
    }

    NvmInstEngine *NvmInstEngine::Open(const char *path,
                                       const MemoryPoolOption &option) {
        NvmInstEngine *impl = new NvmInstEngine();
        bool ret;
        int flags = 0;
        bool create = false;
        void *hint_addr = nullptr;

        if (option.fixed_base_address) {
            flags |= MAP_FIXED;
            hint_addr = (void *) option.fixed_base_address;
        }

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

        impl->segment_dirty.allocate(impl->nr_segments);
        impl->block_dirty.allocate(impl->nr_blocks);
        impl->segment_locks = new std::atomic_flag[kSegmentLocks];
        for (uint64_t i = 0; i < kSegmentLocks; ++i) {
            impl->segment_locks[i].clear(std::memory_order_relaxed);
        }

        if (!create) {
            uint64_t a = ReadTSC();
#ifdef USE_IDENTICAL_DATA
            impl->image->recovery(CheckpointImage::SS_Identical);
#else
            impl->image->recovery(CheckpointImage::SS_Back);
#endif
            impl->has_snapshot = impl->exist_snapshot();
            uint64_t b = ReadTSC();
            printf("%.3lf ms\n", (b - a) / 2400000.0);
        }

        impl->address_range.first = (uintptr_t) impl->image->get_main_block(0);
        impl->address_range.second = impl->address_range.first + impl->capacity;

        Registry::Get()->do_register(impl);
        impl->verbose = option.verbose_output;
        impl->has_init = true;
        impl->cleaner = std::thread(&WriteBackThreadRoutine, impl);
        return impl;
    }

    NvmInstEngine::NvmInstEngine() :
            has_init(false),
            has_snapshot(false),
            next_thread_id(0),
            checkpoint_traffic(0),
            flush_latency(0),
            write_back_latency(0),
            cleaner_running(true),
            checkpoint_in_progress(false),
            cleaner_state(WB_IDLE),
            next_back_id(0),
            skip_copy_on_write(false),
            verbose(false) {
        for (uint64_t i = 0; i < kMaxThreads; ++i) {
            flush_blocks[i] = (volatile uint64_t *)
                    malloc(sizeof(uint64_t) * kMaxFlushBlocks);
            flush_blocks_count[i] = 0;
        }
        back_memory_lock.clear(std::memory_order_relaxed);
    }

    NvmInstEngine::~NvmInstEngine() {
        if (has_init) {
            cleaner_running = false;
            cleaner_condvar.notify_all();
            cleaner.join();
            for (uint64_t i = 0; i < kMaxThreads; ++i) {
                free((void *) flush_blocks[i]);
            }
            delete image;
            delete[]segment_locks;
            fs.close();
            Registry::Get()->do_unregister(this);
            if (verbose) {
                printf("checkpoint_traffic: %.3lf MiB\n",
                       checkpoint_traffic / 1000000.0);
                printf("flush_latency: %.3lf ms\n",
                       flush_latency / 2400000.0);
                printf("write_back_latency: %.3lf ms\n",
                       write_back_latency / 2400000.0);
                printf("nr_blocks %ld nr_segments %ld\n", nr_blocks, nr_segments);
            }
        }
    }

    void NvmInstEngine::determine_flush_mode() {
        uint64_t total_blocks = 0;
        bool all_empty = true, has_full = false;
        for (size_t i = 0; i < kMaxThreads; ++i) {
            size_t size = flush_blocks_count[i];
            if (size != 0) {
                all_empty = false;
            }
            // total_blocks += size;
            total_blocks = size;
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

    void NvmInstEngine::checkpoint(uint64_t nr_threads) {
        uint64_t start_clock, persist_clock;
        int tid = next_thread_id.fetch_add(1, std::memory_order_relaxed);
        bool is_leader = (tid == 0);
        bool thread_busy = true;

        barrier.barrier(nr_threads, tid);
        if (is_leader) {
            start_clock = ReadTSC();
            address_buffer_clear_all();
        }

        std::atomic_thread_fence(std::memory_order_release);
        barrier.barrier(nr_threads, tid);
        if (is_leader) {
            thread_busy = (cleaner_state.load(std::memory_order_acquire) != WB_IDLE);
            determine_flush_mode();
            if (flush_mode == FMODE_NO_ACTION) {
                next_thread_id.store(0, std::memory_order_relaxed);
            } else {
                checkpoint_in_progress.store(true, std::memory_order_relaxed);
                skip_copy_on_write = false;
                if (flush_mode == FMODE_WBINVD || thread_busy) {
                    cleaner_mutex.lock();
                } else if (nr_segments == nr_back_segments) {
                    skip_copy_on_write = true;
                }
            }
            // printf("[DEBUG] checkpoint: flush_mode %d, skip_copy_on_write %d\n",
            //        flush_mode, skip_copy_on_write);
            latch.latch_add(tid);
        }
        latch.latch_wait(tid);
        if (flush_mode == FMODE_NO_ACTION) {
            return;
        }

        flush_parallel(tid, nr_threads);
        barrier.barrier(nr_threads, tid);
        if (flush_mode == FMODE_USE_FLUSH_BLOCKS) {
            if (is_leader) {
                commit_layout_state(CheckpointImage::SS_Main);
                persist_clock = ReadTSC();
                latch.latch_add(tid);
            }
            latch.latch_wait(tid);
            uint64_t delta = write_back_parallel(tid, nr_threads);
            checkpoint_traffic.fetch_add(delta, std::memory_order_relaxed);
            barrier.barrier(nr_threads, tid);
            if (is_leader) {
#ifdef USE_IDENTICAL_DATA
                commit_layout_state(CheckpointImage::SS_Identical);
#else
                commit_layout_state(CheckpointImage::SS_Back);
#endif //USE_IDENTICAL_DATA
                uint64_t complete_clock = ReadTSC();
                next_thread_id.store(0, std::memory_order_relaxed);
                flush_latency.fetch_add(persist_clock - start_clock,
                                        std::memory_order_relaxed);
                write_back_latency.fetch_add(complete_clock - persist_clock,
                                             std::memory_order_relaxed);
                if (unlikely(!has_snapshot)) {
                    image->set_attributes(kAttributeHasSnapshot);
                    has_snapshot = true;
                }
                clear_dirty_bits();
                for (uint64_t i = 0; i < kMaxThreads; ++i) {
                    flush_blocks_count[i] = 0;
                }
                checkpoint_in_progress.store(false, std::memory_order_relaxed);
                if (thread_busy) {
                    cleaner_mutex.unlock();
                    cleaner_condvar.notify_all();
                }
                latch.latch_add(tid);
            }
            std::atomic_thread_fence(std::memory_order_release);
            latch.latch_wait(tid);
        } else {
            if (is_leader) {
                commit_layout_state(CheckpointImage::SS_Main);
                segment_dirty.clear_region(0, nr_segments);
                persist_clock = ReadTSC();
                next_thread_id.store(0, std::memory_order_relaxed);
                flush_latency.fetch_add(persist_clock - start_clock,
                                        std::memory_order_relaxed);
                if (unlikely(!has_snapshot)) {
                    image->set_attributes(kAttributeHasSnapshot);
                    has_snapshot = true;
                }
                cleaner_state.store(WB_STARTED, std::memory_order_relaxed);
                for (uint64_t i = 0; i < kMaxThreads; ++i) {
                    flush_blocks_count[i] = 0;
                }
                checkpoint_in_progress.store(false, std::memory_order_relaxed);
                cleaner_mutex.unlock();
                cleaner_condvar.notify_all();
                latch.latch_add(tid);
            }
            std::atomic_thread_fence(std::memory_order_release);
            latch.latch_wait(tid);
        }
    }

    void NvmInstEngine::commit_layout_state(uint8_t state) {
        if (flush_mode == FMODE_WBINVD) {
            image->begin_segment_state_update();
            for (uint64_t seg_id = 0; seg_id < nr_segments; seg_id += AtomicBitSet::kBitWidth) {
                uint64_t bitset = segment_dirty.test_all(seg_id);
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
    }

    bool NvmInstEngine::exist_snapshot() {
        return image->get_attributes() & kAttributeHasSnapshot;
    }

    void *NvmInstEngine::get_address(uint64_t offset) {
        return image->get_main_segment(0) + offset;
    }

    size_t NvmInstEngine::get_capacity() {
        return capacity;
    }

    void NvmInstEngine::wait_for_background_task() {
        std::atomic_thread_fence(std::memory_order_acquire);
        while (cleaner_state.load(std::memory_order_relaxed) != WB_IDLE) {
            _mm_pause();
        }
        // printf("[DEBUG] wait_for_background_task\n");
        checkpoint_traffic = 0;
        flush_latency = 0;
        write_back_latency = 0;
    }

    bool NvmInstEngine::has_background_task() {
        return cleaner_state.load(std::memory_order_acquire) != WB_IDLE;
    }

    uint64_t NvmInstEngine::flush_parallel(int tid, int nr_threads) {
        if (flush_mode == FMODE_WBINVD) {
            if (tid == 0) {
                WriteBackAndInvalidate();
            }
        } else {
            size_t id = tid;
            uint8_t *base_address = (uint8_t *) get_address(0);
            while (id < kMaxThreads) {
                auto &bucket = flush_blocks[id];
                uint64_t bucket_size = flush_blocks_count[id];
                for (uint64_t i = 0; i != bucket_size; ++i) {
                    uint64_t block_id = bucket[i];
                    uint8_t *addr = base_address + (block_id << kBlockShift);
                    FlushRegion(addr, kBlockSize);
                }
                id += nr_threads;
            }
        }
        StoreFence();
        return 0;
    }

    uint64_t NvmInstEngine::write_back_parallel(int tid, int nr_threads) {
        uint64_t flush_count = 0;
        if (flush_mode == FMODE_WBINVD) {
            uint64_t main_id = tid;
            while (main_id < nr_segments) {
                if (!segment_dirty.test(main_id)) {
                    main_id += nr_threads;
                    continue;
                }

                bool created;
                uint64_t back_id = find_back_segment(main_id, created);
                const uint64_t start_block_id = main_id * kBlocksPerSegment;
                const uint64_t stop_block_id =
                        std::min(nr_blocks, start_block_id + kBlocksPerSegment);
                assert(start_block_id % AtomicBitSet::kBitWidth == 0);

                uint8_t *main_base = image->get_main_segment(main_id);
                uint8_t *back_base = image->get_back_segment(back_id);
                if (created && image->get_segment_state(main_id) != CheckpointImage::SS_Initial) {
                    NonTemporalCopy256(back_base, main_base, kSegmentSize);
                    flush_count += kSegmentSize;
                } else {
                    for (uint64_t block_id = start_block_id;
                         block_id < stop_block_id;
                         block_id += AtomicBitSet::kBitWidth) {
                        uint64_t bitset = block_dirty.test_all(block_id);
                        while (bitset != 0) {
                            uint64_t t = bitset & -bitset;
                            int i = __builtin_ctzll(bitset); // i == first set index
                            bitset ^= t;
                            // assert(block_dirty.test(block_id + i));
                            uint8_t *main_addr = main_base + (i << kBlockShift);
                            uint8_t *back_addr = back_base + (i << kBlockShift);
                            NonTemporalCopy256(back_addr, main_addr, kBlockSize);
                            flush_count += kBlockSize;
                        }
                        main_base += AtomicBitSet::kBitWidth * kBlockSize;
                        back_base += AtomicBitSet::kBitWidth * kBlockSize;
                    }
                }
                main_id += nr_threads;
            }
        } else {
            size_t id = tid;
            while (id < kMaxThreads) {
                auto &bucket = flush_blocks[id];
                uint64_t bucket_size = flush_blocks_count[id];
                for (uint64_t i = 0; i != bucket_size; ++i) {
                    uint64_t main_block_id = bucket[i];
                    bool created;
                    uint64_t back_block_id = find_back_block(main_block_id, created);
                    if (created) {
                        uint8_t *main_addr = image->get_main_segment(
                                main_block_id / kBlocksPerSegment);
                        uint8_t *back_addr = image->get_back_segment(
                                back_block_id / kBlocksPerSegment);
                        NonTemporalCopy256(back_addr, main_addr, kSegmentSize);
                        flush_count += kSegmentSize;
                    } else {
                        uint8_t *main_addr = image->get_main_block(main_block_id);
                        uint8_t *back_addr = image->get_back_block(back_block_id);
                        NonTemporalCopy256(back_addr, main_addr, kBlockSize);
                        flush_count += kBlockSize;
                    }
                }
                id += nr_threads;
            }
        }
        StoreFence();
        return flush_count;
    }

    uint64_t NvmInstEngine::find_back_segment(uint64_t segment_id, bool &created) {
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

    uint64_t NvmInstEngine::find_back_block(uint64_t block_id, bool &created) {
        uint64_t back_seg_id = find_back_segment(block_id / kBlocksPerSegment, created);
        return (back_seg_id * kBlocksPerSegment) + (block_id % kBlocksPerSegment);
    }

    void NvmInstEngine::clear_dirty_bits() {
        for (size_t id = 0; id < kMaxThreads; ++id) {
            auto &bucket = flush_blocks[id];
            uint64_t bucket_size = flush_blocks_count[id];
            for (uint64_t i = 0; i != bucket_size; ++i) {
                uint64_t block_id = bucket[i];
                uint64_t page_id = block_id >> (kSegmentShift - kBlockShift);
                block_dirty.clear_all(block_id);
                segment_dirty.clear_all(page_id);
            }
        }
    }

    bool NvmInstEngine::lazy_write_back(uint64_t segment_id, bool on_demand) {
        uint64_t start_clock, write_back_clock;
        uint8_t *address_list[kAddressListCapacity];
        uint64_t address_list_size = 0;
        uint64_t address_count = 0;
        auto &lock = segment_locks[segment_id & (kSegmentLocks - 1)];
        AcquireLock(lock);

        auto attribute = image->get_segment_state(segment_id);
        uint64_t back_segment_id = image->get_main_to_back(segment_id);
        bool created = false;

        if (attribute != CheckpointImage::SS_Main && back_segment_id != kNullSegmentIndex) {
#ifdef USE_IDENTICAL_DATA
            if (on_demand && attribute == CheckpointImage::SS_Identical) {
                image->set_segment_state_atomic(segment_id, CheckpointImage::SS_Back);
            }
#endif
            if (on_demand) {
                segment_dirty.set(segment_id, std::memory_order_relaxed);
            }
            ReleaseLock(lock);
            return false;
        }

        start_clock = ReadTSC();
        const uint64_t start_block_id = segment_id * kBlocksPerSegment;
        const uint64_t stop_block_id = std::min(nr_blocks, start_block_id + kBlocksPerSegment);
        assert(start_block_id % AtomicBitSet::kBitWidth == 0);

        if (back_segment_id == kNullSegmentIndex) {
            if (!on_demand) {
                ReleaseLock(lock);
                return false;
            }
            back_segment_id = find_back_segment(segment_id, created);
        }

        uint64_t delta = capacity + back_segment_id * kSegmentSize - segment_id * kSegmentSize;
        if (created) {
            if (attribute != CheckpointImage::SS_Initial) {
                uint8_t *addr = (uint8_t *) get_address(start_block_id << kBlockShift);
                NonTemporalCopy256(addr + delta, addr, kSegmentSize);
            }
        } else {
            for (uint64_t block_id = start_block_id;
                 block_id < stop_block_id;
                 block_id += AtomicBitSet::kBitWidth) {
                uint64_t bitset = block_dirty.test_all(block_id);
                uint8_t *base_address = (uint8_t *) get_address(block_id << kBlockShift);
                while (bitset != 0) {
                    uint64_t t = bitset & -bitset;
                    int i = __builtin_ctzll(bitset); // i == first set index
                    bitset ^= t;
                    address_list[address_list_size++] = base_address + (i << kBlockShift);
                    address_count++;
                    if (address_list_size == kAddressListCapacity) {
                        for (int j = 0; j < address_list_size; ++j) {
                            uint8_t *addr = address_list[j];
                            NonTemporalCopy256(addr + delta, addr, kBlockSize);
                        }
                        address_list_size = 0;
                    }
                }
            }
            for (int i = 0; i < address_list_size; ++i) {
                uint8_t *addr = address_list[i];
                NonTemporalCopy256(addr + delta, addr, kBlockSize);
            }
        }
        StoreFence();

#ifdef USE_IDENTICAL_DATA
        uint8_t state = on_demand ? CheckpointImage::SS_Back : CheckpointImage::SS_Identical;
        image->set_segment_state_atomic(segment_id, state);
#else
        image->set_segment_state_atomic(segment_id, CheckpointImage::SS_Back);
#endif
        block_dirty.clear_region(start_block_id, stop_block_id);
        write_back_clock = ReadTSC();
        write_back_latency.fetch_add(write_back_clock - start_clock,
                                     std::memory_order_relaxed);
        checkpoint_traffic.fetch_add(address_count * kBlockSize,
                                     std::memory_order_relaxed);
        if (on_demand) {
            segment_dirty.set(segment_id, std::memory_order_relaxed);
        }
        ReleaseLock(lock);
        return true;
    }

    void NvmInstEngine::allocate_back_segment(uint64_t main_id) {
        const size_t kNumBackSegments = image->get_nr_back_segments();
        AcquireLock(back_memory_lock);
        uint64_t loop_count = 0;
        while (loop_count < kNumBackSegments) {
            uint64_t old_main_id = image->get_back_to_main(next_back_id);
            if (old_main_id == kNullSegmentIndex) {
                image->bind_back_segment(main_id, next_back_id);
                advance_next_back_segment();
                ReleaseLock(back_memory_lock);
                return;
            }
            if (checkpoint_in_progress.load(std::memory_order_relaxed)) {
                if (segment_dirty.test(old_main_id)) {
                    advance_next_back_segment();
                    loop_count++;
                    continue;
                }

                image->bind_back_segment(main_id, next_back_id);
                advance_next_back_segment();
                ReleaseLock(back_memory_lock);
                return;
            } else {
                auto &lock = segment_locks[old_main_id & (kSegmentLocks - 1)];
                if (!TryAcquireLock(lock)) {
                    advance_next_back_segment();
                    loop_count++;
                    continue;
                }
                if (segment_dirty.test(old_main_id)) {
                    ReleaseLock(lock);
                    advance_next_back_segment();
                    loop_count++;
                    continue;
                }

                image->bind_back_segment(main_id, next_back_id);
                advance_next_back_segment();
                ReleaseLock(lock);
                ReleaseLock(back_memory_lock);
                return;
            }
        }
    }

    void NvmInstEngine::hook_routine(const void *addr, size_t len) {
        uint64_t delta = (uint64_t) addr - address_range.first;
        for (uintptr_t ptr = delta & ~kBlockMask; ptr < delta + len; ptr += kBlockSize) {
            hook_routine((void *) (address_range.first + ptr));
        }
    }

    void NvmInstEngine::hook_routine(const void *addr) {
        uint64_t delta = (uint64_t) addr - address_range.first;
        uint64_t block_id = delta >> kBlockShift;
        if (block_dirty.test(block_id, std::memory_order_acquire)) {
            return;
        }

        block_dirty.set(block_id, std::memory_order_release);
        thread_local unsigned int tid = tl_thread_info.get_thread_id();
        auto &bucket = flush_blocks[tid];
        auto &bucket_size = flush_blocks_count[tid];
        if (likely(bucket_size != kMaxFlushBlocks)) {
            bucket[bucket_size] = block_id;
            ++bucket_size;
        }
    }

    void NvmInstEngine::hook_copy_on_write_routine(const void *addr, size_t len) {
        uint64_t delta = (uint64_t) addr - address_range.first;
        uint64_t start_segment_id = delta >> kSegmentShift;
        uint64_t end_segment_id = (delta + len + kSegmentMask) >> kSegmentShift;
        for (uintptr_t segment_id = start_segment_id; segment_id < end_segment_id; ++segment_id) {
            if (!segment_dirty.test(segment_id, std::memory_order_acquire)) {
                if (skip_copy_on_write) {
                    segment_dirty.set(segment_id, std::memory_order_release);
                } else {
                    lazy_write_back(segment_id, true);
                }
            }
        }
    }

    void NvmInstEngine::hook_copy_on_write_routine(const void *addr) {
        uint64_t segment_id = ((uint64_t) addr - address_range.first) >> kSegmentShift;
        if (!segment_dirty.test(segment_id, std::memory_order_acquire)) {
            if (skip_copy_on_write) {
                segment_dirty.set(segment_id, std::memory_order_release);
            } else {
                lazy_write_back(segment_id, true);
            }
        }
    }

    void NvmInstEngine::WriteBackThreadRoutine(NvmInstEngine *engine) {
        BindSingleSocket();
        assert(engine);
        uint64_t segment_id;
        CleanerState state;
        std::unique_lock<std::mutex> lock(engine->cleaner_mutex);
        while (engine->cleaner_running) {
            state = engine->cleaner_state.load(std::memory_order_relaxed);
            switch (state) {
                case WB_STARTED:
                    segment_id = 0;
                    engine->cleaner_state.store(WB_RUNNING, std::memory_order_relaxed);
                    break;
                case WB_RUNNING:
                    if (engine->checkpoint_in_progress.load(std::memory_order_relaxed)) {
                        engine->cleaner_condvar.wait(lock);
                        continue;
                    }
                    // printf("[DEBUG] cleaner: write_back %ld\n", segment_id);
                    // Should be removed for Masstree
                    engine->lazy_write_back(segment_id);
                    segment_id++;
                    if (segment_id == engine->nr_segments) {
                        engine->cleaner_state.store(WB_IDLE, std::memory_order_release);
                    }
                    break;
                case WB_IDLE:
                    engine->cleaner_condvar.wait(lock);
                    break;
                default:
                    assert(0);
            }
        }
    }

#ifdef USE_MPI_EXTENSION
    NvmInstEngine * NvmInstEngine::OpenForMPI(const char *path,
                                              const MemoryPoolOption &option,
                                              MPI_Comm comm) {
        NvmInstEngine *impl = new NvmInstEngine();
        bool ret;
        int flags = 0;
        int create = 0, all_create, comm_size;
        void *hint_addr = nullptr;

        if (option.fixed_base_address) {
            flags |= MAP_FIXED;
            hint_addr = (void *) option.fixed_base_address;
        }

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

        impl->segment_dirty.allocate(impl->nr_segments);
        impl->block_dirty.allocate(impl->nr_blocks);
        impl->segment_locks = new std::atomic_flag[kSegmentLocks];
        for (uint64_t i = 0; i < kSegmentLocks; ++i) {
            impl->segment_locks[i].clear(std::memory_order_relaxed);
        }

        if (!create) {
            uint64_t a = ReadTSC();
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
#ifdef USE_IDENTICAL_DATA
            impl->image->recovery(CheckpointImage::SS_Identical);
#else
            impl->image->recovery(CheckpointImage::SS_Back);
#endif
            impl->has_snapshot = impl->exist_snapshot();
            uint64_t b = ReadTSC();
            printf("%.3lf ms\n", (b - a) / 2400000.0);
        }

        impl->address_range.first = (uintptr_t) impl->image->get_main_block(0);
        impl->address_range.second = impl->address_range.first + impl->capacity;

        Registry::Get()->do_register(impl);
        impl->verbose = option.verbose_output;
        impl->has_init = true;
        impl->cleaner = std::thread(&WriteBackThreadRoutine, impl);
        return impl;
    }

    void NvmInstEngine::checkpoint_for_mpi(uint64_t nr_threads, MPI_Comm comm) {
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
                flush_mode = FMODE_USE_FLUSH_BLOCKS;
            } else {
                checkpoint_in_progress.store(true, std::memory_order_relaxed);
                cleaner_mutex.lock();
            }
            latch.latch_add(tid);
        }
        latch.latch_wait(tid);
        flush_parallel(tid, nr_threads);
        barrier.barrier(nr_threads, tid);

        if (is_leader) {
            commit_layout_state_for_mpi(CheckpointImage::SS_Main, comm);
            segment_dirty.clear_region(0, nr_segments);
            persist_clock = ReadTSC();
            for (uint64_t id = 0; id < kMaxThreads; ++id) {
                flush_blocks_count[id] = 0;
            }
            next_thread_id.store(0, std::memory_order_relaxed);
            flush_latency.fetch_add(persist_clock - start_clock,
                                    std::memory_order_relaxed);
            if (unlikely(!has_snapshot)) {
                image->set_attributes(kAttributeHasSnapshot);
                has_snapshot = true;
            }
            cleaner_state.store(WB_STARTED, std::memory_order_relaxed);
            for (uint64_t i = 0; i < kMaxThreads; ++i) {
                flush_blocks_count[i] = 0;
            }
            checkpoint_in_progress.store(false, std::memory_order_relaxed);
            cleaner_mutex.unlock();
            cleaner_condvar.notify_all();
            latch.latch_add(tid);
        }
        std::atomic_thread_fence(std::memory_order_release);
        latch.latch_wait(tid);
    }

    void NvmInstEngine::commit_layout_state_for_mpi(uint8_t state, MPI_Comm comm) {
        if (flush_mode == FMODE_WBINVD) {
            image->begin_segment_state_update();
            for (uint64_t seg_id = 0; seg_id < nr_segments; seg_id += AtomicBitSet::kBitWidth) {
                uint64_t bitset = segment_dirty.test_all(seg_id);
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
    }

#endif // USE_MPI_EXTENSION
}

#ifdef USE_NVM_INST_ENGINE
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
const static size_t kNumBufferedAddresses = 120;
struct AddressBuffer {
    volatile uint64_t length;
    std::atomic_flag spinlock;
    uint64_t padding[6];
    volatile uint64_t ptr[kNumBufferedAddresses];
};

static_assert(sizeof(AddressBuffer) == 1024, "wrong address buffer size");

alignas(1024) AddressBuffer address_buffer[crpm::kMaxThreads];
alignas(64) uint64_t stack_start_addr, stack_end_addr;

#define INLINE_HOOK

__attribute__((noinline))
void address_buffer_clear(AddressBuffer *buffer) {
    std::atomic_thread_fence(std::memory_order_acquire);
    while (buffer->spinlock.test_and_set(std::memory_order_relaxed)) {}
    auto registry = crpm::NvmInstEngine::Registry::Get();
    uint64_t length = buffer->length;
#ifdef INLINE_HOOK
    auto engine = registry->get_unique_engine();
    if (engine) {
        uintptr_t start_addr = (uintptr_t) engine->get_address(0);
        uintptr_t end_addr = start_addr + engine->get_capacity();
        for (uint64_t i = 0; i != length; ++i) {
            uintptr_t addr = buffer->ptr[i];
            if (addr >= start_addr && addr < end_addr) {
                engine->hook_routine((const void *) addr);
            }
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
}

void __crpm_hook_rt_store(void *addr) {
    // store_counter++;
#ifdef LEGACY_HOOK_FUNCTION
    auto registry = crpm::NvmInstEngine::Registry::Get();
    registry->hook_copy_on_write_routine(addr);
    registry->hook_routine(addr);
#else
    auto registry = crpm::NvmInstEngine::Registry::Get();
    registry->hook_copy_on_write_routine(addr);
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
        auto registry = crpm::NvmInstEngine::Registry::Get();
        registry->hook_copy_on_write_routine(addr, length);
        registry->hook_routine(addr, length);
    }
}

void AnnotateCheckpointRegion(void *addr, size_t length) {
    if (crpm::process_instrumented) {
        __crpm_hook_rt_range_store(addr, length);
    }
}

#endif // USE_NVM_INST_ENGINE
