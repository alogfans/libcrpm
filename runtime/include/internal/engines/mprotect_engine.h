//
// Created by Feng Ren on 2021/1/23.
//

#ifndef LIBCRPM_MPROTECT_ENGINE_H
#define LIBCRPM_MPROTECT_ENGINE_H

#include <set>
#include <mutex>
#include <vector>
#include <csignal>

#include "crpm.h"
#include "internal/common.h"
#include "internal/metadata.h"
#include "internal/filesystem.h"
#include "internal/engine.h"

namespace crpm {
    class MProtectEngine : public Engine {
    public:
        class Registry {
        public:
            static Registry *Get() {
                static Registry instance;
                return &instance;
            }

            ~Registry() = default;

            void do_register(MProtectEngine *engine);

            void do_unregister(MProtectEngine *engine);

        private:
            Registry();

            Registry(const Registry &);

            Registry &operator=(const Registry &);

            MProtectEngine *find(const void *addr);

            static void SegfaultHandlerProc(int sig, siginfo_t *info, void *ucontext);

        private:
            std::mutex mutex;
            std::set<MProtectEngine *> engines;
            MProtectEngine *default_engine;
        };

    public:
        static MProtectEngine *Open(const char *path,
                                    const MemoryPoolOption &option);

        MProtectEngine();

        virtual ~MProtectEngine();

        virtual void checkpoint(uint64_t nr_threads);

        virtual bool exist_snapshot();

        virtual void *get_address(uint64_t offset);

        virtual size_t get_capacity();

        virtual void wait_for_background_task() {
            checkpoint_traffic = 0;
            flush_latency = 0;
            write_back_latency = 0;
        }

        void sigfault_handler(void *addr);

#ifdef USE_MPI_EXTENSION

        static MProtectEngine *OpenForMPI(const char *path,
                                          const MemoryPoolOption &option,
                                          MPI_Comm comm);

        virtual void checkpoint_for_mpi(uint64_t nr_threads, MPI_Comm comm);

#endif // USE_MPI_EXTENSION

    private:
        uint64_t flush_parallel(int tid, int nr_threads);

        uint64_t write_back_parallel(int tid, int nr_threads);

        void clear_dirty_bits_parallel(int tid, int nr_threads);

        void protect_regions(uint64_t region_id, size_t count, bool writable);

    private:
        bool has_init;
        bool has_snapshot;
        bool verbose;
        FileSystem fs;
        MetadataV1 *metadata;

        uint64_t nr_regions;
        uint64_t capacity;
        AtomicBitSet region_dirty;
        std::atomic<uint64_t> next_thread_id;
        Barrier barrier, latch;

        std::atomic<uint64_t> checkpoint_traffic;
        std::atomic<uint64_t> flush_latency;
        std::atomic<uint64_t> write_back_latency;

        uint64_t *flush_regions[kMaxThreads];
        uint64_t flush_regions_count[kMaxThreads];

        enum FlushMode {
            FMODE_NO_ACTION, FMODE_USE_FLUSH_BLOCKS, FMODE_WBINVD
        };
        FlushMode flush_mode;

        std::pair<uintptr_t, uintptr_t> address_range;
    };
}

#endif //LIBCRPM_MPROTECT_ENGINE_H
