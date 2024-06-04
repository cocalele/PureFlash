DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
source $DIR/utils.sh

HOST_IP=($1 $2 $3)


declare -A  VDISKS
declare -A  FIOPIDS

VM_PER_HOST=2
DISK_PER_VM=1

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






TIME=60
JOBS=1
IODEPTH=32
RW_OP=randwrite
BLK_SZ=4k

#trap cleanup 2

FIO_CNT=0

for ((i=0;i<${#HOST_IP[@]};i++)); do
	for ((j=0;j<VM_PER_HOST;j++)); do
		info "Start fio$FIO_CNT on VM$i ..."
		sshpass -p123456 ssh root@${HOST_IP[i]} podman exec -t pfs  \
		     /opt/pureflash/fio -ioengine=pfbd -volume=vol_h${i}_v${j}_d0 -size=100G -direct=1 -iodepth=$IODEPTH -thread -rw=$RW_OP  -bs=$BLK_SZ -numjobs=$JOBS -runtime=$TIME -group_reporting -name=randw0 -time_based -ramp_time=20 > vol_h${i}_v${j}_d0.log &
		FIOPIDS[$FIO_CNT]=$!
		info "FIO[$FIO_CNT] pid is ${FIOPIDS[$FIO_CNT]} "
		FIO_CNT=$((FIO_CNT+1))

	done
done
# rpc.py -s `pwd`/vhost-liu.sock  framework_set_scheduler static

info "Waiting fio jobs complete ..."
for ((i=0;i<FIO_CNT;i++)); do
	verify wait ${FIOPIDS[$i]}
done


info "\n===================All IOPS(rw=$RW_OP, bs=$BLK_SZ)========================= "
grep iops vol_h?_v?_d?.log
grep iops vol_h?_v?_d?.log | awk -F[=,] '{sum+=$6}END{printf "Total: %d IOPS\n",  sum;}'
grep -E ' lat.*avg'   vol_h?_v?_d?.log
grep -E ' lat.*avg'   vol_h?_v?_d?.log | awk -F[=,] '{sum+=$6;cnt+=1}END{printf "Avg latency: %d \n",  sum/cnt;}'
