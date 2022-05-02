//
// Created by Feng Ren on 2021/1/23.
//

#include <sys/mman.h>
#include "internal/common.h"
#include "internal/engines/undolog_engine.h"

void address_buffer_clear_all();

namespace crpm {
    extern bool process_instrumented;

    UndoLogEngine::Registry::Registry() : default_engine(nullptr) {}

    void UndoLogEngine::Registry::do_register(UndoLogEngine *engine) {
        std::lock_guard<std::mutex> guard(mutex);
        engines.insert(engine);
        if (!default_engine) {
            default_engine = engine;
        }
    }

    void UndoLogEngine::Registry::do_unregister(UndoLogEngine *engine) {
        std::lock_guard<std::mutex> guard(mutex);
        engines.erase(engine);
        if (engine == default_engine) {
            default_engine = nullptr;
        }
    }

    UndoLogEngine *UndoLogEngine::Registry::find(const void *addr) {
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

    void UndoLogEngine::Registry::hook_routine(const void *addr) {
        UndoLogEngine *engine = find(addr);
        if (likely(engine != nullptr)) {
            engine->hook_routine(addr);
        }
    }

    void UndoLogEngine::Registry::hook_routine(const void *addr, size_t len) {
        UndoLogEngine *engine = find(addr);
        if (likely(engine != nullptr)) {
            engine->hook_routine(addr, len);
        }
    }

    UndoLogEngine *UndoLogEngine::Registry::get_unique_engine() const {
        if (engines.size() != 1) {
            return nullptr;
        } else {
            return default_engine;
        }
    }

    UndoLogEngine *UndoLogEngine::Open(const char *path,
                                       const MemoryPoolOption &option) {
        UndoLogEngine *impl = new UndoLogEngine();
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
            uint64_t fs_size = kHugePageSize + capacity * 2;
            ret = impl->fs.create(path, fs_size, flags, hint_addr);
            if (!ret) {
                delete impl;
                return nullptr;
            }
            impl->header = (HeapHeader *) impl->fs.rel_to_abs(0);
            HeapHeader *hdr = impl->header;
            memset(hdr, 0, sizeof(HeapHeader));
            hdr->magic = kHeapHeaderMagic;
            hdr->nr_blocks = impl->nr_blocks;
            hdr->data_offset = kHugePageSize;
            hdr->log_offset = hdr->data_offset + capacity;
            hdr->log_capacity = capacity;
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

        impl->block_dirty.allocate(impl->nr_blocks);
        impl->address_range.first = (uintptr_t) impl->fs.rel_to_abs(impl->header->data_offset);
        impl->address_range.second = (uintptr_t) impl->fs.rel_to_abs(impl->header->log_offset);
        impl->segment_locks = new std::atomic_flag[kSegmentLocks];
        for (uint64_t i = 0; i < kSegmentLocks; ++i) {
            impl->segment_locks[i].clear(std::memory_order_relaxed);
        }
        Registry::Get()->do_register(impl);
        impl->verbose = option.verbose_output;
        impl->has_init = true;
        return impl;
    }

    UndoLogEngine::UndoLogEngine() :
            has_init(false),
            has_snapshot(false),
            next_thread_id(0),
            checkpoint_traffic(0),
            flush_latency(0),
            write_back_latency(0),
            log_head(0),
            verbose(false) {
        for (uint64_t i = 0; i < kMaxThreads; ++i) {
            flush_blocks[i] = (volatile uint64_t *)
                    malloc(sizeof(uint64_t) * kMaxFlushBlocks);
            flush_blocks_count[i] = 0;
        }
    }

    UndoLogEngine::~UndoLogEngine() {
        if (has_init) {
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

    void UndoLogEngine::checkpoint(uint64_t nr_threads) {
        uint64_t start_clock, persist_clock, complete_clock;
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
        clear_dirty_bits_parallel(tid, nr_threads);
        if (is_leader) {
            next_thread_id.store(0, std::memory_order_relaxed);
            persist_clock = ReadTSC();
            flush_latency.fetch_add(persist_clock - start_clock,
                                    std::memory_order_relaxed);
            write_back_latency.fetch_add(complete_clock - persist_clock,
                                         std::memory_order_relaxed);
            if (!has_snapshot) {
                NTStore32(&header->attributes, kAttributeHasSnapshot);
                has_snapshot = true;
            }
            NTStore((uint64_t *) &header->log_head, 0);
            StoreFence();
            log_head.store(0, std::memory_order_relaxed);
            latch.latch_add(tid);
        }
        std::atomic_thread_fence(std::memory_order_release);
        latch.latch_wait(tid);
    }

    bool UndoLogEngine::exist_snapshot() {
        return header->attributes & kAttributeHasSnapshot;
    }

    void *UndoLogEngine::get_address(uint64_t offset) {
        return (uint8_t *) address_range.first + offset;
    }

    size_t UndoLogEngine::get_capacity() {
        return nr_blocks << kBlockShift;
    }

    uint64_t UndoLogEngine::flush_parallel(int tid, int nr_threads) {
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

    void UndoLogEngine::clear_dirty_bits_parallel(int tid, int nr_threads) {
        if (flush_mode == FMODE_WBINVD) {
            size_t id = tid * AtomicBitSet::kBitWidth;
            while (id < nr_blocks) {
                block_dirty.clear_all(id);
                id += nr_threads * AtomicBitSet::kBitWidth;
            }
            id = tid;
            while (id < kMaxThreads) {
                flush_blocks_count[id] = 0;
                id += nr_threads;
            }
        } else {
            size_t id = tid;
            while (id < kMaxThreads) {
                auto &bucket = flush_blocks[id];
                uint64_t bucket_size = flush_blocks_count[id];
                for (uint64_t i = 0; i != bucket_size; ++i) {
                    uint64_t block_id = bucket[i];
                    block_dirty.clear_all(block_id);
                }
                flush_blocks_count[id] = 0;
                id += nr_threads;
            }
        }
    }

    void UndoLogEngine::recover() {
        uint64_t cursor = 0;
        uint8_t *data_base = (uint8_t *) fs.rel_to_abs(header->data_offset);
        uint8_t *log_base = (uint8_t *) fs.rel_to_abs(header->log_offset);
        while (cursor < header->log_head) {
            LogEntry *entry = (LogEntry *) (log_base + cursor);
            if (entry->magic != kLogEntryMagic) {
                fprintf(stderr, "broken log entry\n");
                exit(EXIT_FAILURE);
            }
            uint8_t *target = data_base + entry->offset;
            NonTemporalCopy64(target, entry->payload, entry->size);
            cursor += RoundUp(sizeof(LogEntry) + entry->size, kCacheLineSize);
        }
        StoreFence();
        NTStore((uint64_t *) &header->log_head, 0);
        StoreFence();
    }

    void UndoLogEngine::hook_routine(const void *addr, size_t len) {
        uint64_t delta = (uint64_t) addr - address_range.first;
        for (uintptr_t ptr = delta & ~kBlockMask; ptr < delta + len; ptr += kBlockSize) {
            hook_routine((void *) (address_range.first + ptr));
        }
    }

    void UndoLogEngine::hook_routine(const void *addr) {
        uint64_t delta = (uint64_t) addr - address_range.first;
        uint64_t block_id = delta >> kBlockShift;
        uint64_t log_size = sizeof(LogEntry) + RoundUp(kBlockSize, kCacheLineSize);
        if (block_dirty.test(block_id, std::memory_order_acquire)) {
            return;
        }

        auto &lock = segment_locks[block_id & (kSegmentLocks - 1)];
        AcquireLock(lock);
        if (block_dirty.test(block_id, std::memory_order_acquire)) {
            ReleaseLock(lock);
            return;
        }

        uint64_t last_log_head = log_head.fetch_add(log_size);
        if (last_log_head + log_size >= header->log_capacity) {
            fprintf(stderr, "Out of log memory\n");
            exit(EXIT_FAILURE);
        }

        LogEntry *entry = (LogEntry *) (address_range.second + last_log_head);
        entry->magic = kLogEntryMagic;
        entry->offset = block_id << kBlockShift;
        entry->size = kBlockSize;
        Flush(entry);
        uintptr_t target = address_range.first + entry->offset;
        NonTemporalCopy64(entry->payload, (void *) target, entry->size);
        StoreFence();
        while (header->log_head < last_log_head + log_size) {
            if (__sync_bool_compare_and_swap(&header->log_head,
                                             last_log_head,
                                             last_log_head + log_size)) {
                Flush((uint64_t *) &header->log_head);
                StoreFence();
                break;
            }
        }
        checkpoint_traffic.fetch_add(
                sizeof(UndoLogEngine::LogEntry) + kBlockSize + sizeof(uint64_t),
                std::memory_order_relaxed);

        block_dirty.set(block_id, std::memory_order_release);
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

#ifdef USE_UNDOLOG_ENGINE
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
    auto registry = crpm::UndoLogEngine::Registry::Get();
    registry->hook_routine((void *) addr);
}

void __crpm_hook_rt_range_store(void *addr, size_t length) {
    // store_counter++;
    if ((((uint64_t) addr & crpm::kBlockMask) + length) <= crpm::kBlockSize) {
        __crpm_hook_rt_store(addr);
    } else {
        auto registry = crpm::UndoLogEngine::Registry::Get();
        registry->hook_routine(addr, length);
    }
}

void AnnotateCheckpointRegion(void *addr, size_t length) {
    if (crpm::process_instrumented) {
        __crpm_hook_rt_range_store(addr, length);
    }
}
#endif // USE_COW_ENGINE