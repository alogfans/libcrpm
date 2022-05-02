//
// Created by Feng Ren on 2021/1/23.
//

#ifndef LIBCRPM_DIRTYBIT_ENGINE_H
#define LIBCRPM_DIRTYBIT_ENGINE_H

#include <set>
#include <mutex>
#include <vector>
#include <csignal>

#include "crpm.h"
#include "internal/metadata.h"
#include "internal/common.h"
#include "internal/filesystem.h"
#include "internal/engine.h"

namespace crpm {
    class DirtyBitEngine : public Engine {
    public:
        static DirtyBitEngine *Open(const char *path,
                                    const MemoryPoolOption &option);

        DirtyBitEngine();

        virtual ~DirtyBitEngine();

        virtual void checkpoint(uint64_t nr_threads);

        virtual bool exist_snapshot();

        virtual void *get_address(uint64_t offset);

        virtual size_t get_capacity();

#ifdef USE_MPI_EXTENSION

        static DirtyBitEngine *OpenForMPI(const char *path,
                                          const MemoryPoolOption &option,
                                          MPI_Comm comm);

        virtual void checkpoint_for_mpi(uint64_t nr_threads, MPI_Comm comm);

#endif // USE_MPI_EXTENSION

    private:
        uint64_t write_back_parallel(int tid, int nr_threads);

        uint64_t apply_dram_to_nvm(int tid, int nr_threads);

        void prepare_working_memory();

        void start_dirty_bit_trace();

        void stop_dirty_bit_trace();

        void read_page_map();

        void reset_page_map();

    private:
        bool has_init;
        bool has_snapshot;
        bool verbose;
        FileSystem fs;
        MetadataV1 *metadata;
        void *working_memory;

        uint64_t nr_pages;
        uint64_t capacity;
        std::atomic<uint64_t> next_thread_id;
        Barrier barrier, latch;

        std::atomic<uint64_t> checkpoint_traffic;
        std::atomic<uint64_t> flush_latency;
        std::atomic<uint64_t> write_back_latency;

        int clear_refs_fd, pagemap_fd;
        uint64_t *pte_buffer;

        std::pair<uintptr_t, uintptr_t> address_range;
    };
}

#endif //LIBCRPM_DIRTYBIT_ENGINE_H
