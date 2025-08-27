#!/bin/bash
DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
PUREFLASH_HOME=$(realpath $DIR/..)
tmpdir="deps-`$DIR/osname.sh`"
mkdir $tmpdir

 
cp ${PUREFLASH_HOME}/common/include/pf_client_api.h $tmpdir/pf_client_api.h
cp ${PUREFLASH_HOME}/build/bin/libs5common.a $tmpdir/libs5common.a
cp ${PUREFLASH_HOME}/build/bin/libzookeeper_mt.a $tmpdir/libzookeeper_mt.a
cp ${PUREFLASH_HOME}/build/bin/libhashtable.a $tmpdir/libhashtable.a
( cd ${PUREFLASH_HOME}/thirdParty/spdk/build/lib; cp libspdk_rpc.a libspdk_nvme.a libspdk_env_dpdk.a libspdk_util.a libspdk_log.a libspdk_sock.a libspdk_trace.a  libspdk_json.a libspdk_jsonrpc.a  $DIR/$tmpdir)
( cd ${PUREFLASH_HOME}/thirdParty/spdk/dpdk/build/lib; cp -rp librte_eal.a librte_mempool.a librte_ring.a librte_telemetry.a librte_kvargs.a librte_pci.a librte_bus_pci.a librte_mempool_ring.a $DIR/$tmpdir )

tar czf pureflash-$tmpdir.tgz $tmpdir
