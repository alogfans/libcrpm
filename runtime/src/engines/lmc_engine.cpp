//
// Created by Feng Ren on 2021/1/23.
//

#include <sys/mman.h>
#include "internal/common.h"
#include "internal/engines/lmc_engine.h"

void address_buffer_clear_all();

namespace crpm {
    extern bool process_instrumented;

    LmcEngine::Registry::Registry() : default_engine(nullptr) {}

    void LmcEngine::Registry::do_register(LmcEngine *engine) {
        std::lock_guard<std::mutex> guard(mutex);
        engines.insert(engine);
        if (!default_engine) {
            default_engine = engine;
        }
    }

    void LmcEngine::Registry::do_unregister(LmcEngine *engine) {
        std::lock_guard<std::mutex> guard(mutex);
        engines.erase(engine);
        if (engine == default_engine) {
            default_engine = nullptr;
        }
    }

    LmcEngine *LmcEngine::Registry::find(const void *addr) {
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

    void LmcEngine::Registry::hook_routine(const void *addr) {
        LmcEngine *engine = find(addr);
        if (likely(engine != nullptr)) {
            engine->hook_routine(addr);
        }
    }

    void LmcEngine::Registry::hook_routine(const void *addr, size_t len) {
        LmcEngine *engine = find(addr);
        if (likely(engine != nullptr)) {
            engine->hook_routine(addr, len);
        }
    }

    LmcEngine *LmcEngine::Registry::get_unique_engine() const {
        if (engines.size() != 1) {
            return nullptr;
        } else {
            return default_engine;
        }
    }

    LmcEngine *LmcEngine::Open(const char *path,
                               const MemoryPoolOption &option) {
        LmcEngine *impl = new LmcEngine();
        void *hint_addr = nullptr;
        bool create = false;
        int flags = 0;
        bool ret;

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
            impl->nr_blocks = capacity >> kBlockShift;
            if (capacity & kBlockMask) {
                impl->nr_blocks++;
            }
            capacity = (impl->nr_blocks << kBlockShift);
            uint64_t fs_size =
                    RoundUp(sizeof(HeapHeader) + impl->nr_blocks, kHugePageSize) + capacity * 2;
            ret = impl->fs.create(path, fs_size, flags, hint_addr);
            if (!ret) {
                delete impl;
                return nullptr;
            }
            impl->header = (HeapHeader *) impl->fs.rel_to_abs(0);
            HeapHeader *hdr = impl->header;
            memset(hdr, 0, sizeof(HeapHeader) + impl->nr_blocks);
            hdr->magic = kHeapHeaderMagic;
            hdr->current_epoch = 1;
            hdr->nr_blocks = impl->nr_blocks;
            hdr->working_state_offset = RoundUp(sizeof(HeapHeader) +
                                                impl->nr_blocks, kHugePageSize);
            hdr->shadow_state_offset = hdr->working_state_offset + capacity;
            FlushRegion(impl->header, sizeof(HeapHeader) + impl->nr_blocks);
            StoreFence();
        } else {
            ret = impl->fs.open(path, flags, hint_addr);
            if (!ret) {
                delete impl;
                return nullptr;
            }
            impl->header = (HeapHeader *) impl->fs.rel_to_abs(0);
            HeapHeader *hdr = impl->header;
            if (hdr->magic != kHeapHeaderMagic) {
                delete impl;
                return nullptr;
            }
            impl->nr_blocks = hdr->nr_blocks;
            impl->recover();
            impl->has_snapshot = impl->exist_snapshot();
        }

        impl->address_range.first =
                (uintptr_t) impl->fs.rel_to_abs(impl->header->working_state_offset);
        impl->address_range.second =
                (uintptr_t) impl->fs.rel_to_abs(impl->header->shadow_state_offset);
        impl->segment_locks = new std::atomic_flag[kSegmentLocks];
        for (uint64_t i = 0; i < kSegmentLocks; ++i) {
            impl->segment_locks[i].clear(std::memory_order_relaxed);
        }
        Registry::Get()->do_register(impl);
        impl->verbose = option.verbose_output;
        impl->has_init = true;
        return impl;
    }

    LmcEngine::LmcEngine() :
            has_init(false),
            has_snapshot(false),
            next_thread_id(0),
            checkpoint_traffic(0),
            flush_latency(0),
            write_back_latency(0),
            verbose(false) {
        for (uint64_t i = 0; i < kMaxThreads; ++i) {
            flush_blocks[i] = (volatile uint64_t *)
                    malloc(sizeof(uint64_t) * kMaxFlushBlocks);
            flush_blocks_count[i] = 0;
        }
    }

    LmcEngine::~LmcEngine() {
        if (has_init) {
            delete[]segment_locks;
            for (uint64_t i = 0; i < kMaxThreads; ++i) {
                free((void *) flush_blocks[i]);
            }
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

    void LmcEngine::checkpoint(uint64_t nr_threads) {
        uint64_t start_clock, persist_clock;
        int tid = next_thread_id.fetch_add(1, std::memory_order_relaxed);
        bool is_leader = (tid == 0);

        barrier.barrier(nr_threads, tid);
        if (is_leader) {
            start_clock = ReadTSC();
            std::atomic_thread_fence(std::memory_order_acquire);
            bool all_empty = true, has_full = false;
            for (size_t i = 0; i < kMaxThreads; ++i) {
                size_t size = flush_blocks_count[i];
                if (size != 0) {
                    all_empty = false;
                }
                if (size == kMaxFlushBlocks) {
                    has_full = true;
                }
            }
            if (all_empty) {
                flush_mode = FMODE_NO_ACTION;
                next_thread_id.store(0, std::memory_order_relaxed);
            } else if (has_full) {
                flush_mode = FMODE_WBINVD;
            } else {
                flush_mode = FMODE_USE_FLUSH_BLOCKS;
            }
            latch.latch_add(tid);
        }
        latch.latch_wait(tid);
        if (flush_mode == FMODE_NO_ACTION) {
            return;
        }

        flush_parallel(tid, nr_threads);
        barrier.barrier(nr_threads, tid);
        if (is_leader) {
            next_thread_id.store(0, std::memory_order_relaxed);
            persist_clock = ReadTSC();
            flush_latency.fetch_add(persist_clock - start_clock,
                                    std::memory_order_relaxed);
            if (!has_snapshot) {
                NTStore32(&header->attributes, kAttributeHasSnapshot);
                StoreFence();
                has_snapshot = true;
            }
            advance_current_epoch();
            for (uint64_t i = 0; i < kMaxThreads; ++i) {
                flush_blocks_count[i] = 0;
            }
            latch.latch_add(tid);
        }
        std::atomic_thread_fence(std::memory_order_release);
        latch.latch_wait(tid);
    }

    bool LmcEngine::exist_snapshot() {
        return header->attributes & kAttributeHasSnapshot;
    }

    void *LmcEngine::get_address(uint64_t offset) {
        return (uint8_t *) address_range.first + offset;
    }

    size_t LmcEngine::get_capacity() {
        return nr_blocks << kBlockShift;
    }

    uint64_t LmcEngine::flush_parallel(int tid, int nr_threads) {
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
                    uint8_t *addr = base_address + (bucket[i] << kBlockShift);
                    FlushRegion(addr, kBlockSize);
                }
                id += nr_threads;
            }
        }
        StoreFence();
        return 0;
    }

    void LmcEngine::recover() {
        uint8_t *working_base = (uint8_t *) fs.rel_to_abs(header->working_state_offset);
        uint8_t *shadow_base = (uint8_t *) fs.rel_to_abs(header->shadow_state_offset);
        for (uint64_t i = 0; i < nr_blocks; ++i) {
            if (header->tagmap[i] == header->current_epoch) {
                uint8_t *working = working_base + (i << kBlockShift);
                uint8_t *shadow = shadow_base + (i << kBlockShift);
                NonTemporalCopy64(working, shadow, kBlockSize);
            }
        }
        StoreFence();
        advance_current_epoch();
    }

    void LmcEngine::advance_current_epoch() {
        if (header->current_epoch == UINT8_MAX) {
            memset(header->tagmap, 0, nr_blocks);
            FlushRegion(header->tagmap, nr_blocks);
            StoreFence();
            header->current_epoch = 1;
            Flush(&header->current_epoch);
            StoreFence();
        } else {
            header->current_epoch++;
            Flush(&header->current_epoch);
            StoreFence();
        }
    }

    void LmcEngine::hook_routine(const void *addr, size_t len) {
        uint64_t delta = (uint64_t) addr - address_range.first;
        for (uintptr_t ptr = delta & ~kBlockMask; ptr < delta + len; ptr += kBlockSize) {
            hook_routine((void *) (address_range.first + ptr));
        }
    }

    void LmcEngine::hook_routine(const void *addr) {
        uint64_t delta = (uint64_t) addr - address_range.first;
        uint64_t block_id = delta >> kBlockShift;

        auto &lock = segment_locks[block_id & (kSegmentLocks - 1)];
        AcquireLock(lock);
        if (header->tagmap[block_id] == header->current_epoch) {
            ReleaseLock(lock);
            return;
        }
        uint8_t *working = (uint8_t *) address_range.first + (block_id << kBlockShift);
        uint8_t *shadow = (uint8_t *) address_range.second + (block_id << kBlockShift);
        NonTemporalCopy64(shadow, working, kBlockSize);
        StoreFence();
        header->tagmap[block_id] = header->current_epoch;
        Flush(&header->tagmap[block_id]);
        StoreFence();
        checkpoint_traffic.fetch_add(kBlockSize, std::memory_order_relaxed);
        ReleaseLock(lock);

        thread_local unsigned int tid = tl_thread_info.get_thread_id();
        auto &bucket = flush_blocks[tid];
        auto &bucket_size = flush_blocks_count[tid];
        if (likely(bucket_size != kMaxFlushBlocks)) {
            bucket[bucket_size] = block_id;
            ++bucket_size;
        }
    }
}

#ifdef USE_LMC_ENGINE
extern "C" {
__attribute__((noinline))
void __crpm_hook_rt_init();

void __crpm_hook_rt_fini();

void __crpm_hook_rt_store(void *addr);

void __crpm_hook_rt_range_store(void *addr, size_t length);
}

void address_buffer_clear_all() { }
alignas(64) uint64_t stack_start_addr, stack_end_addr;

void __crpm_hook_rt_init() {
    // store_counter = 0;
    crpm::GetStackAddressSpace(stack_start_addr, stack_end_addr);
    crpm::process_instrumented = true;
}

void __crpm_hook_rt_fini() {
    // printf("[fini] store_counter = %ld\n", store_counter);
}

void __crpm_hook_rt_store(void *addr) {
    // store_counter++;
    auto registry = crpm::LmcEngine::Registry::Get();
    registry->hook_routine((void *) addr);
}

void __crpm_hook_rt_range_store(void *addr, size_t length) {
    // store_counter++;
    if ((((uint64_t) addr & crpm::kBlockMask) + length) <= crpm::kBlockSize) {
        __crpm_hook_rt_store(addr);
    } else {
        auto registry = crpm::LmcEngine::Registry::Get();
        registry->hook_routine(addr, length);
    }
}

void AnnotateCheckpointRegion(void *addr, size_t length) {
    if (crpm::process_instrumented) {
        __crpm_hook_rt_range_store(addr, length);
    }
}
#endif // USE_LMC_ENGINE
