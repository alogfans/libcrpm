//
// Created by Feng Ren on 2021/1/23.
//

#ifndef LIBCRPM_UNDOLOG_ENGINE_H
#define LIBCRPM_UNDOLOG_ENGINE_H

#include <set>
#include <mutex>
#include <vector>
#include <csignal>

#include "crpm.h"
#include "internal/common.h"
#include "internal/filesystem.h"
#include "internal/engine.h"

namespace crpm {
    class UndoLogEngine : public Engine {
    public:
        class Registry {
        public:
            static Registry *Get() {
                static Registry instance;
                return &instance;
            }

            ~Registry() = default;

            void do_register(UndoLogEngine *engine);

            void do_unregister(UndoLogEngine *engine);

            void hook_routine(const void *addr);

            void hook_routine(const void *addr, size_t len);

            UndoLogEngine *get_unique_engine() const;

        private:
            Registry();

            Registry(const Registry &);

            Registry &operator=(const Registry &);

            UndoLogEngine *find(const void *addr);

        private:
            std::mutex mutex;
            std::set<UndoLogEngine *> engines;
            UndoLogEngine *default_engine;
        };

    public:
        static UndoLogEngine *Open(const char *path,
                                   const MemoryPoolOption &option);

        UndoLogEngine();

        virtual ~UndoLogEngine();

        virtual void checkpoint(uint64_t nr_threads);

        virtual bool exist_snapshot();

        virtual void *get_address(uint64_t offset);

        virtual size_t get_capacity();

        void hook_routine(const void *addr, size_t len);

        void hook_routine(const void *addr);

    private:
        uint64_t flush_parallel(int tid, int nr_threads);

        void clear_dirty_bits_parallel(int tid, int nr_threads);

        void recover();

    private:
        const static uint32_t kHeapHeaderMagic = 0x6f6f0404;
        const static uint32_t kLogEntryMagic = 0x7a7a2020;
        static const uint32_t kAttributeHasSnapshot = 0x10;

        struct LogEntry {
            uint64_t magic;
            uint64_t offset;
            uint64_t size;
            uint64_t padding[5];
            uint8_t payload[0];
        };

        struct HeapHeader {
            uint32_t magic;
            uint32_t attributes;
            uint64_t nr_blocks;
            uint64_t log_capacity;
            volatile uint64_t log_head;
            uint64_t log_offset;
            uint64_t data_offset;
            uint64_t padding[2];
        };

    private:
        bool has_init;
        bool has_snapshot;
        bool verbose;
        FileSystem fs;

        uint64_t nr_blocks;
        AtomicBitSet block_dirty;
        std::atomic<uint64_t> next_thread_id;
        Barrier barrier, latch;

        std::atomic<uint64_t> checkpoint_traffic;
        std::atomic<uint64_t> flush_latency;
        std::atomic<uint64_t> write_back_latency;

        HeapHeader *header;
        std::atomic_flag *segment_locks;
        std::atomic<uint64_t> log_head;

        volatile uint64_t *flush_blocks[kMaxThreads];
        volatile uint64_t flush_blocks_count[kMaxThreads];

        enum FlushMode {
            FMODE_NO_ACTION, FMODE_USE_FLUSH_BLOCKS, FMODE_WBINVD
        };
        FlushMode flush_mode;

        std::pair<uintptr_t, uintptr_t> address_range;
    };
}

#endif //LIBCRPM_UNDOLOG_ENGINE_H
