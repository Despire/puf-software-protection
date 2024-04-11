#!/bin/bash

set +x

if [ ! -f /project/.initialized ]; then
    echo "Unpacking"
    tar -xzvf clang+llvm-17.0.6-armv7a-linux-gnueabihf.tar.gz
    echo "Done"

    touch /project/.initialized
fi

bash
