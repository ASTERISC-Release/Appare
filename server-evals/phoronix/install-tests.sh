#!/bin/bash -e

ssh seclog@localhost -p 8000 "sudo apt update && sudo apt install python3-pip"
ssh seclog@localhost -p 8000 "pip install oct2py"
ssh seclog@localhost -p 8000 "phoronix-test-suite install pts/openssl"
ssh seclog@localhost -p 8000 "phoronix-test-suite install pts/compress-7zip"
ssh seclog@localhost -p 8000 "phoronix-test-suite install sqlite-speedtest"