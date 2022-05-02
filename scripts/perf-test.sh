#!/bin/bash
NR_RECORDS=24000000
NR_OPS=24000000
BENCH_APP=../build/tests/benchmark
ENGINE=default
ALLOCATOR=default

function thread_test() {
  DATA_STRUCTURE=$1
  INTERVAL=128
  for DATASET_TYPE in insert a b c; do
    DATASET=$DATASET_TYPE-$NR_RECORDS-$NR_OPS
    for NR_THREADS in 1
    do
      if [ $DATASET_TYPE = insert ]; then
        INSERT_TEST=1 $BENCH_APP -d $DATASET -H -t $NR_THREADS -i $INTERVAL -b $DATA_STRUCTURE -e $ENGINE -a $ALLOCATOR
      else
        $BENCH_APP -d $DATASET -t $NR_THREADS -p $NR_RECORDS -i $INTERVAL -b $DATA_STRUCTURE -e $ENGINE -a $ALLOCATOR
      fi
    done
  done
}

thread_test stl-map
thread_test stl-unordered-map