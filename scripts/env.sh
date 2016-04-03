#!/bin/bash
DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
export PATH=$DIR/bin:$PATH
export LD_LIBRARY_PATH=$DIR/bin:$LD_LIBRARY_PATH
export LOG4C_RCPATH=$DIR/bin

