#!/bin/bash

DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
source $DIR/utils.sh
set -xv

function aof_len()
{
	assert pfdd --rw read --of /tmp/pfhead -v $1
	read LO HI <<< $(hexdump -n 8 -s 8 -e '/4 "%d "'  /tmp/pfhead)
	echo $(( (HI<<16) + LO))
}


VOL_NAME=test_5_aof
VOL_SIZE=$((5<<30)) #5G on my testing platform
COND_IP=$(pfcli get_pfc)
read DB_IP DB_NAME DB_USER DB_PASS <<< $(assert pfcli get_conn_str)
export DB_IP DB_NAME DB_USER DB_PASS

pfcli delete_volume  -v $VOL_NAME

FIFO_IN=/tmp/pf_aof_in
AOF_SRC_DAT=/tmp/aof_src.dat
dd if=/dev/urandom bs=1M count=10 of=$AOF_SRC_DAT
rm -f $FIFO_IN
assert mkfifo $FIFO_IN
sleep 1000 > $FIFO_IN &
SLP_PID=$!

aof_helper $VOL_NAME /etc/pureflash/pf.conf < $FIFO_IN &
HELPER_PID=$!

assert_equal "$(pidof aof_helper)" "$HELPER_PID"
echo "a 1024 $AOF_SRC_DAT 0" > $FIFO_IN
echo "s" > $FIFO_IN
assert_equal "$(aof_len $VOL_NAME)" "1024"
# r <len> <vol_off> dst_file dst_off
# echo "r 1024 0 $AOF_OUT_DAT 0" > $FIFO_IN
#to test
#1. read unaligned data
#2. read data in write buffer
#3. read exceed file length



echo "q" > $FIFO_IN
assert wait $HELPER_PID
kill $SLP_PID



#check length
