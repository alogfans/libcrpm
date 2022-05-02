#!/bin/bash

function run_bench() {
  rm -rf /mnt/pmem0/renfeng/*
  mpirun -quiet --mca btl vader,self --mca pml ob1 -n 8 -rf rank ./test_HPCCG $1 $1 $1 $2
}

function run_bench_recovery() {
  rm -rf /mnt/pmem0/renfeng/*
  mpirun -quiet --mca btl vader,self --mca pml ob1 -n 8 -rf rank timeout -s KILL $3 ./test_HPCCG $1 $1 $1 $2
  mpirun -quiet --mca btl vader,self --mca pml ob1 -n 8 -rf rank ./test_HPCCG $1 $1 $1 $2
}

make -j > /dev/null

# Measure normal execution time
run_bench 160 5
run_bench 190 5
run_bench 220 5

# Kill the processes after 20 secs, then restart the application
# run_bench_recovery 160 5 20
