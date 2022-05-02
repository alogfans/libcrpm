//
// Created by Feng Ren on 2021/1/23.
//

#ifndef LIBCRPM_HYBRID_INST_ENGINE_H
#define LIBCRPM_HYBRID_INST_ENGINE_H

#include <set>
#include <mutex>
#include <vector>
#include <thread>
#include <csignal>

#include "crpm.h"
#include "internal/common.h"
#include "internal/filesystem.h"
#include "internal/checkpoint.h"
#include "internal/engine.h"

namespace crpm {
    class HybridInstEngine : public Engine {
    public:
        class Registry {
        public:
            static Registry *Get() {
                static Registry instance;
                return &instance;
            }

            ~Registry() = default;

            void do_register(HybridInstEngine *engine);

            void do_unregister(HybridInstEngine *engine);

            void hook_routine(const void *addr);

            void hook_routine(const void *addr, size_t len);

            HybridInstEngine *get_engine() const;

        private:
            Registry();

            Registry(const Registry &);

            Registry &operator=(const Registry &);

            HybridInstEngine *find(const void *addr);

        private:
            std::mutex mutex;
            std::set<HybridInstEngine *> engines;
            HybridInstEngine *default_engine;
        };

    public:
        static HybridInstEngine *Open(const char *path,
                                      const MemoryPoolOption &option);

        HybridInstEngine();

        virtual ~HybridInstEngine();

        virtual void checkpoint(uint64_t nr_threads);

        virtual bool exist_snapshot();

        virtual void *get_address(uint64_t offset);

        virtual size_t get_capacity();

        virtual void wait_for_background_task();

        void hook_routine(const void *addr, size_t len);

        void hook_routine(const void *addr);

        void clear_dirty_bits_last_epoch();

        void commit_layout_state(uint8_t state);

#ifdef USE_MPI_EXTENSION

        static HybridInstEngine *OpenForMPI(const char *path,
                                            const MemoryPoolOption &option,
                                            MPI_Comm comm);

        virtual void checkpoint_for_mpi(uint64_t nr_threads, MPI_Comm comm);

        void commit_layout_state_for_mpi(uint8_t state, MPI_Comm comm);

#endif // USE_MPI_EXTENSION

    private:
        uint64_t apply_dram_to_back_parallel(int tid, int nr_threads);

        uint64_t apply_dram_to_main_parallel(int tid, int nr_threads);

        void apply_nvm_to_nvm_parallel(int tid, int nr_threads);

        void allocate_back_segment(uint64_t segment_id);

        uint64_t find_back_block(uint64_t block_id, bool &created);

        uint64_t find_back_segment(uint64_t segment_id, bool &created);

        void prepare_working_memory();

        void determine_flush_mode();

        bool create_checkpoint_image(const char *path, size_t capacity, void *hint_addr, int flags,
                                     const MemoryPoolOption &option);

        bool open_checkpoint_image(const char *path, void *hint_addr, int flags);

        static void WriteBackThreadRoutine(HybridInstEngine *engine);

        inline void advance_next_back_segment() {
            const size_t kNumBackSegments = image->get_nr_back_segments();
            next_back_segment = (next_back_segment + 1) % kNumBackSegments;
        }

    private:
        bool has_init;
        bool has_snapshot;
        bool verbose;
        FileSystem fs;
        CheckpointImage *image;
        void *working_memory; // buffer in DRAM

        uint64_t nr_blocks;
        uint64_t nr_segments;
        uint64_t capacity;
        AtomicBitSet segment_dirty[2];
        AtomicBitSet block_dirty[2];
        std::atomic<uint64_t> next_thread_id;
        Barrier barrier, latch;
        volatile bool write_back_thread_running;
        enum WriteBackState {
            WB_STARTING, WB_RUNNING, WB_EXITING, WB_IDLE
        };
        std::atomic<WriteBackState> write_back_state;
        std::atomic_flag write_back_thread_lock;
        std::thread write_back_thread;
        std::atomic<bool> checkpoint_in_progress;

        std::atomic<uint64_t> checkpoint_traffic;
        std::atomic<uint64_t> flush_latency;
        std::atomic<uint64_t> write_back_latency;

        volatile uint64_t *flush_blocks[kMaxThreads];
        volatile uint64_t flush_blocks_count[kMaxThreads];

        enum FlushMode {
            FMODE_NO_ACTION, FMODE_USE_FLUSH_BLOCKS, FMODE_WBINVD
        };
        FlushMode flush_mode, prev_flush_mode;

        std::pair<uintptr_t, uintptr_t> address_range;

        uint64_t next_back_segment;
        std::atomic_flag back_memory_lock;
        uint64_t epoch;
    };
}

#endif //LIBCRPM_HYBRID_INST_ENGINE_H
