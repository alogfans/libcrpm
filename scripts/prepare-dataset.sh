#!/bin/bash
NR_RECORDS=24000000
NR_OPS=24000000

function prepare_dataset() {
  WORKLOAD_GEN=../build/tests/workload_gen
  for DATASET_TYPE in insert a b c; do
    $WORKLOAD_GEN $DATASET_TYPE $NR_RECORDS $NR_OPS
  done
}

prepare_dataset