#!/bin/bash

DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
source $DIR/utils.sh
#set -xv

FIFO_IN=/tmp/pf_aof_in
AOF_SRC_DAT=/tmp/aof_src.dat
AOF_OUT_DAT=/tmp/aof_out.dat

function aof_len()
{
	assert pfdd --rw read --of /tmp/pfhead -v $1
	read LO HI <<< $(hexdump -n 8 -s 8 -e '/4 "%d "'  /tmp/pfhead)
	echo $(( (HI<<16) + LO))
}
function cleanup {
  pkill aof_helper
  pkill sleep
#  rm  -f /tmp/pfhead  $FIFO_IN $AOF_SRC_DAT $AOF_OUT_DAT
}
trap cleanup EXIT

VOL_NAME=test_5_aof
VOL_SIZE=$((5<<30)) #5G on my testing platform
COND_IP=$(pfcli get_pfc)
read DB_IP DB_NAME DB_USER DB_PASS <<< $(assert pfcli get_conn_str)
export DB_IP DB_NAME DB_USER DB_PASS

pfcli delete_volume  -v $VOL_NAME


dd if=/dev/urandom bs=1M count=10 of=$AOF_SRC_DAT
rm -f $FIFO_IN
assert mkfifo $FIFO_IN
sleep 1000 > $FIFO_IN &
SLP_PID=$!

aof_helper $VOL_NAME /etc/pureflash/pf.conf < $FIFO_IN &
HELPER_PID=$!
sleep 2

assert_equal "$(pidof aof_helper)" "$HELPER_PID"
echo "a 1024 $AOF_SRC_DAT 0" > $FIFO_IN
echo "s" > $FIFO_IN
assert_equal "$(aof_len $VOL_NAME)" "1024"
for (( i=0; i<5; i++ )); do
	echo "a $((1<<20)) $AOF_SRC_DAT $(( (i<<20) + 1024 ))" > $FIFO_IN
done
echo "s" > $FIFO_IN

FILE_LEN=$(aof_len $VOL_NAME)

assert_equal "$FILE_LEN" "$(( (5<<20) + 1024 ))"

function read_check()
{
# r <len> <vol_off> dst_file dst_off
	info "read len:$1 from:$2"
	echo "r $1 $2 $AOF_OUT_DAT $2" > $FIFO_IN
	sleep 1 #wait command execution
	assert_equal "$(dd if=$AOF_OUT_DAT bs=1024 skip=$(($2>>10)) count=$(($1>>10))  | md5sum -b)" "$(dd if=$AOF_SRC_DAT bs=1024 skip=$(($2>>10)) count=$(($1>>10))  | md5sum -b)"
}
#to test
read_check 8192 0 
#1. read unaligned data
read_check 1024 $((7<<10))
read_check 1024 $((8<<10))
read_check 1024 $((6<<10))

read_check 4096 $((1<<20 - 1024))
read_check $((3<<20)) $((1<<20 - 2048))


#2. read data in write buffer
echo "a $(( (1<<20)+4096)) $AOF_SRC_DAT $FILE_LEN" > $FIFO_IN
sleep 1
read_check $((8<<10)) $(( FILE_LEN - (5<<10) )) 
read_check $((8<<10)) $(( FILE_LEN + (5<<10) )) 

#3. read exceed file length

echo "q" > $FIFO_IN
assert wait $HELPER_PID
kill $SLP_PID

info "========Test OK!=========="
