#!/bin/bash
DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
source $DIR/utils.sh

VOL1=test_1
COND_IP=$(pfcli get_pfc)
read DB_IP DB_NAME DB_USER DB_PASS <<< $(assert pfcli get_conn_str)
export DB_IP DB_NAME DB_USER DB_PASS

curlex "http://$COND_IP:49180/s5c/?op=delete_volume&name=$VOL1"

info "Creating volume $VOL1"
assert curlex "http://$COND_IP:49180/s5c/?op=create_volume&name=$VOL1&size=$((5<<30))&rep_cnt=3"

count1=$( get_obj_count $VOL1 )
SNAP1_FILE=${VOL1}_snap1.dat
SNAP1_OUT_FILE=${VOL1}_snap1_out.dat
SNAP2_FILE=${VOL1}_snap2.dat

assert dd if=/dev/urandom bs=1M count=1 of=$SNAP1_FILE
info "Writing to volume"
assert pfdd --count 32 --rw write --bs 4k -v $VOL1 --if $SNAP1_FILE
assert_equal $( get_obj_count $VOL1 ) $(($count1 + 3))

info "Create snapshot snap1"
assert curlex "http://$COND_IP:49180/s5c/?op=create_snapshot&volume_name=$VOL1&snapshot_name=snap1"


assert dd if=/dev/urandom bs=1M count=1 of=$SNAP2_FILE
info "Writing to volume again"
assert pfdd --count 32 --rw write --bs 4k -v $VOL1 --if $SNAP2_FILE
assert_equal $( get_obj_count $VOL1 ) $(($count1 + 6))

info "Now compare snapshot data"
SRC_MD5=$(dd if=$SNAP1_FILE bs=4k count=32 | md5sum -b)
assert pfdd --count 32 --rw read --bs 4k -v $VOL1 --snapshot snap1 --of $SNAP1_OUT_FILE
SNAP1_MD5=$(dd if=$SNAP1_OUT_FILE bs=4k count=32  | md5sum -b)

assert_equal "$SRC_MD5" "$SNAP1_MD5"

info "Test OK"
