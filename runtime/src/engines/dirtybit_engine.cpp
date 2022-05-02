//
// Created by Feng Ren on 2021/1/23.
//

#include <sys/mman.h>
#include "internal/common.h"
#include "internal/engines/dirtybit_engine.h"

#ifdef USE_MPI_EXTENSION
#define USE_HYBRID_MEMORY
#endif

namespace crpm {
    DirtyBitEngine *DirtyBitEngine::Open(const char *path,
                                         const MemoryPoolOption &option) {
        DirtyBitEngine *impl = new DirtyBitEngine();
        bool ret;
        int flags = 0;
        bool create = false;
        void *hint_addr = nullptr;

#ifndef USE_HYBRID_MEMORY
        if (option.fixed_base_address) {
            flags |= MAP_FIXED;
            hint_addr = (void *) option.fixed_base_address;
        }
#endif //USE_HYBRID_MEMORY

        if (option.create) {
            create = (option.truncate || !FileSystem::Exist(path));
        }

        if (create) {
            if (!option.capacity) {
                fprintf(stderr, "use default capacity for pool allocation\n");
            }
            uint64_t capacity = std::max(option.capacity, kMinContainerSize);
            impl->nr_pages = capacity >> kPageShift;
            if (capacity & kPageMask) {
                impl->nr_pages++;
                capacity = impl->nr_pages << kPageShift;
            }
            impl->capacity = capacity;
            uint64_t fs_size = MetadataV1::GetLayoutSize(capacity);
            ret = impl->fs.create(path, fs_size, flags, hint_addr);
            if (!ret) {
                delete impl;
                return nullptr;
            }
            void *base_addr = impl->fs.rel_to_abs(0);
            impl->metadata = MetadataV1::Open(base_addr, capacity, create);
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
            impl->metadata = MetadataV1::Open(base_addr, 0, create);
            if (!impl->metadata) {
                impl->fs.close();
                delete impl;
                return nullptr;
            }

            impl->capacity = impl->metadata->get_header().capacity;
            if (impl->capacity & kPageMask) {
                fprintf(stderr, "capacity is not aligned with page granularity\n");
                delete impl->metadata;
                impl->fs.close();
                delete impl;
                return nullptr;
            }

            impl->nr_pages = impl->capacity >> kPageShift;
            impl->metadata->recover_data();
            impl->has_snapshot = impl->exist_snapshot();
        }

#ifdef USE_HYBRID_MEMORY
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
            impl->prepare_working_memory();
        }
#endif // USE_HYBRID_MEMORY

        impl->address_range.first = (uintptr_t) impl->get_address(0);
        impl->address_range.second = impl->address_range.first + impl->capacity;
        impl->start_dirty_bit_trace();
        impl->reset_page_map();
        impl->verbose = option.verbose_output;
        impl->has_init = true;
        return impl;
    }

    DirtyBitEngine::DirtyBitEngine() :
            has_init(false),
            has_snapshot(false),
            next_thread_id(0),
            checkpoint_traffic(0),
            flush_latency(0),
            write_back_latency(0),
            verbose(false) {}

    DirtyBitEngine::~DirtyBitEngine() {
        if (has_init) {
            stop_dirty_bit_trace();
            delete metadata;
            fs.close();
#ifdef USE_HYBRID_MEMORY
            munmap(working_memory, capacity);
#endif //USE_HYBRID_MEMORY
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

    void DirtyBitEngine::checkpoint(uint64_t nr_threads) {
        uint64_t start_clock, persist_clock, complete_clock;
        int tid = next_thread_id.fetch_add(1, std::memory_order_relaxed);
        bool is_leader = (tid == 0);

        barrier.barrier(nr_threads, tid);
#ifdef USE_HYBRID_MEMORY
        if (is_leader) {
            start_clock = ReadTSC();
            read_page_map();
            latch.latch_add(tid);
        }
        latch.latch_wait(tid);
        apply_dram_to_nvm(tid, nr_threads);
        barrier.barrier(nr_threads, tid);
        if (is_leader) {
            metadata->set_consistent_data(MetadataV1::kMainData);
            persist_clock = ReadTSC();
            latch.latch_add(tid);
        }
        latch.latch_wait(tid);
#else
        if (is_leader) {
            start_clock = ReadTSC();
            read_page_map();
            WriteBackAndInvalidate(); // issue wbinvd
            StoreFence();
            metadata->set_consistent_data(MetadataV1::kMainData);
            persist_clock = ReadTSC();
            latch.latch_add(tid);
        }
        latch.latch_wait(tid);
#endif //USE_HYBRID_MEMORY

        uint64_t delta = write_back_parallel(tid, nr_threads);
        checkpoint_traffic.fetch_add(delta, std::memory_order_relaxed);
        barrier.barrier(nr_threads, tid);

        if (is_leader) {
            metadata->set_consistent_data(MetadataV1::kBackData);
            next_thread_id.store(0, std::memory_order_relaxed);
            reset_page_map();
            complete_clock = ReadTSC();
            flush_latency.fetch_add(persist_clock - start_clock,
                                    std::memory_order_relaxed);
            write_back_latency.fetch_add(complete_clock - persist_clock,
                                         std::memory_order_relaxed);
            if (unlikely(!has_snapshot)) {
                metadata->set_attributes(MetadataV1::kAttributeHasSnapshot);
                has_snapshot = true;
            }
            latch.latch_add(tid);
        }
        latch.latch_wait(tid);
    }

    bool DirtyBitEngine::exist_snapshot() {
        return metadata->get_header().attributes & MetadataV1::kAttributeHasSnapshot;
    }

    void *DirtyBitEngine::get_address(uint64_t offset) {
#ifdef USE_HYBRID_MEMORY
        return (void *) ((uintptr_t) working_memory + offset);
#else
        uint8_t *base = metadata->get_header().main_data;
        return base + offset;
#endif //USE_HYBRID_MEMORY
    }

    size_t DirtyBitEngine::get_capacity() {
        return capacity;
    }

    void DirtyBitEngine::prepare_working_memory() {
        uint8_t *work_base = (uint8_t *) working_memory;
        uint8_t *main_base = metadata->get_header().main_data;
        NonTemporalCopyWithWriteElimination(work_base, main_base, capacity);
    }

    uint64_t DirtyBitEngine::apply_dram_to_nvm(int tid, int nr_threads) {
        uint64_t flush_count = 0;
        uint64_t page_id = tid;
        while (page_id < nr_pages) {
            if (pte_buffer[page_id] & kPTESoftDirtyBit) {
                uint8_t *work_addr = (uint8_t *) working_memory + (page_id << kPageShift);
                uint8_t *main_addr = (uint8_t *) metadata->get_header().main_data
                        + (page_id << kPageShift);
                NonTemporalCopy256(main_addr, work_addr, kPageSize);
                flush_count += kPageSize;
            }
            page_id += nr_threads;
        }
        StoreFence();
        return flush_count;
    }

    uint64_t DirtyBitEngine::write_back_parallel(int tid, int nr_threads) {
        uint64_t flush_count = 0;
        uint64_t page_id = tid;
        while (page_id < nr_pages) {
            if (pte_buffer[page_id] & kPTESoftDirtyBit) {
                uint8_t *addr = (uint8_t *) metadata->get_header().main_data
                                + (page_id << kPageShift);
                NonTemporalCopy256(addr + capacity, addr, kPageSize);
                flush_count += kPageSize;
            }
            page_id += nr_threads;
        }
        StoreFence();
        return flush_count;
    }

    void DirtyBitEngine::start_dirty_bit_trace() {
        const char clear_refs_path[] = "/proc/self/clear_refs";
        clear_refs_fd = open(clear_refs_path, O_WRONLY);
        if (clear_refs_fd < 0) {
            perror("open");
            exit(EXIT_FAILURE);
        }

        const char pagemap_path[] = "/proc/self/pagemap";
        pagemap_fd = open(pagemap_path, O_RDONLY);
        if (pagemap_fd < 0) {
            perror("open");
            exit(EXIT_FAILURE);
        }

        pte_buffer = (uint64_t *) malloc(nr_pages * sizeof(uint64_t));
        if (!pte_buffer) {
            perror("malloc");
            exit(EXIT_FAILURE);
        }
    }

    void DirtyBitEngine::stop_dirty_bit_trace() {
        close(clear_refs_fd);
        close(pagemap_fd);
        free(pte_buffer);
    }

    void DirtyBitEngine::read_page_map() {
        size_t off = sizeof(uint64_t) * (address_range.first >> kPageShift);
        off64_t ret = lseek64(pagemap_fd, off, SEEK_SET);
        if (ret == (off64_t) (-1)) {
            perror("lseek64");
            exit(EXIT_FAILURE);
        }

        ssize_t bytes = read(pagemap_fd, pte_buffer, nr_pages * sizeof(uint64_t));
        if (bytes != nr_pages * sizeof(uint64_t)) {
            perror("read");
            exit(EXIT_FAILURE);
        }
    }

    void DirtyBitEngine::reset_page_map() {
        const char written_char[] = "4";
        size_t ret = write(clear_refs_fd, written_char, sizeof(char));
        if (ret != sizeof(char)) {
            perror("write");
            exit(EXIT_FAILURE);
        }
    }


#ifdef USE_MPI_EXTENSION
    DirtyBitEngine * DirtyBitEngine::OpenForMPI(const char *path,
                                                const MemoryPoolOption &option,
                                                MPI_Comm comm) {
        DirtyBitEngine *impl = new DirtyBitEngine();
        bool ret;
        int flags = 0;
        int create = 0, all_create, comm_size;
        void *hint_addr = nullptr;

#ifndef USE_HYBRID_MEMORY
        if (option.fixed_base_address) {
            flags |= MAP_FIXED;
            hint_addr = (void *) option.fixed_base_address;
        }
#endif //USE_HYBRID_MEMORY

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
            impl->nr_pages = capacity >> kPageShift;
            if (capacity & kPageMask) {
                impl->nr_pages++;
                capacity = impl->nr_pages << kPageShift;
            }
            impl->capacity = capacity;
            uint64_t fs_size = MetadataV1::GetLayoutSize(capacity);
            ret = impl->fs.create(path, fs_size, flags, hint_addr);
            if (!ret) {
                delete impl;
                return nullptr;
            }
            void *base_addr = impl->fs.rel_to_abs(0);
            impl->metadata = MetadataV1::Open(base_addr, capacity, create);
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
            impl->metadata = MetadataV1::Open(base_addr, 0, create);
            if (!impl->metadata) {
                impl->fs.close();
                delete impl;
                return nullptr;
            }

            impl->capacity = impl->metadata->get_header().capacity;
            if (impl->capacity & kPageMask) {
                fprintf(stderr, "capacity is not aligned with page granularity\n");
                delete impl->metadata;
                impl->fs.close();
                delete impl;
                return nullptr;
            }

            impl->nr_pages = impl->capacity >> kPageShift;
            uint32_t consistent_data = impl->metadata->get_header().consistent_data;
            MPI_Bcast(&consistent_data, 1, MPI_UINT32_T, 0, comm);
            impl->metadata->set_consistent_data(consistent_data);
            impl->metadata->recover_data();
            impl->has_snapshot = impl->exist_snapshot();
        }

#ifdef USE_HYBRID_MEMORY
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
            impl->prepare_working_memory();
        }
#endif // USE_HYBRID_MEMORY

        impl->address_range.first = (uintptr_t) impl->get_address(0);
        impl->address_range.second = impl->address_range.first + impl->capacity;
        impl->start_dirty_bit_trace();
        impl->reset_page_map();
        impl->verbose = option.verbose_output;
        impl->has_init = true;
        return impl;
    }

    void DirtyBitEngine::checkpoint_for_mpi(uint64_t nr_threads, MPI_Comm comm) {
        uint64_t start_clock, persist_clock, complete_clock;
        int tid = next_thread_id.fetch_add(1, std::memory_order_relaxed);
        bool is_leader = (tid == 0);

        barrier.barrier(nr_threads, tid);
#ifdef USE_HYBRID_MEMORY
        if (is_leader) {
            start_clock = ReadTSC();
            read_page_map();
            latch.latch_add(tid);
        }
        latch.latch_wait(tid);
        apply_dram_to_nvm(tid, nr_threads);
        barrier.barrier(nr_threads, tid);
        if (is_leader) {
            MPI_Barrier(comm);
            metadata->set_consistent_data(MetadataV1::kMainData);
            MPI_Barrier(comm);
            persist_clock = ReadTSC();
            latch.latch_add(tid);
        }
        latch.latch_wait(tid);
#else
        if (is_leader) {
            start_clock = ReadTSC();
            read_page_map();
            WriteBackAndInvalidate(); // issue wbinvd
            StoreFence();
            MPI_Barrier(comm);
            metadata->set_consistent_data(MetadataV1::kMainData);
            MPI_Barrier(comm);
            persist_clock = ReadTSC();
            latch.latch_add(tid);
        }
        latch.latch_wait(tid);
#endif //USE_HYBRID_MEMORY

        uint64_t delta = write_back_parallel(tid, nr_threads);
        checkpoint_traffic.fetch_add(delta, std::memory_order_relaxed);
        barrier.barrier(nr_threads, tid);

        if (is_leader) {
            MPI_Barrier(comm);
            metadata->set_consistent_data(MetadataV1::kBackData);
            MPI_Barrier(comm);
            next_thread_id.store(0, std::memory_order_relaxed);
            reset_page_map();
            complete_clock = ReadTSC();
            flush_latency.fetch_add(persist_clock - start_clock,
                                    std::memory_order_relaxed);
            write_back_latency.fetch_add(complete_clock - persist_clock,
                                         std::memory_order_relaxed);
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