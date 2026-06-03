# Running many benchmark tests in Phoronix

- Execute `prepare-phoronix.sh` to install the phoronix benchmark suite

- Install some tests within the benchmark suite using `install-tests.sh`
    - By default, it installs 3 tests: openssl, 7-zip, and SQLite

## Running tests

To run the OpenSSL test natively:

    ./openssl.sh

To run the OpenSSL test with audit:

    ./run-test-audit.sh system/openssl openssl
    ./run-test-audit.sh pts/sqlite-speedtest sqlite
    ./run-test-audit.sh pts/7zip 7zip

To stop auditing:

    ./stop-audit.sh
