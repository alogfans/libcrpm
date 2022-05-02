#include <iostream>
#include <getopt.h>
#include <cassert>

#include "apps/stl_map.h"
#include "apps/stl_unordered_map.h"
#include "apps/consistency_check.h"

using namespace crpm;

void parseCmdline(int argc, char **argv, BenchmarkOption &conf) {
    static struct option long_options[] = {
            {"data-path",       required_argument, 0, 'd'},
            {"threads",         required_argument, 0, 't'},
            {"populated-keys",  required_argument, 0, 'p'},
            {"hashed-key",      no_argument,       0, 'H'},
            {"interval",        required_argument, 0, 'i'},
            {"benchmark",       required_argument, 0, 'b'},
            {"help",            no_argument,       0, 'h'},
            {"verbose",         no_argument,       0, 'v'},
            {"memory-pool-path", required_argument, 0, 'm'},
            {"capacity",        required_argument, 0, 'c'},
            {"allocator",       required_argument, 0, 'a'},
            {"engine",          required_argument, 0, 'e'},
            {0, 0, 0, 0}
    };

    while (true) {
        int option_index = 0;
        int c = getopt_long(argc, argv, "d:t:p:Hi:b:hvm:c:a:e:",
                            long_options, &option_index);
        if (c == -1)
            break;
        switch (c) {
            case 'd':
                conf.data_path = optarg;
                break;
            case 't':
                conf.threads = strtol(optarg, NULL, 10);
                break;
            case 'p':
                conf.populated_keys = strtol(optarg, NULL, 10);
                break;
            case 'H':
                conf.use_hashed_key = true;
                break;
            case 'i':
                conf.interval = strtol(optarg, NULL, 10);
                break;
            case 'b':
                conf.benchmark = optarg;
                break;
            case 'v':
                conf.memory_pool_option.verbose_output = true;
                break;
            case 'm':
                conf.memory_pool_path = optarg;
                break;
            case 'c':
                conf.memory_pool_option.capacity =
                        (1ULL << 20ULL) * strtol(optarg, NULL, 10);
                break;
            case 'a':
                conf.memory_pool_option.allocator_name = optarg;
                break;
            case 'e':
                conf.memory_pool_option.engine_name = optarg;
                break;
            case 'h':
            case '?':
                fprintf(stderr, "Usage: %s [arguments]\n", argv[0]);
                fprintf(stderr, "  --data-path -d: Path of the workload file\n");
                fprintf(stderr, "  --threads -t: Count of working threads\n");
                fprintf(stderr, "  --populated-keys -p: Count of keys to be populated before execution\n");
                fprintf(stderr, "  --hashed-key -H: Hash populated keys\n");
                fprintf(stderr, "  --interval -i: Checkpoint interval\n");
                fprintf(stderr, "  --benchmark -b: Benchmark name\n");
                fprintf(stderr, "  --verbose -v: Show verbose information\n");
                fprintf(stderr, "  --memory-pool-path -m: Path of the memory pool file\n");
                fprintf(stderr, "  --capacity -c: Capacity of the memory pool in MiB\n");
                fprintf(stderr, "  --allocator -a: Name of used allocator\n");
                fprintf(stderr, "  --engine -e: Name of used engine\n");
                fprintf(stderr, "  --help -h: This help message\n");
                exit(EXIT_SUCCESS);
            default:
                fprintf(stderr, "Unknown arguments %s\n", optarg);
                exit(EXIT_FAILURE);
        }
    }
}

template<size_t kBytes = 8>
struct FixedWidthVariable {
    static_assert(kBytes % sizeof(uint64_t) == 0, "wrong template argument");
    uint64_t content[kBytes / sizeof(uint64_t)];

    FixedWidthVariable() {
        for (size_t i = 0; i < kBytes / sizeof(uint64_t); i++) {
            content[i] = i;
        }
    }

    FixedWidthVariable &operator=(const FixedWidthVariable &RHS) {
        for (size_t i = 0; i < kBytes / sizeof(uint64_t); i++) {
            content[i] = RHS.content[i];
        }
        return *this;
    }

    void inc() {
        content[0]++;
    }
};

#ifndef VALUE_WIDTH
#define VALUE_WIDTH (8)
#endif

int main(int argc, char **argv) {
    using ValueType = FixedWidthVariable<8>;
    ComputeBindPreferences();
    BindThread(pthread_self(), 0);

    BenchmarkOption conf;
    conf.threads = 1;
    conf.interval = 0;
    conf.populated_keys = 0;
    conf.use_hashed_key = false;
    conf.value_length = sizeof(ValueType);
    conf.memory_pool_path = kDefaultMappingPath;
    conf.memory_pool_option.create = true;
    conf.memory_pool_option.truncate = true;
    conf.memory_pool_option.capacity = 1ULL << 33ULL; // 8 GiB
    parseCmdline(argc, argv, conf);

    Benchmark *bench;
    if (conf.benchmark == "stl-map") {
        bench = new STLMapBenchmark<ValueType>(conf);
    } else if (conf.benchmark == "stl-unordered-map") {
        bench = new STLUnorderedMapBenchmark<ValueType>(conf);
    } else if (conf.benchmark == "consistency-check") {
        bench = new ConsistencyChecker(conf);
    } else {
        assert(0 && "--benchmark: unknown benchmark");
        exit(EXIT_FAILURE);
    }

    bench->run();
    bench->report();
    delete bench;
    return 0;
}