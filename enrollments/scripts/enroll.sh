#!/bin/bash

num_iterations=3
puf_block_1=0x84c00000
puf_size=4194304

echo "starting measurements"
#for num in 20 30 40 50; do
for num in 50; do
    sleep_seconds=$num

    folder_name="./${sleep_seconds}s"
    mkdir -p "$folder_name"

    for ((i=1; i<=$num_iterations; i++)); do
        iter=$((i + 0))
        echo "Iteration $iter"

        ssh -q root@beaglebone.local insmod /home/debian/puf/dram_puf.ko puf_block_1=${puf_block_1} puf_block_1_size=${puf_size}
        echo "recording dram cells at $sleep_seconds secs of decay"
        sleep ${sleep_seconds}
        ssh -q root@beaglebone.local rmmod dram_puf

        ssh -q root@beaglebone.local cat /dev/puf_block_1 >  "${folder_name}/BBB_${iter}_${num}sec"
        echo "Done with Iteration $iter!"
        echo "wait 5 mins before next iteration"
        sleep 300
    done
    echo "wait 5 mins before the next timeout"
    sleep 300
done
echo "finished measurements"
