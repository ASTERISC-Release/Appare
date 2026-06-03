# Testing Memcached using Memaslap

The following explains memcached's install in a client-server model. In particular,
the server runs memcached and the client executes the memaslap benchmark to query
keys from the server. Note that the server and client can be the same (local) machine.

## Quick Start

1. In the server, please execute `install-memcached.sh`
2. In the client, perform the following steps:
    - Download memaslap: `download-memaslap.sh`
    - Edit Makefile by adding LDFLAGS "-L/lib64 -lpthread" 
    
            ### diff of Makefile
            # -LDFLAGS =
            # +LDFLAGS = -L/lib64 -lpthread 
            ###
    - Install memslap: `install-memaslap.sh`

## Running Memacached with Audit

If you want to audit memcached, after installation (in server) execute `start-memcached-audit.sh`. 
You can stop auditing using `stop-memcached-audit.sh`. By default, the audit rules are based on
the ones used in the Hardlog (SP21) paper.

## Testing memcached performance

In the client machine, run `benchmark.sh` after installing memaslap. By default it will run
memaslap with a 90:10 GET:SET split for 60 seconds.

## Guides for troubleshooting

- https://medium.com/swlh/the-complete-guide-to-benchmark-the-performance-of-memcached-on-ubuntu-16-04-71edeaf6e740 