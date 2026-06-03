#!/bin/sh

redis-benchmark -h 127.0.0.1 -p 11000 -c 1000 -n 50000000 -t get,set  -P 32
