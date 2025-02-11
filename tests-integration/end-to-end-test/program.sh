#!/bin/bash

exec &>output.txt

# access file from home directory hardocoded
cat /home/r4r/test1.txt

# access file from home directory using $HOME
cat $HOME/test2.txt
