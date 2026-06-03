#!/bin/bash -e

# Get 10000 requests for a 10KB file using a concurrecy level of 2
#for i in {1..5}
#do
#	taskset -c 6-15 ab -n 10000 -c 12  http://127.0.0.1:9000/10K.html
#	sleep 30
#done


ab -n 10000 -c 12  http://127.0.0.1:9000/10K.html
