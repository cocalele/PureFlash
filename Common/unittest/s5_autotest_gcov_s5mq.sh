#!/bin/sh

function check_process()
{
    while true
    do
        echo "Testind s5mq msg send"
        ps -ef|grep $1 |grep -v grep
        if [ $? -ne 0 ]
        then
            echo "Finish test $2"
            break;
        fi
        sleep 1
    done
}

./test_cndct 0  > test_cndct_send.log &
sleep 1
./test_worker 0 > test_worker_send.log &

check_process "test_cndct" "s5mq message send" 
check_process "test_worker" "s5mq message send"


./test_cndct 1  > test_cndct_send.log &
sleep 1
./test_worker 1 > test_worker_send.log &

check_process "test_cndct" "s5mq message asend"
check_process "test_worker" "s5mq message asend"

./test_cndct 2  > test_cndct_send.log &
sleep 1
./test_worker 0 > test_worker_send.log &
check_process "test_cndct" "s5mq message send with no data"
check_process "test_worker" "s5mq message send with no data"
