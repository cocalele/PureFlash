#!/bin/bash
DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
source $DIR/utils.sh

VOL_NAME=test_7
COND_IP=$(pfcli get_pfc)
read DB_IP DB_NAME DB_USER DB_PASS <<< $(assert pfcli get_conn_str)
export DB_IP DB_NAME DB_USER DB_PASS

curlex "http://$COND_IP:49180/s5c/?op=delete_volume&volume_name=$VOL_NAME"
sleep 1

info "Creating volume $VOL_NAME"
NODE_CNT=$(pfcli list_store |grep OK |wc -l)
REP_CNT=1

assert curlex "http://$COND_IP:49180/s5c/?op=create_volume&volume_name=$VOL_NAME&size=$((5<<30))&rep_cnt=$REP_CNT"

count1=$( get_obj_count $VOL_NAME )
SNAP1_FILE=${VOL_NAME}_snap1.dat
#SNAP1_OUT_FILE=${VOL_NAME}_snap1_out.dat
SNAP2_FILE=${VOL_NAME}_snap2.dat
#SNAP2_OUT_FILE=${VOL_NAME}_snap2_out.dat

assert dd if=/dev/urandom bs=4K count=512 of=$SNAP1_FILE
info "Writing to volume"
assert pfdd --count 512 --rw write --bs 4k -v $VOL_NAME --if $SNAP1_FILE
assert_equal $( get_obj_count $VOL_NAME ) $((count1 + REP_CNT))

info "Create snapshot snap1"
assert curlex "http://$COND_IP:49180/s5c/?op=create_snapshot&volume_name=$VOL_NAME&snapshot_name=snap1"


assert cp $SNAP1_FILE $SNAP2_FILE
assert dd if=/dev/urandom bs=4k count=32 conv=nocreat,notrunc of=$SNAP2_FILE
info "Writing to volume again"
assert pfdd --count 32 --rw write --bs 4k -v $VOL_NAME --if $SNAP2_FILE
assert_equal $( get_obj_count $VOL_NAME ) $((count1 + REP_CNT * 2))

info "Now compare snapshot data"
SRC_MD5=$(dd if=$SNAP1_FILE bs=4k count=512 | md5sum -b)
#assert pfdd --count 512 --rw read --bs 4k -v $VOL_NAME --snapshot snap1 --of $SNAP1_OUT_FILE
SNAP1_MD5=$(assert pfdd --count 512 --rw read --bs 4k -v $VOL_NAME --snapshot snap1 --of /dev/stdout | md5sum -b)
assert_equal "$SRC_MD5" "$SNAP1_MD5"

info "Now compare HEAD data"
SRC2_MD5=$(dd if=$SNAP2_FILE bs=4k count=512 | md5sum -b)
#assert pfdd --count 512 --rw read --bs 4k -v $VOL_NAME  --of $SNAP2_OUT_FILE
SNAP2_MD5=$(assert pfdd --count 512 --rw read --bs 4k -v $VOL_NAME  --of /dev/stdout  | md5sum -b)
assert_equal "$SRC2_MD5" "$SNAP2_MD5"

PRIMARY_IP=$(query_db "select mngt_ip from t_store where id in (select store_id from v_replica_ext where is_primary=1 and volume_name='$VOL_NAME') limit 1")
info "stop pfs on $PRIMARY_IP"
stop_pfs $PRIMARY_IP
sleep 5
info "start pfs on $PRIMARY_IP"
start_pfs $PRIMARY_IP
sleep 3
SNAP2_MD5=$(assert pfdd --count 512 --rw read --bs 4k -v $VOL_NAME  --of /dev/stdout  | md5sum -b)
assert_equal "$SRC2_MD5" "$SNAP2_MD5"
SNAP1_MD5=$(assert pfdd --count 512 --rw read --bs 4k -v $VOL_NAME --snapshot snap1 --of /dev/stdout | md5sum -b)
assert_equal "$SRC_MD5" "$SNAP1_MD5"

info "Test OK"
