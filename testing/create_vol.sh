
VM_PER_HOST=12
DISK_PER_VM=1

VOL_SIZE=128G
HOST_IP=($1 $2 $3)

for ((i=0;i<${#HOST_IP[@]};i++)); do
	for ((j=0;j<VM_PER_HOST;j++)); do
		for ((k=0;k<DISK_PER_VM;k++)); do
			# pfcli delete_volume  -v vol_h${i}_v${j}_d${k}
			 pfcli create_volume  -v vol_h${i}_v${j}_d${k} -s $VOL_SIZE -r 2 --host_id=$((i+1))
		done	
	done
done
