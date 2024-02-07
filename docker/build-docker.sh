#!/bin/bash
function fatal {
    echo -e "\033[31m$* \033[0m"
    exit 1
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
COND_HOME=$1
PFS_BUILD=$2
QEMU_BUILD=$3
FIO_BUILD=$4

if [[ "$COND_HOME" == "" || "$PFS_BUILD" == ""  || "$QEMU_BUILD" == "" || "$FIO_BUILD" == "" ]]; then
    echo "Usage: build-docker.sh <JCONDUCTOR_DIR> <PFS_BUILD_DIR> <QEMU_BUILD_DIR> <FIO_BUILD_DIR>"
    exit 1;
fi

#COND_HOME=/root/v2/jconductor
#PFS_BUILD=/root/v2/ViveNAS/PureFlash/build

rm -rf jconductor/com
mkdir jconductor
assert cp -rp $COND_HOME/pfconductor.jar jconductor/
assert cp -rp $COND_HOME/lib jconductor/

assert tar xzf $COND_HOME/res/apache-zookeeper-3.5.9-bin.tar.gz 
assert mv apache-zookeeper-3.5.9-bin/conf/zoo_sample.cfg apache-zookeeper-3.5.9-bin/conf/zoo.cfg
assert cp -rp $COND_HOME/res/init_s5metadb.sql  mariadb/
assert cp -rp $COND_HOME/pfcli  .
assert cp -rp $PFS_BUILD/bin/pfs .
assert cp -rp $PFS_BUILD/bin/pfdd .
#assert cp -rp $QEMU_BUILD/qemu-img .
assert cp -rp $FIO_BUILD/fio .

PUREFLASH_HOME=../
assert cp -f $PUREFLASH_HOME/thirdParty/spdk/dpdk/build/lib/librte_eal.so.23 .
assert cp -f $PUREFLASH_HOME/thirdParty/spdk/dpdk/build/lib/librte_mempool.so.23 .
assert cp -f $PUREFLASH_HOME/thirdParty/spdk/dpdk/build/lib/librte_ring.so.23 .
assert cp -f $PUREFLASH_HOME/thirdParty/spdk/dpdk/build/lib/librte_bus_pci.so.23 .
assert cp -f $PUREFLASH_HOME/thirdParty/spdk/dpdk/build/lib/librte_kvargs.so.23 .
assert cp -f $PUREFLASH_HOME/thirdParty/spdk/dpdk/build/lib/librte_telemetry.so.23 .
assert cp -f $PUREFLASH_HOME/thirdParty/spdk/dpdk/build/lib/librte_pci.so.23 .


docker build -f Dockerfile -t pureflash/pureflash:1.9.1 .
