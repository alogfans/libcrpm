# libcrpm: Improving the Checkpoint Performance of NVM

`libcrpm` (**C**heckpoint-**R**ecovery interface using **P**ersistent **M**emory) is a new programming library to improve the checkpoint performance for applications running in NVM. Developers only need to follow the traditional crash-recovery programming paradigm.

`libcrpm` addresses two problems simultaneously that exist in the current NVM-based checkpoint-recovery libraries: (1) high write amplification when page-granularity incremental checkpointing is used, and (2) high persistence costs from excessive memory fence instructions when fine-grained undo-log or copy-on-write is used. 

For more details, please refer to our paper:

[DAC'22] libcrpm: Improving the Checkpoint Performance of NVM. Feng Ren, Kang Chen and Yongwei Wu, Tsinghua University.

## System Requirements

* Optane DC persistent memory, configured as FSDAX mode and mounted to /mnt/pmem0.
* Clang/LLVM 10.x, required by our instrumention tool. We built and verified libcrpm with clang 10.0.0, if you use other clang versions, you may have to modify codes to resolve interface incompatibility.
* OpenMPI 4.x, required for building MPI support modules. Currently MPICH is not supported. `mpicc` should be located in the `PATH` directory.

## Getting Started

### Build kernel module
The `flush` directory includes a kernel module for wbinvd instruction invocation. This is extracted from [InCLL](https://github.com/epfl-vlsc/Incll). You should build and load this kernel module first, and this requires the root privilege. The `linux-headers` package should be installed.

1. `cd flush`
2. `make`
3. `sudo insmod gf.ko`
4. `sudo chmod 777 /dev/global_flush`

### Build `libcrpm`

1. `cd libcrpm`
2. `mkdir build; cd build; cmake ..; make -j`
3. `mkdir /mnt/pmem0/renfeng`

### Evaluate `libcrpm`

We provide test scripts for generating datasets and evaluating end-to-end performance of C++ STL data structures (`map` and `unordered_map`).
1. `bash ../script/prepare-dataset.sh`
2. `bash ../script/perf-test.sh` 
When each test completes, there is one line of output. The last two fields are latency and throughput of this test.

You can also use the following command for run custom tests:
```
./tests/benchmark -d <dataset-path> -t <threads> 
                  -p <populated_records_before_execution> 
                  -i <N: a-checkpoint-per-N-operations>
                  -b <stl-map|stl-unordered-map> -e default -a default
```

As the starting point, we recommend you to read the `tests` directory for understanding the programming interface of `libcrpm`. It is no hard to transform your application to be recoverable.
