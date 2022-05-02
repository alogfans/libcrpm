//
// Created by Feng Ren on 2021/1/23.
//

#ifndef LIBCRPM_LMC_ENGINE_H
#define LIBCRPM_LMC_ENGINE_H

#include <set>
#include <mutex>
#include <vector>
#include <csignal>

#include "crpm.h"
#include "internal/common.h"
#include "internal/filesystem.h"
#include "internal/engine.h"

namespace crpm {
    class LmcEngine : public Engine {
    public:
        class Registry {
        public:
            static Registry *Get() {
                static Registry instance;
                return &instance;
            }

            ~Registry() = default;

            void do_register(LmcEngine *engine);

            void do_unregister(LmcEngine *engine);

            void hook_routine(const void *addr);

            void hook_routine(const void *addr, size_t len);

            LmcEngine *get_unique_engine() const;

        private:
            Registry();

            Registry(const Registry &);

            Registry &operator=(const Registry &);

            LmcEngine *find(const void *addr);

        private:
            std::mutex mutex;
            std::set<LmcEngine *> engines;
            LmcEngine *default_engine;
        };

    public:
        static LmcEngine *Open(const char *path,
                               const MemoryPoolOption &option);

        LmcEngine();

        virtual ~LmcEngine();

        virtual void checkpoint(uint64_t nr_threads);

        virtual bool exist_snapshot();

        virtual void *get_address(uint64_t offset);

        virtual size_t get_capacity();

        void hook_routine(const void *addr, size_t len);

        void hook_routine(const void *addr);

    private:
        uint64_t flush_parallel(int tid, int nr_threads);

        void advance_current_epoch();

        void recover();

    private:
        const static uint32_t kHeapHeaderMagic = 0x6f6f0303;
        static const uint32_t kAttributeHasSnapshot = 0x10;

        struct HeapHeader {
            uint32_t magic;
            uint32_t attributes;
            uint64_t nr_blocks;
            uint64_t working_state_offset;
            uint64_t shadow_state_offset;
            uint8_t current_epoch;
            uint8_t padding[27];
            uint8_t tagmap[0];
        };

        static_assert(sizeof(HeapHeader) == 64, "wrong heap header size");

        enum FlushMode {
            FMODE_NO_ACTION, FMODE_USE_FLUSH_BLOCKS, FMODE_WBINVD
        };

    private:
        bool has_init;
        bool has_snapshot;
        bool verbose;
        FileSystem fs;

        uint64_t nr_blocks;
        std::atomic<uint64_t> next_thread_id;
        Barrier barrier, latch;

        std::atomic<uint64_t> checkpoint_traffic;
        std::atomic<uint64_t> flush_latency;
        std::atomic<uint64_t> write_back_latency;

        HeapHeader *header;
        std::atomic_flag *segment_locks;

        volatile uint64_t *flush_blocks[kMaxThreads];
        volatile uint64_t flush_blocks_count[kMaxThreads];

        FlushMode flush_mode;
        std::pair<uintptr_t, uintptr_t> address_range;
    };
}

#endif //LIBCRPM_LMC_ENGINE_H
