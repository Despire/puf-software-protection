#!/bin/bash

set +x

if [ ! -f /project/.initialized ]; then
    echo "Downloading LLVM 17.0.6"
    wget https://github.com/llvm/llvm-project/releases/download/llvmorg-17.0.6/clang+llvm-17.0.6-armv7a-linux-gnueabihf.tar.gz
    echo "Finished"

    echo "Unpacking"
    tar -xzvf clang+llvm-17.0.6-armv7a-linux-gnueabihf.tar.gz
    echo "Done"

    touch /project/.initialized
fi

bash
