//
// Created by Feng Ren on 2021/1/24.
//

#ifndef LIBCRPM_WORKLOAD_GEN_H
#define LIBCRPM_WORKLOAD_GEN_H

#include <cmath>
#include <mutex>
#include <atomic>
#include <cstdlib>
#include <cstdint>
#include <cassert>
#include <random>

namespace crpm {
    inline double RandomDouble(double min = 0.0, double max = 1.0) {
        static std::default_random_engine generator;
        static std::uniform_real_distribution<double> uniform(min, max);
        return uniform(generator);
    }

    inline char RandomPrintChar() {
        return rand() % 94 + 33;
    }

    const uint64_t kFNVOffsetBasis64 = 0xCBF29CE484222325;
    const uint64_t kFNVPrime64 = 1099511628211;

    inline uint64_t FNVHash64(uint64_t val) {
        uint64_t hash = kFNVOffsetBasis64;

        for (int i = 0; i < 8; i++) {
            uint64_t octet = val & 0x00ff;
            val = val >> 8;

            hash = hash ^ octet;
            hash = hash * kFNVPrime64;
        }
        return hash;
    }

    class Generator {
    public:
        virtual uint64_t next() = 0;

        virtual uint64_t last() = 0;

        virtual ~Generator() {}
    };

    class ZipfianGenerator : public Generator {
    public:
        constexpr static const double kZipfianConst = 0.99;
        static const uint64_t kMaxNumItems = (UINT64_MAX >> 24);

        ZipfianGenerator(uint64_t min, uint64_t max,
                         double zipfian_const = kZipfianConst) :
                num_items_(max - min + 1), base_(min), theta_(zipfian_const),
                zeta_n_(0), n_for_zeta_(0) {
            assert(num_items_ >= 2 && num_items_ < kMaxNumItems);
            zeta_2_ = zeta(2, theta_);
            alpha_ = 1.0 / (1.0 - theta_);
            raiseZeta(num_items_);
            eta_ = eta();
            next(num_items_);
        }

        ZipfianGenerator(uint64_t num_items) :
                ZipfianGenerator(0, num_items - 1, kZipfianConst) {}


        uint64_t next(uint64_t num) {
            assert(num >= 2 && num < kMaxNumItems);
            std::lock_guard<std::mutex> lock(mutex_);

            if (num > n_for_zeta_) { // Recompute zeta_n and eta
                raiseZeta(num);
                eta_ = eta();
            }

            double u = RandomDouble();
            double uz = u * zeta_n_;

            if (uz < 1.0) {
                return last_value_ = 0;
            }

            if (uz < 1.0 + std::pow(0.5, theta_)) {
                return last_value_ = 1;
            }

            return last_value_ = base_ + num * std::pow(eta_ * u - eta_ + 1, alpha_);
        }

        uint64_t next() { return next(num_items_); }

        uint64_t last() {
            std::lock_guard<std::mutex> lock(mutex_);
            return last_value_;
        }

    private:
        ///
        /// Compute the zeta constant needed for the distribution.
        /// Remember the number of items, so if it is changed, we can recompute zeta.
        ///
        void raiseZeta(uint64_t num) {
            assert(num >= n_for_zeta_);
            zeta_n_ = zeta(n_for_zeta_, num, theta_, zeta_n_);
            n_for_zeta_ = num;
        }

        double eta() {
            return (1 - std::pow(2.0 / num_items_, 1 - theta_)) /
                   (1 - zeta_2_ / zeta_n_);
        }

        ///
        /// Calculate the zeta constant needed for a distribution.
        /// Do this incrementally from the last_num of items to the cur_num.
        /// Use the zipfian constant as theta. Remember the new number of items
        /// so that, if it is changed, we can recompute zeta.
        ///
        static double zeta(uint64_t last_num, uint64_t cur_num,
                           double theta, double last_zeta) {
            double zeta = last_zeta;
            for (uint64_t i = last_num + 1; i <= cur_num; ++i) {
                zeta += 1 / std::pow(i, theta);
            }
            return zeta;
        }

        static double zeta(uint64_t num, double theta) {
            return zeta(0, num, theta, 0);
        }

        uint64_t num_items_;
        uint64_t base_; /// Min number of items to generate

        // Computed parameters for generating the distribution
        double theta_, zeta_n_, eta_, alpha_, zeta_2_;
        uint64_t n_for_zeta_; /// Number of items used to compute zeta_n
        uint64_t last_value_;
        std::mutex mutex_;
    };

    class UniformGenerator : public Generator {
    public:
        // Both min and max are inclusive
        UniformGenerator(uint64_t min, uint64_t max) : dist_(min, max) { next(); }

        uint64_t next() {
            std::lock_guard<std::mutex> lock(mutex_);
            return last_int_ = dist_(generator_);
        }

        uint64_t last() {
            std::lock_guard<std::mutex> lock(mutex_);
            return last_int_;
        }

    private:
        std::mt19937_64 generator_;
        std::uniform_int_distribution<uint64_t> dist_;
        uint64_t last_int_;
        std::mutex mutex_;
    };

    class CounterGenerator : public Generator {
    public:
        CounterGenerator(uint64_t start) : counter_(start) {}

        uint64_t next() { return counter_.fetch_add(1); }

        uint64_t last() { return counter_.load() - 1; }

        void set(uint64_t start) { counter_.store(start); }

    private:
        std::atomic<uint64_t> counter_;
    };

    class SkewedLatestGenerator : public Generator {
    public:
        SkewedLatestGenerator(CounterGenerator &counter) :
                basis_(counter), zipfian_(basis_.last()) {
            next();
        }

        uint64_t next() {
            uint64_t max = basis_.last();
            return last_ = max - zipfian_.next(max);
        }

        uint64_t last() { return last_; }

    private:
        CounterGenerator &basis_;
        ZipfianGenerator zipfian_;
        std::atomic<uint64_t> last_;
    };

    class ScrambledZipfianGenerator : public Generator {
    public:
        ScrambledZipfianGenerator(uint64_t min, uint64_t max,
                                  double zipfian_const = ZipfianGenerator::kZipfianConst) :
                base_(min), num_items_(max - min + 1),
                generator_(min, max, zipfian_const) {}

        ScrambledZipfianGenerator(uint64_t num_items) :
                ScrambledZipfianGenerator(0, num_items - 1) {}

        uint64_t next() {
            return scramble(generator_.next());
        }

        uint64_t last() {
            return scramble(generator_.last());
        }

    private:
        const uint64_t base_;
        const uint64_t num_items_;
        ZipfianGenerator generator_;

        uint64_t scramble(uint64_t value) const {
            return base_ + FNVHash64(value) % num_items_;
        }
    };

    template<typename Value>
    class DiscreteGenerator {
    public:
        DiscreteGenerator() : sum_(0) {}

        void addValue(Value value, double weight) {
            if (values_.empty()) {
                last_ = value;
            }
            values_.push_back(std::make_pair(value, weight));
            sum_ += weight;
        }

        Value next() {
            mutex_.lock();
            double chooser = RandomDouble();
            mutex_.unlock();

            for (auto p = values_.cbegin(); p != values_.cend(); ++p) {
                if (chooser < p->second / sum_) {
                    return last_ = p->first;
                }
                chooser -= p->second / sum_;
            }

            assert(false);
            return last_;
        }

        Value last() { return last_; }

    private:
        std::vector<std::pair<Value, double>> values_;
        double sum_;
        std::atomic<Value> last_;
        std::mutex mutex_;
    };

    enum Operation {
        INSERT, READ, UPDATE, SCAN, READMODIFYWRITE
    };

    class Workload {
    public:
        Workload(size_t record_count,
                 int read_prop, int update_prop, int scan_prop, int insert_prop,
                 const std::string &key_dist, bool use_hashed_key = false);

        virtual ~Workload();

        void write_to_file(const std::string &path, size_t op_count);

    private:
        Operation nextOperation() { return op_chooser.next(); }

        uint64_t nextKey() {
            if (use_hashed_key) {
                return FNVHash64(key_generator->next());
            } else {
                return key_generator->next();
            }
        }

        uint64_t nextSequenceKey() {
            if (use_hashed_key) {
                return FNVHash64(insert_key_sequence.next());
            } else {
                return insert_key_sequence.next();
            }
        }

        int nextScanLength() {
            return scan_length.next();
        }

    private:
        DiscreteGenerator<Operation> op_chooser;
        CounterGenerator insert_key_sequence;
        UniformGenerator scan_length;
        Generator *key_generator;
        bool use_hashed_key;
    };
    
    struct InsertOnly : public Workload {
        InsertOnly(size_t record_count) : Workload(record_count, 0, 0, 0, 100, "counter") {}
    };

    struct YCSBA : public Workload {
        YCSBA(size_t record_count) : Workload(record_count, 50, 50, 0, 0, "zipfian") {}
    };

    struct YCSBB : public Workload {
        YCSBB(size_t record_count) : Workload(record_count, 95, 5, 0, 0, "zipfian") {}
    };

    struct YCSBC : public Workload {
        YCSBC(size_t record_count) : Workload(record_count, 100, 0, 0, 0, "zipfian") {}
    };

    struct OperationEntry {
        uint32_t type;
        uint32_t scan_length;
        uint64_t key;
    };
}

#endif //LIBCRPM_WORKLOAD_GEN_H
