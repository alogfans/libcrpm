//
// Created by Feng Ren on 2021/2/9.
//

#ifndef LIBCRPM_CONSISTENCY_CHECK_H
#define LIBCRPM_CONSISTENCY_CHECK_H

#include <cassert>
#include <unordered_map>
#include <cstring>
#include <random>

#include "../bench.h"

namespace crpm {
    class ConsistencyChecker : public Benchmark {
        const static uint64_t kSegmentBytes = 2ull << 20;
        const static uint64_t kAllocatedBytes = 128ull << 20;
        const static uint64_t kNumSegments = kAllocatedBytes / kSegmentBytes;

        uint8_t *target;
        uint8_t *snapshot;
        uint64_t seed;

    public:
        ConsistencyChecker(const BenchmarkOption &option) : Benchmark(option) {
            target = (uint8_t *) pool->pmalloc(kAllocatedBytes);
            pool->set_root(0, target);
            snapshot = (uint8_t *) malloc(kAllocatedBytes);
            seed = time(NULL);
        }

        virtual ~ConsistencyChecker() {
            free(snapshot);
            pool->pfree(target);
        }

    protected:
        virtual void setup(unsigned int id) {
            if (id == 0) {
                for (int offset = 0; offset < kAllocatedBytes; offset += kSegmentBytes) {
                    memset(target + offset, 0, kSegmentBytes);
                    memset(snapshot + offset, 0, kSegmentBytes);
                    pool->checkpoint(1);
                }
            }
        }

        virtual void teardown(unsigned int id) {}

        virtual uint64_t worker(unsigned int id) {
            const size_t kTotalCheckpoints = 5000;
            const size_t kOperationsPerCheckpoint = 50;

            std::uniform_int_distribution<int> percentage_distribution(0, 99);
            std::uniform_int_distribution<int> offset_distribution(0, kSegmentBytes - 1);
            std::uniform_int_distribution<int> segment_distribution(0, kNumSegments - 1);
            std::uniform_int_distribution<uint8_t> data_distribution;
            std::mt19937 shared_generator(seed + id);
            std::mt19937 uniform_generator(seed);

            uint64_t segment[4];
            for (int i = 0; i < 4; ++i) {
                segment[i] = segment_distribution(uniform_generator) * kSegmentBytes;
            }

            for (int step = 1; step <= kTotalCheckpoints; ++step) {
                for (int op = 0; op < kOperationsPerCheckpoint; ++op) {
                    uint8_t offset = offset_distribution(shared_generator);
                    uint8_t data = data_distribution(shared_generator);
                    target[segment[op % 4] + offset] = data;
                }

                int percent = percentage_distribution(uniform_generator);
                if (percent < 5) {
                    pthread_barrier_wait(&barrier);
                    if (id == 0) {
                        reopen();
                        target = pool->get_root<uint8_t>(0);
                        if (!target || memcmp(target, snapshot, kAllocatedBytes) != 0) {
                            printf("Consistency check failed\n");
                            exit(EXIT_FAILURE);
                        }
                    }
                    pthread_barrier_wait(&barrier);
                } else if (percent < 15) {
                    pool->checkpoint(option.threads);
                    if (id == 0) {
                        for (int i = 0; i < 4; ++i) {
                            memcpy(snapshot + segment[i], target + segment[i], kSegmentBytes);
                        }
                    }
                    pthread_barrier_wait(&barrier);
                    for (int i = 0; i < 4; ++i) {
                        segment[i] = segment_distribution(uniform_generator) * kSegmentBytes;
                    }
                }

                if (id == 0 && (step % 100 == 0)) {
                    printf("Iteration %d has done\n", step);
                }
            }

            if (id == 0) {
                printf("Consistency check passed\n");
            }
            return 1;
        }
    };
}

#endif //LIBCRPM_CONSISTENCY_CHECK_H
