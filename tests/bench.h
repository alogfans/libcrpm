//
// Created by Feng Ren on 2021/1/24.
//

#ifndef LIBCRPM_BENCH_H
#define LIBCRPM_BENCH_H

#include <cstdint>
#include <vector>
#include <string>
#include <sys/time.h>

#include "crpm.h"

namespace crpm {
    const static size_t kMaxThreads = 128;
    const static char *kDefaultMappingPath = "/mnt/pmem0/renfeng/crpm";

    struct BenchmarkOption {
        unsigned int threads;
        unsigned int operations;
        unsigned int interval;
        size_t value_length;
        std::string data_path;
        std::string benchmark;
        std::string memory_pool_path;
        MemoryPoolOption memory_pool_option;
        uint64_t populated_keys;
        bool use_hashed_key;
    };

    enum {
        TYPE_INSERT, TYPE_READ, TYPE_UPDATE, TYPE_SCAN, TYPE_RMW
    };

    struct Operation {
        uint32_t type;
        uint32_t scan_length;
        uint64_t key;
    };

    const uint64_t kFNVOffsetBasis64 = 0xCBF29CE484222325;
    const uint64_t kFNVPrime64 = 1099511628211;

    static inline uint64_t FNVHash64(uint64_t val) {
        uint64_t hash = kFNVOffsetBasis64;

        for (int i = 0; i < 8; i++) {
            uint64_t octet = val & 0x00ff;
            val = val >> 8;

            hash = hash ^ octet;
            hash = hash * kFNVPrime64;
        }
        return hash;
    }

    class Benchmark {
    public:
        Benchmark(const BenchmarkOption &option_);

        virtual ~Benchmark();

        static Benchmark *GetInstance() {
            return instance;
        }

        void run();

        static void *worker(void *arg);

        uint64_t get_latency();

        uint64_t get_throughput();

        void report();

    protected:
        virtual void setup(unsigned int id) = 0;

        virtual void teardown(unsigned int id) = 0;

        virtual uint64_t worker(unsigned int id) = 0;

        void reopen();

    private:
        uint64_t load(const std::string &path, std::vector<Operation> *trace);

    protected:
        static Benchmark *instance;

        BenchmarkOption option;
        MemoryPool *pool;
        std::vector<Operation> execute_trace[kMaxThreads];
        pthread_barrier_t barrier;
        struct timespec start_clock, stop_clock;
    };

    void ComputeBindPreferences();

    void BindThread(pthread_t thread, uint64_t logical_id);

    static inline uint64_t GetCurrentMillisecond() {
        struct timeval tv;
        gettimeofday(&tv, nullptr);
        return tv.tv_sec * 1000 + tv.tv_usec / 1000;
    }

    static inline void RegisterGlobalMemoryPool(MemoryPool *pool) {
        pool->set_default_pool();
    }

    static inline void UnregisterGlobalMemoryPool() {
        // Do nothing
    }
}

#endif //LIBCRPM_BENCH_H
