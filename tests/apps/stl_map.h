//
// Created by Feng Ren on 2021/1/24.
//

#ifndef LIBCRPM_STL_MAP_H
#define LIBCRPM_STL_MAP_H

#include <cassert>
#include <map>

#include "../bench.h"

namespace crpm {
    template<typename T>
    class STLMapBenchmark : public Benchmark {
        using MapType = std::map<uint64_t, T,
                std::less<uint64_t>,
                Crpm2Allocator<std::pair<const uint64_t, T>>>;
        MapType *target;

    public:
        STLMapBenchmark(const BenchmarkOption &option) : Benchmark(option) {
            assert(option.threads == 1);
            target = pool->pnew<MapType>();
            pool->set_root(0, target);
            RegisterGlobalMemoryPool(pool);
        }

        virtual ~STLMapBenchmark() {
            pool->pdelete(target);
            UnregisterGlobalMemoryPool();
        }

    protected:
        virtual void setup(unsigned int id) {
            unsigned long i;
            T value_buffer;
            for (i = 0; i < option.populated_keys; i += option.threads) {
                uint64_t key = option.use_hashed_key ? FNVHash64(i) : i;
                target->insert(std::make_pair(key, value_buffer));
            }
            pool->checkpoint();
        }

        virtual void teardown(unsigned int id) {}

        virtual uint64_t worker(unsigned int id) {
            unsigned long i = 0, cnt = 0;
            T value_buffer;
            int num_checkpoints = 5000 / option.interval + 1;
            uint64_t last_clock = GetCurrentMillisecond();
            uint64_t checkpoint_time = 0;
            bool do_insert_test = getenv("INSERT_TEST");
            while (true) {
                if (do_insert_test && cnt >= 5000000) {
                    break;
                }
                if (!do_insert_test && num_checkpoints == 0) {
                    break;
                }
                Operation &entry = execute_trace[id][i];
                i++;
                cnt++;
                if (i == execute_trace[id].size()) { 
                    if (do_insert_test) {
                        pool->checkpoint(option.threads);
                        return cnt;
                    } else {
                        i = 0;
                    }
                }
                uint64_t key = option.use_hashed_key ? FNVHash64(entry.key) : entry.key;
                if (entry.type == TYPE_INSERT) {
                    target->insert(std::make_pair(key, value_buffer));
                } else if (entry.type == TYPE_UPDATE) {
                    auto iter = target->find(key);
                    assert(iter != target->end());
                    iter->second = value_buffer;
                } else if (entry.type == TYPE_READ) {
                    auto iter = target->find(key);
                    assert(iter != target->end());
                    value_buffer = iter->second;
                } else if (entry.type == TYPE_RMW) {
                    auto iter = target->find(key);
                    assert(iter != target->end());
                    value_buffer = iter->second;
                    target->insert(std::make_pair(key, value_buffer));
                } else if (entry.type == TYPE_SCAN) {
                    auto iter = target->find(key);
                    for (int k = 0;
                         k < entry.scan_length && iter != target->end();
                         ++k, ++iter) {
                        value_buffer = iter->second;
                    }
                } else {
                    assert(0 && "not supported command");
                    exit(EXIT_FAILURE);
                }
                if (i % 20 == 0) {
                    uint64_t curr_clock = GetCurrentMillisecond();
                    if (curr_clock - last_clock > option.interval) {
                        pool->checkpoint(option.threads);
                        last_clock = GetCurrentMillisecond();
                        checkpoint_time += (last_clock - curr_clock);
                        num_checkpoints--;
                    }
                }
            }
            // if (id == 0) { printf("%d; %ld\n", 5000 / option.interval + 1, checkpoint_time); }
            return cnt;
        }
    };
}

#endif //LIBCRPM_STL_MAP_H
