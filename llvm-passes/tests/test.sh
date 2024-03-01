#!/bin/bash

echo "----- Puf: Patching -----"
  python3 ./tests/test_pufparser.py && echo "ok"
echo "-------------------------"

echo "----- Puf: Checksum -----"
  python3 ./tests/test_checksum.py && echo "ok"
echo "-------------------------"

