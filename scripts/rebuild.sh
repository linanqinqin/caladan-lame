#!/bin/bash

# exit immediately if any command fails
set -e  

# go to the home directory of caladan-lame
cd "$(dirname "$0")/.."
CALADAN_HOME=$(pwd)

# rebuild caladan
make clean
make -j

# rebuild ksched
cd ksched/
make clean
make

# rebuild hello_world
cd ../apps/hello_world
make clean
make threads matmul

# setup machine and start iokerneld
cd "$CALADAN_HOME"
sudo ./scripts/setup_machine.sh
sudo ./iokerneld simple nobw noidlefastwake 24-27
