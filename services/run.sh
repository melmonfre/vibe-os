#!/bin/bash
# Quickly build and run both background services

set -e
make clean && make

./audio-service /tmp/audio-socket &
./net-service   /tmp/net-socket &
echo
wait