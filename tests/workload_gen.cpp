//
// Created by Feng Ren on 2021/1/24.
//

#include <fstream>
#include <sstream>
#include "workload_gen.h"

namespace crpm {
    Workload::Workload(size_t record_count, int read_prop, int update_prop,
                       int scan_prop, int insert_prop, const std::string &key_dist,
                       bool use_hashed_key)
            : insert_key_sequence(0),
              scan_length(1, 100),
              use_hashed_key(use_hashed_key) {
        if (read_prop)
            op_chooser.addValue(READ, read_prop);
        if (update_prop)
            op_chooser.addValue(UPDATE, update_prop);
        if (scan_prop)
            op_chooser.addValue(SCAN, scan_prop);
        if (insert_prop)
            op_chooser.addValue(INSERT, insert_prop);
        int raw_prop = 100 - read_prop - update_prop - scan_prop - insert_prop;
        if (raw_prop)
            op_chooser.addValue(READMODIFYWRITE, raw_prop);

        insert_key_sequence.set(record_count);
        if (key_dist == "zipfian") {
            int new_keys = (int) (record_count * insert_prop / 100 * 2); // a fudge factor
            key_generator = new ScrambledZipfianGenerator(record_count + new_keys);
        } else if (key_dist == "uniform") {
            key_generator = new UniformGenerator(0, record_count - 1);
        } else if (key_dist == "latest") {
            key_generator = new SkewedLatestGenerator(insert_key_sequence);
        } else if (key_dist == "counter") {
            insert_key_sequence.set(0);
            key_generator = new CounterGenerator(0);
        } else {
            assert(0);
        }
    }

    Workload::~Workload() {
        if (key_generator)
            delete key_generator;
        key_generator = nullptr;
    }

    void Workload::write_to_file(const std::string &path, size_t op_count) {
        FILE *fout = fopen(path.c_str(), "wb");
        if (!fout) {
            perror("fopen");
            exit(EXIT_FAILURE);
        }
        OperationEntry *entries = new OperationEntry[op_count];
        for (size_t i = 0; i < op_count; ++i) {
            entries[i].type = nextOperation();
            if (entries[i].type == INSERT) {
                entries[i].key = nextSequenceKey();
            } else {
                entries[i].key = nextKey();
            }
            if (entries[i].type == SCAN) {
                entries[i].scan_length = nextScanLength();
            }
        }
        if (fwrite(entries, sizeof(OperationEntry), op_count, fout) != op_count) {
            perror("fwrite");
        }
        fclose(fout);
    }
}

using namespace crpm;

int main(int argc, char **argv) {
    if (argc < 4) {
        fprintf(stderr, "Usage: %s <type> <record_count> <op_count>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    std::string type(argv[1]);
    size_t record_count = atoi(argv[2]);
    size_t op_count = atoi(argv[3]);

    Workload *factory;
    if (type == "insert") {
        factory = new InsertOnly(record_count);
        op_count = record_count;
    } else if (type == "a") {
        factory = new YCSBA(record_count);
    } else if (type == "b") {
        factory = new YCSBB(record_count);
    } else if (type == "c") {
        factory = new YCSBC(record_count);
    } else {
        assert(0 && "unknown workload type");
        exit(EXIT_FAILURE);
    }

    std::string path = type + "-" + argv[2] + "-" + argv[3];
    factory->write_to_file(path, op_count);
    delete factory;
    return 0;
}
