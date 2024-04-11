#!/bin/bash

set +x

if [ ! -f /project/.initialized ]; then
    echo "Unpacking"
    unzip clang+llvm-17.0.6-armv7a-linux-gnueabihf.zip
    echo "Done"

    touch /project/.initialized
fi

bash
