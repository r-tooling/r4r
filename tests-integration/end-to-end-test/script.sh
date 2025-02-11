#!/bin/sh

set -e

# add file to home
echo "test1" >"$HOME/test1.txt"
echo "test2" >"$HOME/test2.txt"

r4r ./program.sh
