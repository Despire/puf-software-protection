#!/bin/bash

num_iterations=6
puf_block_1=0x82a00000
puf_size=2097152

for num in 20 30 40; do
    sleep_seconds=$num

    folder_name="./${sleep_seconds}s"
    mkdir -p "$folder_name"

    for ((i=1; i<=$num_iterations; i++)); do
        iter=$((i + 0))
        echo "Iteration $iter"

        ssh -q root@beaglebone.local insmod /home/debian/puf/dram_puf.ko puf_block_1=${puf_block_1} puf_block_1_size=${puf_size}

        echo "decaying..."
        sleep ${sleep_seconds}

        ssh -q root@beaglebone.local rmmod dram_puf

        ssh -q root@beaglebone.local cat /dev/puf_block_1 >  "${folder_name}/BBB_${iter}_${num}sec"

        echo "Done with Iteration $iter!"
        sleep 2 # wait between iterations.
    done
    sleep 10 # wait between decay times.
done
