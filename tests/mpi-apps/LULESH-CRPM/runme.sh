#!/bin/bash

function run_bench() {
	rm -rf /mnt/pmem0/renfeng/*
	mpirun --mca btl vader,self --mca pml ob1 -n 8 -rf rank ./lulesh2.0 -q -s $1 -i 250 -cp $2
}

function run_bench_recovery() {
  rm -rf /mnt/pmem0/renfeng/*
  mpirun -quiet --mca btl vader,self --mca pml ob1 -n 8 -rf rank timeout -s KILL $3 ./lulesh2.0 -p -s $1 -i 250 -cp $2
  mpirun -quiet --mca btl vader,self --mca pml ob1 -n 8 -rf rank ./lulesh2.0 -p -s $1 -i 250 -cp $2
}

make -j > /dev/null

run_bench_recovery 90 5 20
exit 0

# Measure normal execution time
run_bench 90 5
run_bench 100 5
run_bench 110 5

# Kill the processes after 20 secs, then restart the application
run_bench_recovery 90 5 20

