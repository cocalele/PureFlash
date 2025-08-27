#!/bin/bash
#set -xv
DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
source $DIR/utils.sh

VOL_NAME=test_r3
VOL_SIZE=$((5<<30)) #5G on my testing platform
COND_IP=$(pfcli get_pfc)
read DB_IP DB_NAME DB_USER DB_PASS <<< $(assert pfcli get_conn_str)
export DB_IP DB_NAME DB_USER DB_PASS

pfcli delete_volume  -v $VOL_NAME
assert pfcli create_volume  -v $VOL_NAME -s $VOL_SIZE -r 3
query_db "select hex(replica_id), is_primary, store_id, tray_uuid from v_replica_ext where volume_name='$VOL_NAME'"

#assert "fio --enghelp | grep pfbd "
assert pfdd --rw write --if /dev/zero -v $VOL_NAME --bs 4k --count 10
PRIMARY_IP=$(query_db "select mngt_ip from t_store where id in (select store_id from v_replica_ext where is_primary=1 and volume_name='$VOL_NAME') limit 1")
read rep_id store_id tray_uuid store_ip <<< $(query_db "select replica_id , store_id , tray_uuid, s.mngt_ip from v_replica_ext, t_store s  where volume_name='$VOL_NAME' and is_primary=0 and store_id=s.id limit 1")

info "Primary node is:$PRIMARY_IP Slave node is:$store_ip"

#choose a target node
read target_uuid  target_id <<< $(query_db "select uuid, store_id  from t_tray where store_id not in (select store_id from v_replica_ext where volume_name='$VOL_NAME')")

query_db "select replica_id , store_id , tray_uuid from v_replica_ext  where volume_name='$VOL_NAME'" | grep $target_uuid
assert_not_eq $? 0

info "move replica $rep_id from store $store_id ssd $tray_uuid to store $target_id ssd $target_uuid"


info "check volume status should OK" 
assert_equal $(query_db "select status from t_volume where name='$VOL_NAME'") "OK"


assert async_curl "http://$COND_IP:49180/s5c/?op=move_volume&volume_name=$VOL_NAME&from_store=$store_id&from_ssd_uuid=$tray_uuid&target_store=$target_id&target_ssd_uuid=$target_uuid"

#now, volume should not lay on original ssd
query_db "select replica_id , store_id , tray_uuid from v_replica_ext  where volume_name='$VOL_NAME'" | grep $tray_uuid
assert_not_eq $? 0
 

info "check volume status should OK"
assert_equal $(query_db "select status from t_volume where name='$VOL_NAME'") "OK"

info "Test OK"
