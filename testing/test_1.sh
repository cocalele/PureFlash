#!/bin/bash
DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
source $DIR/utils.sh

VOL1=test_1
COND_IP=$(pfcli get_pfc)
read DB_IP DB_NAME DB_USER DB_PASS <<< $(assert pfcli get_conn_str)
export DB_IP DB_NAME DB_USER DB_PASS

curlex "http://$COND_IP:49180/s5c/?op=delete_volume&volume_name=$VOL1"
sleep 1

info "Creating volume $VOL1"
NODE_CNT=$(pfcli list_store |grep OK |wc -l)
if [ $((NODE_CNT < 3 )) ] ;  then REP_CNT=1; else REP_CNT=3; fi

assert curlex "http://$COND_IP:49180/s5c/?op=create_volume&volume_name=$VOL1&size=$((5<<30))&rep_cnt=$REP_CNT"

count1=$( get_obj_count $VOL1 )
SNAP1_FILE=${VOL1}_snap1.dat
SNAP1_OUT_FILE=${VOL1}_snap1_out.dat
SNAP2_FILE=${VOL1}_snap2.dat
SNAP2_OUT_FILE=${VOL1}_snap2_out.dat

assert dd if=/dev/urandom bs=4K count=512 of=$SNAP1_FILE
info "Writing to volume"
assert pfdd --count 512 --rw write --bs 4k -v $VOL1 --if $SNAP1_FILE
assert_equal $( get_obj_count $VOL1 ) $((count1 + REP_CNT))

info "Create snapshot snap1"
assert curlex "http://$COND_IP:49180/s5c/?op=create_snapshot&volume_name=$VOL1&snapshot_name=snap1"


assert cp $SNAP1_FILE $SNAP2_FILE
assert dd if=/dev/urandom bs=4k count=32 conv=nocreat,notrunc of=$SNAP2_FILE
info "Writing to volume again"
assert pfdd --count 32 --rw write --bs 4k -v $VOL1 --if $SNAP2_FILE
assert_equal $( get_obj_count $VOL1 ) $((count1 + REP_CNT * 2))

info "Now compare snapshot data"
SRC_MD5=$(dd if=$SNAP1_FILE bs=4k count=512 | md5sum -b)
assert pfdd --count 512 --rw read --bs 4k -v $VOL1 --snapshot snap1 --of $SNAP1_OUT_FILE
SNAP1_MD5=$(dd if=$SNAP1_OUT_FILE bs=4k count=512  | md5sum -b)
assert_equal "$SRC_MD5" "$SNAP1_MD5"

info "Now compare HEAD data"
SRC2_MD5=$(dd if=$SNAP2_FILE bs=4k count=512 | md5sum -b)
assert pfdd --count 512 --rw read --bs 4k -v $VOL1  --of $SNAP2_OUT_FILE
SNAP2_MD5=$(dd if=$SNAP2_OUT_FILE bs=4k count=512  | md5sum -b)
assert_equal "$SRC2_MD5" "$SNAP2_MD5"


info "Test OK"
