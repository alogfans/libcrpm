//
// Created by Feng Ren on 2021/1/23.
//

#ifndef LIBCRPM_NVM_INST_ENGINE_H
#define LIBCRPM_NVM_INST_ENGINE_H

#include <set>
#include <mutex>
#include <vector>
#include <thread>
#include <csignal>
#include <condition_variable>

#include "crpm.h"
#include "internal/common.h"
#include "internal/filesystem.h"
#include "internal/checkpoint.h"
#include "internal/engine.h"

namespace crpm {
    class NvmInstEngine : public Engine {
    public:
        class Registry {
        public:
            static Registry *Get() {
                static Registry instance;
                return &instance;
            }

            ~Registry() = default;

            void do_register(NvmInstEngine *engine);

            void do_unregister(NvmInstEngine *engine);

            void hook_routine(const void *addr);

            void hook_routine(const void *addr, size_t len);

            void hook_copy_on_write_routine(const void *addr);

            void hook_copy_on_write_routine(const void *addr, size_t len);

            NvmInstEngine *get_unique_engine() const;

        private:
            Registry();

            Registry(const Registry &);

            Registry &operator=(const Registry &);

            NvmInstEngine *find(const void *addr);

            NvmInstEngine *find_address_space(const void *addr);

        private:
            std::mutex mutex;
            std::set<NvmInstEngine *> engines;
            NvmInstEngine *default_engine;
        };

    public:
        static NvmInstEngine *Open(const char *path,
                                   const MemoryPoolOption &option);

        NvmInstEngine();

        virtual ~NvmInstEngine();

        virtual void checkpoint(uint64_t nr_threads);

        virtual bool exist_snapshot();

        virtual void *get_address(uint64_t offset);

        virtual size_t get_capacity();

        virtual void wait_for_background_task();

        bool has_background_task();

        void hook_routine(const void *addr, size_t len);

        void hook_routine(const void *addr);

        void hook_copy_on_write_routine(const void *addr, size_t len);

        void hook_copy_on_write_routine(const void *addr);

#ifdef USE_MPI_EXTENSION

        static NvmInstEngine *OpenForMPI(const char *path,
                                         const MemoryPoolOption &option,
                                         MPI_Comm comm);

        virtual void checkpoint_for_mpi(uint64_t nr_threads, MPI_Comm comm);

        void commit_layout_state_for_mpi(uint8_t state, MPI_Comm comm);

#endif // USE_MPI_EXTENSION

    private:
        uint64_t write_back_parallel(int tid, int nr_threads);

        void clear_dirty_bits();

        void commit_layout_state(uint8_t state);

        bool create_checkpoint_image(const char *path, size_t capacity, void *hint_addr,
                                     int flags, const MemoryPoolOption &option);

        bool open_checkpoint_image(const char *path, void *hint_addr, int flags);

        void determine_flush_mode();

        uint64_t flush_parallel(int tid, int nr_threads);

        bool lazy_write_back(uint64_t segment_id, bool on_demand = false);

        void allocate_back_segment(uint64_t main_id);

        uint64_t find_back_block(uint64_t block_id, bool &created);

        uint64_t find_back_segment(uint64_t segment_id, bool &created);

        static void WriteBackThreadRoutine(NvmInstEngine *engine);

        inline void advance_next_back_segment() {
            const size_t kNumBackSegments = image->get_nr_back_segments();
            next_back_id = (next_back_id + 1) % kNumBackSegments;
        }

    private:
        bool has_init;
        bool has_snapshot;
        bool verbose;
        FileSystem fs;
        CheckpointImage *image;

        uint64_t nr_blocks;
        uint64_t nr_segments, nr_back_segments;
        uint64_t capacity;
        bool skip_copy_on_write;
        AtomicBitSet segment_dirty;
        AtomicBitSet block_dirty;
        std::atomic<uint64_t> next_thread_id;
        Barrier barrier, latch;
        std::atomic_flag *segment_locks;

        enum CleanerState {
            WB_STARTED, WB_RUNNING, WB_IDLE
        };
        std::atomic<CleanerState> cleaner_state;

        std::thread cleaner;
        volatile bool cleaner_running;
        std::mutex cleaner_mutex;
        std::atomic<bool> checkpoint_in_progress;
        std::condition_variable cleaner_condvar;

        std::atomic<uint64_t> checkpoint_traffic;
        std::atomic<uint64_t> flush_latency;
        std::atomic<uint64_t> write_back_latency;

        volatile uint64_t *flush_blocks[kMaxThreads];
        volatile uint64_t flush_blocks_count[kMaxThreads];

        enum FlushMode {
            FMODE_NO_ACTION, FMODE_USE_FLUSH_BLOCKS, FMODE_WBINVD
        };
        FlushMode flush_mode;

        std::pair<uintptr_t, uintptr_t> address_range;

        uint64_t next_back_id;
        std::atomic_flag back_memory_lock;
    };
}

#endif //LIBCRPM_NVM_INST_ENGINE_H
