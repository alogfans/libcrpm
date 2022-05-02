//
// Created by Feng Ren on 2021/1/24.
//

#include <cstdlib>
#include <cassert>
#include <pthread.h>
#include <cstring>
#include <atomic>
#include <fstream>
#include <iostream>
#include <numa.h>
#include <sys/stat.h>

#include "bench.h"

// #define SFENCE_STAT

namespace crpm {
    Benchmark *Benchmark::instance = NULL;

    Benchmark::Benchmark(const BenchmarkOption &option_) : option(option_) {
        assert(!instance);
        assert(option_.threads <= kMaxThreads);
        instance = this;
        pthread_barrier_init(&barrier, nullptr, option.threads);

        memset(&start_clock, 0, sizeof(start_clock));
        memset(&stop_clock, 0, sizeof(stop_clock));

        uint64_t operations = load(option.data_path, execute_trace);
        uint64_t div = operations / option.threads;
        uint64_t rem = operations % option.threads;
        if (rem) {
            for (int i = 0; i < option.threads; i++) {
                execute_trace[i].resize(div);
            }
        }
        pool = MemoryPool::Open(option.memory_pool_path.c_str(),
                                option.memory_pool_option);
        if (!pool) {
            fprintf(stderr, "unable to open a memory pool\n");
            exit(EXIT_FAILURE);
        }
    }

    Benchmark::~Benchmark() {
        for (int i = 0; i < kMaxThreads; ++i) {
            execute_trace[i].clear();
        }
        pthread_barrier_destroy(&barrier);
        instance = NULL;
        delete pool;
    }

    void Benchmark::reopen() {
        delete pool;
        option.memory_pool_option.create = false;
        pool = MemoryPool::Open(option.memory_pool_path.c_str(),
                                option.memory_pool_option);
        if (!pool) {
            fprintf(stderr, "unable to open a memory pool\n");
            exit(EXIT_FAILURE);
        }
    }

    uint64_t Benchmark::load(const std::string &path, std::vector<Operation> *trace) {
        struct stat st_buf;
        if (stat(path.c_str(), &st_buf)) {
            perror("stat");
            return 0;
        }
        uint64_t ops = st_buf.st_size / sizeof(Operation);
        FILE *fin = fopen(path.c_str(), "rb");
        if (!fin) {
            perror("fopen");
            return 0;
        }

        uint64_t ops_per_thread = ops / option.threads;
        uint64_t ops_remain = ops;
        if (ops % option.threads) {
            ops_per_thread++;
        }

        for (int i = 0; i < option.threads; i++) {
            uint64_t ops_read = std::min(ops_per_thread, ops_remain);
            trace[i].resize(ops_read);
            if (fread(&trace[i][0], sizeof(Operation), ops_read, fin) != ops_read) {
                perror("fread");
            }
            ops_remain -= ops_read;
        }
        fclose(fin);
        return ops;
    }

    void Benchmark::run() {
        unsigned int nr_threads = option.threads;
        option.operations = 0;
        pthread_t *threads = (pthread_t *) calloc(nr_threads, sizeof(pthread_t));
        for (int i = 1; i < nr_threads; i++) {
            threads[i] = i;
            pthread_create(&threads[i], NULL, worker, (void *) (uintptr_t) i);
        }
        worker((void *) 0);
        for (int i = 1; i < nr_threads; i++) {
            pthread_join(threads[i], NULL);
        }
        free(threads);
    }

    extern uint64_t sfence_cnt;

    void *Benchmark::worker(void *arg) {
        uint64_t id = (uint64_t) arg;
        auto self = Benchmark::GetInstance();
        BindThread(pthread_self(), id);
        self->setup(id);
        if (id == 0) {
            self->pool->wait_for_background_task();
#ifdef SFENCE_STAT
            sfence_cnt = 0;
#endif
        }
        pthread_barrier_wait(&self->barrier);
        if (id == 0) {
            clock_gettime(CLOCK_REALTIME, &self->start_clock);
        }
        uint64_t local_operations = self->worker(id);
        pthread_barrier_wait(&self->barrier);
        if (id == 0) {
            clock_gettime(CLOCK_REALTIME, &self->stop_clock);
#ifdef SFENCE_STAT
            std::cout << sfence_cnt << std::endl;
#endif
        }
        self->teardown(id);
        __sync_fetch_and_add(&self->option.operations, local_operations);
        return NULL;
    }

    uint64_t Benchmark::get_latency() {
        uint64_t total = (stop_clock.tv_sec - start_clock.tv_sec) * 1E9;
        total += (stop_clock.tv_nsec - start_clock.tv_nsec);
        return total;
    }

    uint64_t Benchmark::get_throughput() {
        uint64_t totalOps = option.operations;
        uint64_t execTime = get_latency();
        return execTime > option.operations ? (totalOps * 1E9) / execTime : 0;
    }

    void Benchmark::report() {
        std::cout << option.memory_pool_option.engine_name
                  << ","
                  << option.benchmark << ","
                  << option.threads << ","
                  << option.value_length << ","
                  << option.data_path << ","
                  << option.interval << ","
                  << get_latency() << ","
                  << get_throughput() << std::endl;
    }

    static unsigned long g_bind_preference[kMaxThreads];
    static size_t g_cpus = 0;

    void ComputeBindPreferences() {
        assert(numa_available() == 0);
        int configured_nodes = numa_num_configured_nodes();
        int configured_cpus = numa_num_configured_cpus();
        bitmask *mask = numa_bitmask_alloc(configured_cpus);
        for (int node = 0; node < configured_nodes; node++) {
            int rc = numa_node_to_cpus(node, mask);
            if (rc) {
                exit(EXIT_FAILURE);
            }
            for (int i = 0; i < configured_cpus; i++) {
                if (numa_bitmask_isbitset(mask, i)) {
                    g_bind_preference[g_cpus] = i;
                    g_cpus++;
                }
            }
        }
        assert(g_cpus == configured_cpus);
        numa_set_preferred(0);
    }

    void BindThread(pthread_t thread, uint64_t logical_id) {
        cpu_set_t cpuset;
        CPU_ZERO(&cpuset);
        CPU_SET(g_bind_preference[logical_id % g_cpus], &cpuset);
        pthread_setaffinity_np(thread, sizeof(cpu_set_t), &cpuset);
    }
}