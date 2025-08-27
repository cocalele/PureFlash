#!/bin/bash
DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
source $DIR/utils.sh

VOL_NAME=test_3
VOL_SIZE=$((5<<30)) #5G on my testing platform
COND_IP=$(pfcli get_pfc)
read DB_IP DB_NAME DB_USER DB_PASS <<< $(assert pfcli get_conn_str)
export DB_IP DB_NAME DB_USER DB_PASS
REP_CNT=$(get_rep_count)

pfcli delete_volume  -v $VOL_NAME
assert pfcli create_volume  -v $VOL_NAME -s $VOL_SIZE -r $REP_CNT
assert "fio --enghelp | grep pfbd "
fio -name=test -ioengine=pfbd -volume=$VOL_NAME -iodepth=1  -rw=randwrite -size=$VOL_SIZE -bs=4k -direct=1 &
FIO_PID=$!
sleep 10 #wait fio to start

PRIMARY_IP=$(query_db "select mngt_ip from t_store where id in (select store_id from v_replica_ext where is_primary=1 and volume_name='$VOL_NAME') limit 1")
info "Primary node is:$PRIMARY_IP"

STORE_IP=$(query_db "select mngt_ip from t_store where id in (select store_id from v_replica_ext where is_primary=0 and volume_name='$VOL_NAME') limit 1")
info "stop slave node $STORE_IP"
stop_pfs  $STORE_IP #stop pfs
sleep 3

info "check volume status should DEGRADED"
assert_equal $(query_db "select status from t_volume where name='$VOL_NAME'") "DEGRADED"

info "start slave node $STORE_IP"
start_pfs $STORE_IP #start pfs
sleep 3

assert async_curl "http://$COND_IP:49180/s5c/?op=recovery_volume&volume_name=$VOL_NAME"

assert_equal $(query_db "select status from t_volume where name='$VOL_NAME'") "OK"
sleep 3

if  pidof fio | grep $FIO_PID ; then
    info "FIO still running, that's OK, kill it"
    kill -INT $FIO_PID
else
    assert wait $FIO_PID
fi
info "Test OK"
