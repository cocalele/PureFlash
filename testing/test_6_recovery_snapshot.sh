#!/bin/bash
DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
source $DIR/utils.sh

VOL_NAME=test_6
VOL_SIZE=$((5<<30)) #5G on my testing platform
COND_IP=$(pfcli get_pfc)
read DB_IP DB_NAME DB_USER DB_PASS <<< $(assert pfcli get_conn_str)
export DB_IP DB_NAME DB_USER DB_PASS

NODE_CNT=$(pfcli list_store |grep OK |wc -l)
if ((NODE_CNT < 2 )) ;  then 
	fatal "At least 2 nodes are required"
fi
if  ((NODE_CNT < 3 )) ;  then REP_CNT=2; else REP_CNT=3; fi


pfcli delete_volume  -v $VOL_NAME
assert pfcli create_volume  -v $VOL_NAME -s $VOL_SIZE -r $REP_CNT
count1=$( get_obj_count $VOL_NAME )


PRIMARY_IP=$(query_db "select mngt_ip from t_store where id in (select store_id from v_replica_ext where is_primary=1 and volume_name='$VOL_NAME') limit 1")
info "Primary node is:$PRIMARY_IP"

STORE_IP=$(query_db "select mngt_ip from t_store where id in (select store_id from v_replica_ext where is_primary=0 and volume_name='$VOL_NAME') limit 1")
info "Slave node is:$STORE_IP"


primary_cnt=$(obj_cnt_on_ip $PRIMARY_IP)
slave_cnt=$(obj_cnt_on_ip $STORE_IP)



assert dd if=/dev/urandom bs=4K count=512 of=snap1.dat
assert pfdd --count 512 --rw write --bs 4k -v $VOL_NAME --if snap1.dat
assert pfcli create_snapshot -v $VOL_NAME -n snap1

assert cp snap1.dat snap2.dat
assert dd if=/dev/urandom bs=4k count=32 conv=nocreat,notrunc of=snap2.dat
assert pfdd --count 64 --rw write --bs 4k -v $VOL_NAME --if snap2.dat
assert pfcli create_snapshot -v $VOL_NAME -n snap2



# Replica-1                Replica-2
#   s1                       s1
#   s2                       s2
assert_equal $( get_obj_count $VOL_NAME ) $((count1 + REP_CNT*2))

info "stop slave node $STORE_IP"
stop_pfs $STORE_IP
sleep 3
assert cp snap2.dat snap3.dat
assert dd if=/dev/urandom bs=4k          count=32 of=temp.dat
assert dd if=temp.dat     bs=4k  seek=64 count=32 conv=nocreat,notrunc of=snap3.dat
assert pfdd --count 32 --rw write --bs 4k -v $VOL_NAME --if temp.dat --offset=$((64*4096))


# Replica-1                Replica-2 [OFFLINE]
#   s1                       [s1]
#   s2                       [s2]
#   s3
assert_equal $(obj_cnt_on_ip $PRIMARY_IP) $((primary_cnt + 3))

# case 1: snapshot deleted during one node fault
assert pfcli delete_snapshot -v $VOL_NAME -n snap2

# Replica-1                Replica-2 [OFFLINE]
#   s1                       [s1]
#                            [s2]
#   s3 <HEAD>
sleep 2
assert_equal $(obj_cnt_on_ip $PRIMARY_IP) $((primary_cnt + 2))

info "check volume status should DEGRADED"
assert_equal $(query_db "select status from t_volume where name='$VOL_NAME'") "DEGRADED"

set -o pipefail

SNAP3_MD5=$(dd if=snap3.dat bs=4k count=512  | md5sum -b)
S3_MD5=$(assert pfdd --count 512 --rw read --bs 4k -v $VOL_NAME --of /dev/stdout | md5sum -b)
#assert_equal "${PIPESTATUS[0]}" "0"
assert_equal "$SNAP3_MD5" "$S3_MD5"


# case 2: new snapshot created and new object formed during one node fault
assert "fio --enghelp | grep pfbd "
fio -name=test -ioengine=pfbd -volume=$VOL_NAME -iodepth=1  -rw=randwrite -size=64M -bs=4k -direct=1 -time_based -runtime=60 &
FIO_PID=$!
sleep 10 #wait fio to start

assert pfcli create_snapshot -v $VOL_NAME -n snap3
sleep 2
# Replica-1                Replica-2 [OFFLINE]
#   s1                       [s1]
#                            [s2]
#   s3
#   s4 <HEAD>

assert_equal $(obj_cnt_on_ip $PRIMARY_IP) $((primary_cnt + 3))

info "start slave node $STORE_IP"
start_pfs $STORE_IP #start pfs
sleep 3

SLAVE_STATUS=$(query_db "select status from t_store where mngt_ip='$STORE_IP'" )
assert_equal "$SLAVE_STATUS" "OK"
assert_equal $(obj_cnt_on_ip $STORE_IP) $((slave_cnt + 2))

assert async_curl "http://$COND_IP:49180/s5c/?op=recovery_volume&volume_name=$VOL_NAME"

assert_equal $(query_db "select status from t_volume where name='$VOL_NAME'") "OK"
sleep 3
# Replica-1                Replica-2
#   s1                       s1
#                             
#   s3                       s3
#   s4                       s4 <HEAD>
assert_equal $(obj_cnt_on_ip $STORE_IP) $((slave_cnt + 3))
if  pidof fio | grep $FIO_PID ; then
    info "FIO still running, that's OK, kill it"
    kill -INT $FIO_PID
	wait $FIO_PID
else
    assert wait $FIO_PID
fi
sleep 3

info "Begin scrub volume"
assert async_curl "http://$COND_IP:49180/s5c/?op=deep_scrub_volume&volume_name=$VOL_NAME"


SNAP1_MD5=$(dd if=snap1.dat bs=4k count=512  | md5sum -b)
S1_MD5=$(assert pfdd --count 512 --rw read --bs 4k -v $VOL_NAME --snapshot snap1 --of /dev/stdout | md5sum -b)
#assert_equal "${PIPESTATUS[0]}" "0"
assert_equal "$SNAP1_MD5" "$S1_MD5"


info "Test OK"

