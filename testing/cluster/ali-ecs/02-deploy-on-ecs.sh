#!/bin/bash
# 本脚本可以在阿里云环境上进行PureFlash的测试，完成如下的步骤：
# 1. 在aliyun上创建3台虚拟机作为测试集群
# 2. 下载代码(master分支）并编译，利用build镜像做编译，但是把编译结果保留在云服务器上
# 3. 以容器镜像启动服务，构建集群
# 4. 执行tests下面的测试脚本

### 注意： 执行测试后本脚本不会自动删除阿里云虚机，为的是方便调试诊断问题。请根据使用情况自行删除虚机已避免不必要的开销。

# 在运行本脚本前，需要在环境变量里指定aliyun的AK/SK， 用以访问云服务创建测试虚机
# 比如：
#      export ALIBABA_CLOUD_ACCESS_KEY_ID=your_access_key_id
#      export ALIBABA_CLOUD_ACCESS_KEY_SECRET=your_access_key_secret
#


DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
source $DIR/../../utils.sh
shopt -s expand_aliases

 #aliyun ecs DescribeInstances --RegionId $REGION --output cols=InstanceId,StartTime,PublicIpAddress.IpAddress[0],VpcAttributes.PrivateIpAddress.IpAddress[0]  rows=Instances.Instance[] num=true
REGION=cn-wulanchabu 
#REGION=cn-hongkong

declare -A IPs
i=0
for ip in $(aliyun ecs DescribeInstances --RegionId $REGION --output cols=PublicIpAddress.IpAddress[0]  rows=Instances.Instance[] num=false | tail  -n +3 | tac | sed '/^$/d'); do
	IPs[$i]=$ip
	i=$((i+1))
done
echo ${IPs[*]}


declare -A PrivIPs
i=0
for ip in $(aliyun ecs DescribeInstances --RegionId $REGION --output cols=VpcAttributes.PrivateIpAddress.IpAddress[0]  rows=Instances.Instance[] num=false | tail  -n +3 | tac |  sed '/^$/d'); do
	PrivIPs[$i]=$ip
	i=$((i+1))
done
echo ${PrivIPs[*]}


##服务器配置，每行代表一个服务器，分别为 ip地址， root密码， 数据盘符
##       IP      password     data_disk 
#arrA=(	"11	    pfstesT@123    /dev/vdb"
#		"21	    pfstesT@123    /dev/vdb"
#	)
#
#for ((i=0;i<${#arrA[@]};i++)); do
# read  IP PASS DEV <<< ${arrA[i]}
# echo "IP:$IP PASS:$PASS DEV:$DEV"
#done

export SSHPASS="pfstesT@123"

#function myssh(){
#	sshpass -p $2 ssh -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o ServerAliveInterval=60 -o ServerAliveCountMax=120 $1
#}
alias myssh2='sshpass -e ssh -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -l root'

ZOOKEEPER_IP=${PrivIPs[0]}
DB_IP=${PrivIPs[0]}



function install_software(){
	SRV_IP=$1
	THISIP=$2
	myssh2 root@$SRV_IP <<EOF
set -xv
function assert()
{
    local cmd=\$*
	eval '\${cmd}'
    if [ "\$?" != "0" ]; then
        echo "Failed to run:\$cmd"
		exit 1
    fi
}
assert apt -y update
assert apt install -y podman sshpass curl nfs-server
assert mkdir -p /root/v2
assert mkdir -p /etc/pureflash

cat >> /etc/containers/registries.conf <<EEEF
unqualified-search-registries = ["docker.io"]

[[registry]]
prefix = "docker.io"
insecure = false
blocked = false
location = "docker.io"
[[registry.mirror]]
location = "docker.chenby.cn"
EEEF

cat > /etc/pureflash/pfs.conf <<EEEF
[cluster]
name=cluster1
[zookeeper]
ip=$ZOOKEEPER_IP:2181

[afs]
	mngt_ip=$THISIP
	id=$(echo $THISIP | awk -F. '{print $NF}')
	meta_size=10737418240
[engine]
        name=aio
[tray.0]
   dev = /dev/vdb
[port.0]
   ip=$THISIP
[rep_port.0]
   ip=$THISIP
[tcp_server]
   poller_count=8
[replicator]
   conn_type=tcp
   count=4
EEEF

cat > /etc/pureflash/pfc.conf <<EEEF
[cluster]
name=cluster1
[zookeeper]
ip=$ZOOKEEPER_IP:2181
[conductor]
mngt_ip=$THISIP
[db]
ip=$DB_IP
user=pureflash
pass=123456
db_name=s5
EEEF

cat > /etc/pureflash/pf.conf <<EEEF
[cluster]
name=cluster1
[zookeeper]
ip=$ZOOKEEPER_IP:2181
[db]
ip=$DB_IP
user=pureflash
pass=123456
db_name=s5
[client]
conn_type=tcp
EEEF

EOF
}




declare -A  INSTALL_PIDS
for ((i=0;i<${#IPs[@]};i++)); do
	info "====install softwar on ${IPs[$i]}"
	install_software ${IPs[$i]} ${PrivIPs[$i]}&
	INSTALL_PIDS[$i]=$!
done
for ((i=0;i<${#INSTALL_PIDS[@]};i++)); do
	wait ${INSTALL_PIDS[$i]}
	if [ "$?" != "0" ]; then
        fatal "Failed install software on ${IPs[$i]}"
    fi
done
CONTAINER_IMAGE_NAME="crpi-1c7udixchl3kflep.cn-wulanchabu.personal.cr.aliyuncs.com/pfs-liulele/pureflash-dev:1.9.2-x64"
RUNTIME_IMAGE_NAME="crpi-1c7udixchl3kflep.cn-wulanchabu.personal.cr.aliyuncs.com/pfs-liulele/pureflash:1.9.3-x64"

DO_BUILD=1

info "==== build pfs container image on ${IPs[0]}"
myssh2 root@${IPs[0]} <<EOF
function assert()
{
    local cmd=\$*
	eval '\${cmd}'
    if [ "\$?" != "0" ]; then
        echo "Failed to run:\$cmd"
		exit 1
    fi
}
if [ "$DO_BUILD" != "" ]; then
echo "Start build now"
echo "/root/v2 *(rw,no_root_squash)" > /etc/exports
assert systemctl restart nfs-server

cd /root/v2
rm -rf *
podman kill pfs-build &> /dev/null
assert wget https://gitee.com/cocalele/PureFlash/raw/master/docker/build-all.sh -O build-all.sh


#assert podman pull $CONTAINER_IMAGE_NAME
echo "Do build ..."
assert podman run -d  --ulimit core=-1 --privileged  --rm -v /etc/pureflash:/etc/pureflash -v /root/v2:/root/v2  --name pfs-build  -e TZ=Asia/Shanghai -e PFREPO=gitee $CONTAINER_IMAGE_NAME sleep infinity
assert podman cp /etc/apt/sources.list pfs-build:/etc/apt/sources.list
assert podman exec  pfs-build apt update

assert podman exec -e PFREPO=gitee -w /root/v2 pfs-build bash ./build-all.sh
assert podman kill pfs-build
else
echo "Skip build"
fi

#start a new containe to run
podman kill pfs-d &> /dev/null
assert podman run -d  --ulimit core=-1 --privileged  --rm -v /etc/pureflash:/etc/pureflash -v /root/v2:/root/v2 --network host --name pfs-d  -e TZ=Asia/Shanghai -e PFREPO=gitee $RUNTIME_IMAGE_NAME sleep infinity
assert podman exec  pfs-d cp -rpfu /root/v2/PureFlash/build/bin/* /opt/pureflash/
assert podman exec  pfs-d cp -rpfu /root/v2/jconductor/lib/* /opt/pureflash/jconductor/lib/
assert podman exec  pfs-d cp -rpfu /root/v2/jconductor/pfconductor.jar /opt/pureflash/jconductor/
assert podman exec  pfs-d cp -rpfu /root/v2/jconductor/pfcli /opt/pureflash/
assert podman exec  pfs-d cp -rpfu /root/v2/qemu/build/qemu-img /opt/pureflash/
assert podman exec  pfs-d cp -rpfu /root/v2/qemu/build/qemu-system-x86_64 /opt/pureflash/
assert podman exec  pfs-d cp -rpfu /root/v2/fio/fio /opt/pureflash/
assert podman exec -e PFREPO=gitee -e NOBASH=Y -w /opt/pureflash pfs-d ./start-zk-mysql.sh
assert podman exec -e PFREPO=gitee -e NOBASH=Y -w /opt/pureflash pfs-d ./restart-pfc.sh
assert podman exec -e PFREPO=gitee -e NOBASH=Y -w /opt/pureflash pfs-d ./restart-pfs.sh

EOF
assert_equal "$?" "0"

NODE0_NFS_IP=${PrivIPs[0]}
for ((i=1;i<${#IPs[@]};i++)); do
	info "====install softwar on ${IPs[$i]}"
	THISIP="${IPs[i]}"
myssh2 root@${IPs[$i]} <<EOF	
function assert()
{
    local cmd=\$*
	eval '\${cmd}'
    if [ "\$?" != "0" ]; then
        echo "Failed to run:\$cmd"
		exit 1
    fi
}
assert mount $NODE0_NFS_IP:/root/v2 /root/v2
#assert podman pull $RUNTIME_IMAGE_NAME
podman kill pfs-d &> /dev/null
assert podman run -d  --ulimit core=-1 --privileged  --rm -v /etc/pureflash:/etc/pureflash -v /root/v2:/root/v2 --network host --name pfs-d  -e TZ=Asia/Shanghai -e PFREPO=gitee $RUNTIME_IMAGE_NAME sleep infinity
assert podman exec  pfs-d cp -rpfu /root/v2/PureFlash/build/bin/* /opt/pureflash/
assert podman exec  pfs-d cp -rpfu /root/v2/jconductor/lib/* /opt/pureflash/jconductor/lib/
assert podman exec  pfs-d cp -rpfu /root/v2/jconductor/pfconductor.jar /opt/pureflash/jconductor/
assert podman exec  pfs-d cp -rpfu /root/v2/jconductor/pfcli /opt/pureflash/
assert podman exec  pfs-d cp -rpfu /root/v2/qemu/build/qemu-img /opt/pureflash/
assert podman exec  pfs-d cp -rpfu /root/v2/qemu/build/qemu-system-x86_64 /opt/pureflash/
assert podman exec  pfs-d cp -rpfu /root/v2/fio/fio /opt/pureflash/
assert podman exec -e PFREPO=gitee -e NOBASH=Y -w /opt/pureflash pfs-d ./restart-pfs.sh

EOF
	assert_equal "$?" "0"
	info "OK"
done

