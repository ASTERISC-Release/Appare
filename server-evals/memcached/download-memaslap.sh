#!/bin/bash -e

[ ! -f libmemcached-1.0.18.tar.gz ] && wget https://launchpad.net/libmemcached/1.0/1.0.18/+download/libmemcached-1.0.18.tar.gz

[ ! -d libmemcached-1.0.18 ] && tar -xvf libmemcached-1.0.18.tar.gz