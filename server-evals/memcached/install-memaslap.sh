#!/bin/bash -e

pushd libmemcached-1.0.18
    ./configure --enable-memaslap
    sudo make install -j`nproc`
popd
