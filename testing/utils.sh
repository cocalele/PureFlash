#!/bin/bash
function fatal {
    echo -e "\033[31m$* \033[0m [line: ${BASH_LINENO[-2]}]"
    exit 1
}
function info {
    echo -e "\033[32m$* \033[0m"
}

function assert()
{
    local cmd=$*
	echo "Run:$cmd" > /dev/stderr
	eval '${cmd}'
    if [ $? -ne 0 ]; then
        fatal "Failed to run:$cmd"
    fi
}

function assert_equal()
{
    if [ "$1" != "$2" ]; then
        fatal "Assert fail, $1 != $2, $3"
    fi
}

function assert_not_eq()
{
    if [ "$1" == "$2" ]; then
        fatal "Assert fail, $1 != $2, $3"
    fi
}

function assert_fail()
{
    local cmd=$*
	echo "Run:$cmd" > /dev/stderr
	eval '${cmd}'
    if [ $? -eq 0 ]; then
        fatal "Failed to run:$cmd"
    fi
}

function curlex () {
    echo "curl $@"
    rsp=$(curl --write-out '\n%{http_code}\n'  "$@" 2>/dev/null)
    code=$(echo "$rsp" | tail -n 1)
    ret=$(echo "$rsp" | head -n -1 | jq -r ".ret_code")
    echo "$rsp, $ret"
    if [ ! $ret ];then
        ret=0
    fi
    if (( $code >= 400 )); then
        return 22
    elif (( $ret != 0 )); then
        return 22
    else
        return 0
    fi
}


function query_db () {
    mysql -h$DB_IP -u$DB_USER -p$DB_PASS $DB_NAME -B --disable-column-names -e "$*"
}

function get_obj_count() {
    total=0
    store_ip=$(query_db "select mngt_ip from t_store where id in (select store_id from v_replica_ext where volume_name='$1') and status='OK'")
    for ip in $store_ip; do
        cnt=$(curl "http://$ip:49181/debug?op=get_obj_count")
        total=$(($total + $cnt))
    done
    echo $total
}
function obj_cnt_on_ip(){
	curl "http://$1:49181/debug?op=get_obj_count"
}

async_curl() {
	echo "curl $@"
    rsp=$(curl --write-out '\n%{http_code}\n'  "$@" 2>/dev/null)
	code=$(echo "$rsp" | tail -n 1)
	ret=$(echo "$rsp" | head -n -1 | jq -r ".ret_code")
	echo "$rsp, $ret"
	if [ ! $ret ];then
		ret=0
	fi
	if (( $code >= 400 )); then
		return 22
	fi
	if (( $ret != 0 )); then
		return 22
	fi
	task_id=$(echo "$rsp" | head -n -1 | jq -r ".task_id")
	echo "task_id:$task_id"

	while sleep 5 ; do
		rsp=$(curl --write-out '\n%{http_code}\n'  "http://$(pfcli get_pfc):49180/s5c/?op=query_task&task_id=$task_id" 2>/dev/null)
		echo $rsp
		status=$(echo "$rsp" | head -n -1 | jq -r ".task.status")
		echo $status
		if [ "$status" == "WAITING" ] ; then
			continue
		elif [ "$status" == "RUNNING" ] ; then
			continue
		elif [ "$status" == "FAILED" ] ; then
		    echo "$rsp" | head -n -1 | jq -r ".reason"
			return -2
		elif [ "$status" == "SUCCEEDED" ] ; then
			return 0
		fi
	done
	return -1
}
if [ ! -v SSH_PORT ]; then
    export SSH_PORT=22
fi

SSH_CMD="ssh -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -p $SSH_PORT"

function stop_pfs(){
    assert $SSH_CMD root@$1 pkill pfs
	#ssh root@$STORE_IP supervisorctl stop pfs
	#assert ssh root@$1  podman exec pfs-run pkill pfs
}

function start_pfs(){
    assert $SSH_CMD root@$1 /opt/pureflash/restart-pfs.sh
	#ssh root@$STORE_IP supervisorctl start pfs
	#assert ssh root@$1 podman exec pfs-run /opt/pureflash/restart-pfs.sh
}

function get_rep_count(){
    NODE_CNT=$(pfcli list_store |grep OK |wc -l)
    if ((NODE_CNT < 2 )) ;  then 
        fatal "At least 2 nodes are required"
    fi
    if  ((NODE_CNT < 3 )) ;  then
        echo "2"
    else
        echo 3
    fi
}