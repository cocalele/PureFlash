DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
source $DIR/utils.sh

HOST_IP=($1 $2 $3)

declare -A  VDISKS
declare -A  FIOPIDS


VOL_SIZE=128G


VM_CNT=${#HOST_IP[@]}

cleanup()
{
	info "kill fio"
	for ((i=0;i<VM_CNT;i++)); do
		for ((j=0;j<4;j++)) do
			kill -s 2 ${FIOPIDS[$((i*4+j))]}
		done
	done
	info "Wait VM to clean exit"
	for ((i=0;i<VM_CNT;i++)); do
		sshpass -p123456 ssh root@localhost -p $((20022+i)) poweroff
	done

	sleep 5
	for ((i=0;i<VM_CNT;i++)); do
		wait ${VMPIDS[$i]}
	done


	exit
}

function pfcli()
{
	echo "pfcli $*"
	sshpass -p123456 ssh root@10.255.87.37 podman exec 58d3616ada03 /opt/pureflash/pfcli $*
}



# recreate_all_volume

#podman run -ti -d --rm --name vm_pod -v /home/test/conf-2:/etc/pureflash -v /dev/kvm:/dev/kvm -v /home/test:/root -v /home/vm-centos8:/vm-centos8 --ulimit core=-1 --privileged -e TZ=Asia/Shanghai --network host docker.io/pureflash/pureflash-dev:1.8-virsh /bin/bash

TEST_TYPE="READ_4K"
if [ "$TEST_TYPE" == "READ_4K" ]; then
	VM_PER_HOST=10
	DISK_PER_VM=1
	TIME=180
	BLK_SZ=4k
	JOBS=2
	IODEPTH=48
	RW_OP=" -rw=randread "
elif  [ "$TEST_TYPE" == "WRITE_4K" ]; then
	VM_PER_HOST=4
	DISK_PER_VM=1
	TIME=180
	BLK_SZ=4k
	JOBS=2
	IODEPTH=48
	RW_OP=" -rw=randwrite "
elif  [ "$TEST_TYPE" == "MIX_4K" ]; then
	VM_PER_HOST=10
	DISK_PER_VM=1
	TIME=180
	BLK_SZ=4k
	JOBS=2
	IODEPTH=48
	RW_OP=" -rw=randrw -rwmixread=70 "
elif [ "$TEST_TYPE" == "READ_128K" ]; then
	VM_PER_HOST=8
	DISK_PER_VM=1
	TIME=180
	BLK_SZ=128k
	JOBS=1
	IODEPTH=32
	RW_OP=" -rw=randread "
elif  [ "$TEST_TYPE" == "WRITE_128K" ]; then
	VM_PER_HOST=8
	DISK_PER_VM=1
	TIME=180
	BLK_SZ=128k
	JOBS=1
	IODEPTH=32
	RW_OP=" -rw=randwrite "
elif  [ "$TEST_TYPE" == "MIX_128K" ]; then
	VM_PER_HOST=10
	DISK_PER_VM=1
	TIME=180
	BLK_SZ=128k
	JOBS=1
	IODEPTH=32
	RW_OP=" -rw=randrw -rwmixread=50 "	
else
	fatal "Invalid test type"
fi



for ((i=0;i<${#HOST_IP[@]};i++)); do
	for ((j=0;j<VM_PER_HOST;j++)); do		
		sshpass -p123123 ssh -o 'StrictHostKeyChecking no'  -o 'UserKnownHostsFile /dev/null' root@${HOST_IP[i]} -p $((20022+j))  poweroff
	done
done
sleep 10


for ((i=0;i<${#HOST_IP[@]};i++)); do
#
	for ((j=0;j<VM_PER_HOST;j++)); do
		sshpass -p123456 ssh root@${HOST_IP[i]}  podman  kill  vm_pod_$j
		sshpass -p123456 ssh root@${HOST_IP[i]}  podman run -t -d --rm --name vm_pod_$j -v /home/test/conf-1:/etc/pureflash -v /dev/kvm:/dev/kvm -v /home/test:/root -v /home/vm-centos8:/vm-centos8 --ulimit core=-1 --privileged -e TZ=Asia/Shanghai --network host docker.io/pureflash/pureflash-dev:1.8-virsh /bin/bash

		sshpass -p123456 ssh root@${HOST_IP[i]} podman exec vm_pod_$j /root/v2/testing/startvm.sh vm$j $((20022+j)) vol_h${i}_v${j}_d0
		#assert sshpass -p123123 ssh -o "StrictHostKeyChecking no"  -o "UserKnownHostsFile /dev/null" root@${HOST_IP[i]} -p $((20022+j))  date
	done
done

info "Wait 30s for VM starting.."
sleep 30
for ((i=0;i<${#HOST_IP[@]};i++)); do
	for ((j=0;j<VM_PER_HOST;j++)); do		
		
		sshpass -p123123 ssh -o 'StrictHostKeyChecking no'  -o 'UserKnownHostsFile /dev/null' root@${HOST_IP[i]} -p $((20022+j))  lsblk /dev/vdb
		if (($? == 0)); then
			info "VM h${i}_v${j} OK"
		else
			fatal "VM h${i}_v${j} NOT ready"
		fi	
	done
done
info "ALL VM OK"





#trap cleanup 2

FIO_CNT=0

for ((i=0;i<${#HOST_IP[@]};i++)); do
	for ((j=0;j<VM_PER_HOST;j++)); do
		info "Start fio$FIO_CNT on VM$i ..."
		sshpass -p123123 ssh -o "StrictHostKeyChecking no"  -o "UserKnownHostsFile /dev/null" root@${HOST_IP[i]} -p $((20022+j))  \
		     fio -filename=/dev/vdb -size=100G -direct=1 -iodepth=$IODEPTH -thread $RW_OP -ioengine=libaio -bs=$BLK_SZ -numjobs=$JOBS -runtime=$TIME -group_reporting -name=randw0 -time_based -ramp_time=20 > vol_h${i}_v${j}_d0.log &
		FIOPIDS[$FIO_CNT]=$!
		info "FIO[$FIO_CNT] pid is ${FIOPIDS[$FIO_CNT]} "
		FIO_CNT=$((FIO_CNT+1))

	done
done


#sleep 30
#info "statistics CPU usage..."
#
##  ps -C pfs -o '%cpu='
#
#declare -A CPU_COST
#for ((t=0;t<5;t++));do
#	for ((i=0;i<${#HOST_IP[@]};i++)); do
#		CPU_COST[$i,$t]=$(sshpass -p123456 ssh -o "StrictHostKeyChecking no"  -o "UserKnownHostsFile /dev/null" root@${HOST_IP[i]} <<EOF
#		top -b -d 1 -n 2 -p $(pidof pfs) | tail -n 1 |awk '{print $9}'
#EOF
#		)
#	done
#	sleep 10
#done

info "Waiting fio jobs complete ..."
for ((i=0;i<FIO_CNT;i++)); do
	verify wait ${FIOPIDS[$i]}
done

info "\n===================All IOPS(rw=$RW_OP, bs=$BLK_SZ)========================= "
#grep iops vol_h?_v?_d?.log
grep -E 'bw.*avg' vol_h?_v?_d?.log 
grep iops vol_h?_v?_d?.log | awk -F[=,] '{sum+=$6}END{printf "Total: %d IOPS\n",  sum;}'
grep -E 'bw.*avg' vol_h?_v?_d?.log | awk -F[=,] '{sum+=$8}END{printf "Total: %d  bandwidth\n",  sum;}'
#grep -E ' lat.*avg'   vol_h?_v?_d?.log
grep -E ' lat.*avg'   vol_h?_v?_d?.log | awk -F[=,] '{sum+=$6;cnt+=1}END{printf "Avg latency: %d \n",  sum/cnt;}'

info "Test params: 	VM_PER_HOST=$VM_PER_HOST
	DISK_PER_VM=$DISK_PER_VM
	BLK_SZ=$BLK_SZ
	JOBS=$JOBS
	IODEPTH=$IODEPTH
	RW_OP=$RW_OP"
#for ((i=0;i<${#HOST_IP[@]};i++)); do
#	for ((t=1;t<5;t++));do
#		CPU_COST[$i,0]=$(awk "BEGIN { print ${CPU_COST[$i,0]}+${CPU_COST[$i,$t]} }")
#	done
#	info "${HOST_IP[i]} CPU: " $(awk "BEGIN { print ${CPU_COST[$i,0]}/5 }")
#done 
