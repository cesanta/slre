#!/usr/bin/env bash

# Test library
#
#--------------------------
echo "Running Unit test"
echo ""
./unit_test.bin
echo ""
echo ""

# Test shared library Wrapper
# 
export LD_LIBRARY_PATH=$(pwd):$LD_LIBRARY_PATH

echo "Running Shared Library Unit Test"
echo ""
./unit_test_shared.bin
echo ""
echo ""

