//
// Created by Feng Ren on 2021/1/23.
//

#include <sys/mman.h>
#include "internal/common.h"
#include "internal/engines/mprotect_engine.h"

namespace crpm {
    static thread_local int tl_nested_signals = 0;

    MProtectEngine::Registry::Registry() : default_engine(nullptr) {
        int ret;
        struct sigaction sa;
        sa.sa_flags = SA_SIGINFO;
        sigemptyset(&sa.sa_mask);
        sa.sa_sigaction = &Registry::SegfaultHandlerProc;
        ret = sigaction(SIGSEGV, &sa, nullptr);
        if (ret < 0) {
            perror("sigaction");
            exit(EXIT_FAILURE);
        }
    }

    void MProtectEngine::Registry::do_register(MProtectEngine *engine) {
        std::lock_guard<std::mutex> guard(mutex);
        engines.insert(engine);
        if (!default_engine) {
            default_engine = engine;
        }
    }

    void MProtectEngine::Registry::do_unregister(MProtectEngine *engine) {
        std::lock_guard<std::mutex> guard(mutex);
        engines.erase(engine);
        if (engine == default_engine) {
            default_engine = nullptr;
        }
    }

    MProtectEngine *MProtectEngine::Registry::find(const void *addr) {
        uintptr_t u_addr = (uintptr_t) addr;
        if (likely(default_engine != nullptr)) {
            auto &range = default_engine->address_range;
            if (u_addr >= range.first && u_addr < range.second) {
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

    void MProtectEngine::Registry::SegfaultHandlerProc(
            int sig, siginfo_t *info, void *ucontext) {
        if (tl_nested_signals) {
            fprintf(stderr, "nested signal is forbidden\n");
            exit(EXIT_FAILURE);
        }

        if (sig != SIGSEGV) {
            fprintf(stderr, "unknown signal (sig=%d)\n", sig);
            exit(EXIT_FAILURE);
        }

        if (!(info->si_code & SEGV_ACCERR)) {
            fprintf(stderr, "segmentation fault (addr=%p)\n", info->si_addr);
            exit(EXIT_FAILURE);
        }

        MProtectEngine *engine = Registry::Get()->find(info->si_addr);
        if (!engine) {
            fprintf(stderr, "cannot find appropriate memory (addr=%p)\n", info->si_addr);
            exit(EXIT_FAILURE);
        }

        ++tl_nested_signals;
        engine->sigfault_handler(info->si_addr);
        --tl_nested_signals;
    }

    MProtectEngine *MProtectEngine::Open(const char *path,
                                         const MemoryPoolOption &option) {
        MProtectEngine *impl = new MProtectEngine();
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
            impl->nr_regions = capacity >> kRegionShift;
            if (capacity & kRegionMask) {
                impl->nr_regions++;
                capacity = impl->nr_regions << kRegionShift;
            }
            impl->capacity = capacity;
            uint64_t fs_size = MetadataV1::GetLayoutSize(capacity);
            ret = impl->fs.create(path, fs_size, flags, hint_addr);
            if (!ret) {
                delete impl;
                return nullptr;
            }
            void *base_addr = impl->fs.rel_to_abs(0);
            impl->metadata = MetadataV1::Open(base_addr, capacity, true);
            if (!impl->metadata) {
                impl->fs.close();
                delete impl;
                return nullptr;
            }
        } else {
            ret = impl->fs.open(path, flags, hint_addr);
            if (!ret) {
                delete impl;
                return nullptr;
            }

            void *base_addr = impl->fs.rel_to_abs(0);
            impl->metadata = MetadataV1::Open(base_addr, 0, false);
            if (!impl->metadata) {
                impl->fs.close();
                delete impl;
                return nullptr;
            }

            impl->capacity = impl->metadata->get_header().capacity;
            if (impl->capacity & kRegionMask) {
                fprintf(stderr, "capacity is not aligned with region granularity\n");
                delete impl->metadata;
                impl->fs.close();
                delete impl;
                return nullptr;
            }

            impl->nr_regions = impl->capacity >> kRegionShift;
            impl->metadata->recover_data();
            impl->has_snapshot = impl->exist_snapshot();
        }

        impl->region_dirty.allocate(impl->nr_regions);
        impl->address_range.first = (uintptr_t) (void *) impl->metadata->get_header().main_data;
        impl->address_range.second = impl->address_range.first + impl->capacity;
        Registry::Get()->do_register(impl);
        impl->protect_regions(0, impl->nr_regions, false);
        impl->verbose = option.verbose_output;
        impl->has_init = true;
        return impl;
    }

    MProtectEngine::MProtectEngine() :
            has_init(false),
            has_snapshot(false),
            next_thread_id(0),
            checkpoint_traffic(0),
            flush_latency(0),
            write_back_latency(0),
            verbose(false) {
        for (uint64_t i = 0; i < kMaxThreads; ++i) {
            flush_regions[i] = new uint64_t[kMaxFlushRegions];
            flush_regions_count[i] = 0;
        }
    }

    MProtectEngine::~MProtectEngine() {
        if (has_init) {
            for (uint64_t i = 0; i < kMaxThreads; ++i) {
                delete[]flush_regions[i];
            }
            delete metadata;
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

    void MProtectEngine::checkpoint(uint64_t nr_threads) {
        uint64_t start_clock, persist_clock, complete_clock;
        int tid = next_thread_id.fetch_add(1, std::memory_order_relaxed);
        bool is_leader = (tid == 0);

        barrier.barrier(nr_threads, tid);
        if (is_leader) {
            start_clock = ReadTSC();
            bool all_empty = true, has_full = false;
            for (size_t i = 0; i < kMaxThreads; ++i) {
                size_t size = flush_regions_count[i];
                if (size != 0) {
                    all_empty = false;
                }
                if (size == kMaxFlushRegions) {
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
            metadata->set_consistent_data(MetadataV1::kMainData);
            protect_regions(0, nr_regions, false);
            persist_clock = ReadTSC();
            latch.latch_add(tid);
        }
        latch.latch_wait(tid);

        uint64_t delta = write_back_parallel(tid, nr_threads);
        checkpoint_traffic.fetch_add(delta, std::memory_order_relaxed);
        barrier.barrier(nr_threads, tid);
        clear_dirty_bits_parallel(tid, nr_threads);

        if (is_leader) {
            metadata->set_consistent_data(MetadataV1::kBackData);
            next_thread_id.store(0, std::memory_order_relaxed);
            complete_clock = ReadTSC();
            flush_latency.fetch_add(persist_clock - start_clock,
                                    std::memory_order_relaxed);
            write_back_latency.fetch_add(complete_clock - persist_clock,
                                         std::memory_order_relaxed);
            for (size_t i = 0; i < kMaxThreads; ++i) {
                flush_regions_count[i] = 0;
            }
            if (unlikely(!has_snapshot)) {
                metadata->set_attributes(MetadataV1::kAttributeHasSnapshot);
                has_snapshot = true;
            }
            latch.latch_add(tid);
        }
        latch.latch_wait(tid);
    }

    bool MProtectEngine::exist_snapshot() {
        return metadata->get_header().attributes & MetadataV1::kAttributeHasSnapshot;
    }

    void *MProtectEngine::get_address(uint64_t offset) {
        uint8_t *base = metadata->get_header().main_data;
        return base + offset;
    }

    size_t MProtectEngine::get_capacity() {
        return nr_regions * kRegionSize;
    }

    uint64_t MProtectEngine::flush_parallel(int tid, int nr_threads) {
        if (flush_mode == FMODE_WBINVD) {
            if (tid == 0) {
                WriteBackAndInvalidate();
            }
        } else {
            size_t id = tid;
            uint8_t *base_address = (uint8_t *) get_address(0);
            while (id < kMaxThreads) {
                auto &bucket = flush_regions[id];
                uint64_t bucket_size = flush_regions_count[id];
                for (uint64_t i = 0; i != bucket_size; ++i) {
                    uint64_t region_id = bucket[i];
                    uint8_t *addr = base_address + (region_id << kRegionShift);
                    FlushRegion(addr, kRegionSize);
                }
                id += nr_threads;
            }
        }
        StoreFence();
        return 0;
    }

    uint64_t MProtectEngine::write_back_parallel(int tid, int nr_threads) {
        uint64_t flush_count = 0;
        if (flush_mode == FMODE_WBINVD) {
            uint64_t region_id = tid;
            while (region_id < nr_regions) {
                if (region_dirty.test(region_id)) {
                    uint8_t *addr = (uint8_t *) get_address(region_id << kRegionShift);
                    NonTemporalCopy256(addr + capacity, addr, kRegionSize);
                    flush_count += kRegionSize;
                }
                region_id += nr_threads;
            }
        } else {
            size_t id = tid;
            uint8_t *base_address = (uint8_t *) get_address(0);
            while (id < kMaxThreads) {
                auto &bucket = flush_regions[id];
                uint64_t bucket_size = flush_regions_count[id];
                for (uint64_t i = 0; i != bucket_size; ++i) {
                    uint64_t region_id = bucket[i];
                    uint8_t *addr = base_address + (region_id << kRegionShift);
                    NonTemporalCopy256(addr + capacity, addr, kRegionSize);
                    flush_count += kRegionSize;
                }
                id += nr_threads;
            }
        }
        StoreFence();
        return flush_count;
    }

    void MProtectEngine::clear_dirty_bits_parallel(int tid, int nr_threads) {
        if (flush_mode == FMODE_WBINVD) {
            uint64_t region_id = tid * AtomicBitSet::kBitWidth;
            while (region_id < nr_regions) {
                region_dirty.clear_all(region_id);
                region_id += nr_threads * AtomicBitSet::kBitWidth;
            }
        } else {
            size_t id = tid;
            while (id < kMaxThreads) {
                auto &bucket = flush_regions[id];
                uint64_t bucket_size = flush_regions_count[id];
                for (uint64_t i = 0; i != bucket_size; ++i) {
                    uint64_t region_id = bucket[i];
                    region_dirty.clear_all(region_id);
                }
                id += nr_threads;
            }
        }
    }

    void MProtectEngine::protect_regions(uint64_t region_id, size_t count, bool writable) {
        void *addr = get_address(region_id << kRegionShift);
        int prot = PROT_READ;
        if (writable) {
            prot |= PROT_WRITE;
        }
        int ret = mprotect(addr, count << kRegionShift, prot);
        if (ret < 0) {
            perror("mprotect");
            exit(EXIT_FAILURE);
        }
    }

    void MProtectEngine::sigfault_handler(void *addr) {
        uint64_t region_id = ((uint64_t) addr - address_range.first) >> kRegionShift;
        region_dirty.set(region_id);
        protect_regions(region_id, 1, true);

        thread_local unsigned int tid = tl_thread_info.get_thread_id();
        auto &bucket = flush_regions[tid];
        auto &bucket_size = flush_regions_count[tid];
        if (likely(bucket_size != kMaxFlushRegions)) {
            bucket[bucket_size] = region_id;
            ++bucket_size;
        }
    }


#ifdef USE_MPI_EXTENSION
    MProtectEngine * MProtectEngine::OpenForMPI(const char *path,
                                                const MemoryPoolOption &option,
                                                MPI_Comm comm) {
        MProtectEngine *impl = new MProtectEngine();
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
            impl->nr_regions = capacity >> kRegionShift;
            if (capacity & kRegionMask) {
                impl->nr_regions++;
            }
            impl->capacity = impl->nr_regions << kRegionShift;
            uint64_t fs_size = MetadataV1::GetLayoutSize(capacity);
            ret = impl->fs.create(path, fs_size, flags, hint_addr);
            if (!ret) {
                delete impl;
                return nullptr;
            }
            void *base_addr = impl->fs.rel_to_abs(0);
            impl->metadata = MetadataV1::Open(base_addr, capacity, true);
            if (!impl->metadata) {
                impl->fs.close();
                delete impl;
                return nullptr;
            }

            impl->region_dirty.allocate(impl->nr_regions);
        } else {
            ret = impl->fs.open(path, flags, hint_addr);
            if (!ret) {
                delete impl;
                return nullptr;
            }

            void *base_addr = impl->fs.rel_to_abs(0);
            impl->metadata = MetadataV1::Open(base_addr, 0, false);
            if (!impl->metadata) {
                delete impl->metadata;
                impl->fs.close();
                delete impl;
                return nullptr;
            }

            impl->capacity = impl->metadata->get_header().capacity;
            if (impl->capacity & kRegionMask) {
                fprintf(stderr, "capacity is not aligned with region granularity\n");
                impl->fs.close();
                delete impl;
                return nullptr;
            }

            impl->nr_regions = impl->capacity >> kRegionShift;
            impl->region_dirty.allocate(impl->nr_regions);
            uint32_t consistent_data = impl->metadata->get_header().consistent_data;
            MPI_Bcast(&consistent_data, 1, MPI_UINT32_T, 0, comm);
            impl->metadata->set_consistent_data(consistent_data);
            impl->metadata->recover_data();
            impl->has_snapshot = impl->exist_snapshot();
        }

        impl->address_range.first = (uintptr_t) (void *) impl->metadata->get_header().main_data;
        impl->address_range.second = impl->address_range.first + impl->capacity;
        Registry::Get()->do_register(impl);
        impl->protect_regions(0, impl->nr_regions, false);
        impl->verbose = option.verbose_output;
        impl->has_init = true;
        return impl;
    }

    void MProtectEngine::checkpoint_for_mpi(uint64_t nr_threads, MPI_Comm comm) {
        uint64_t start_clock, persist_clock, complete_clock;
        int tid = next_thread_id.fetch_add(1, std::memory_order_relaxed);
        bool is_leader = (tid == 0);

        barrier.barrier(nr_threads, tid);
        if (is_leader) {
            start_clock = ReadTSC();
            bool has_full = false;
            for (size_t i = 0; i < kMaxThreads; ++i) {
                size_t size = flush_regions_count[i];
                if (size == kMaxFlushRegions) {
                    has_full = true;
                }
            }
            if (has_full) {
                flush_mode = FMODE_WBINVD;
            } else {
                flush_mode = FMODE_USE_FLUSH_BLOCKS;
            }
            latch.latch_add(tid);
        }
        latch.latch_wait(tid);

        flush_parallel(tid, nr_threads);
        barrier.barrier(nr_threads, tid);

        if (is_leader) {
            MPI_Barrier(comm);
            metadata->set_consistent_data(MetadataV1::kMainData);
            MPI_Barrier(comm);
            protect_regions(0, nr_regions, false);
            persist_clock = ReadTSC();
            latch.latch_add(tid);
        }
        latch.latch_wait(tid);

        uint64_t delta = write_back_parallel(tid, nr_threads);
        checkpoint_traffic.fetch_add(delta, std::memory_order_relaxed);
        barrier.barrier(nr_threads, tid);
        clear_dirty_bits_parallel(tid, nr_threads);

        if (is_leader) {
            MPI_Barrier(comm);
            metadata->set_consistent_data(MetadataV1::kBackData);
            MPI_Barrier(comm);
            next_thread_id.store(0, std::memory_order_relaxed);
            complete_clock = ReadTSC();
            flush_latency.fetch_add(persist_clock - start_clock,
                                    std::memory_order_relaxed);
            write_back_latency.fetch_add(complete_clock - persist_clock,
                                         std::memory_order_relaxed);
            for (size_t i = 0; i < kMaxThreads; ++i) {
                flush_regions_count[i] = 0;
            }
            if (unlikely(!has_snapshot)) {
                metadata->set_attributes(MetadataV1::kAttributeHasSnapshot);
                has_snapshot = true;
            }
            latch.latch_add(tid);
        }
        latch.latch_wait(tid);
    }

#endif // USE_MPI_EXTENSION
}