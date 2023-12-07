SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
DIR=$(pwd)

function fatal {
    echo -e "\033[31m[`date`] $* \033[0m"
    exit 1
}
function info {
    echo -e "\033[32m[`date`] $* \033[0m"
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


info "This build script need run in pureflash develop container."
info "Start container with command:\n   docker run -ti --rm --network host  docker.io/pureflash/pureflash-dev:1.8 /bin/bash"
#give time to read above tips
sleep 2

set -v


export JAVA_HOME=/usr/lib/jvm/jdk-15 
export PATH=$JAVA_HOME/bin/:$PATH

info "build jconductor"
git clone https://gitee.com/cocalele/jconductor.git
cd jconductor/
git submodule update --init
ant -f jconductor.xml 

info "build PureFlash"
cd $DIR
git clone https://gitee.com/cocalele/PureFlash.git
cd PureFlash/
git submodule update --init
mkdir build
cd build
cmake -GNinja -DCMAKE_BUILD_TYPE=Debug ..
ninja

info "build fio with pfbd"
cd $DIR
git clone https://gitee.com/cocalele/fio.git
cd fio
#./configure --pfbd-include=$DIR/PureFlash/common/include --pfbd-lib=$DIR/PureFlash/build/bin
./configure --pfbd-include=$DIR/PureFlash/common/include --pfbd-lib=$DIR/PureFlash/build/bin/ --spdk-lib=$DIR/PureFlash/thirdParty/spdk/build/lib --dpdk-lib=$DIR/PureFlash/thirdParty/spdk/dpdk/build/lib
make

cd $DIR
info "build qemu with pfbd"
git clone https://gitee.com/cocalele/qemu.git
cd qemu
PUREFLASH_HOME=$DIR/PureFlash
OSNAME=`$DIR/PureFlash/scripts/osname.sh`
mkdir /usr/include/pfbd
cp -f ${PUREFLASH_HOME}/common/include/pf_client_api.h /usr/include/pfbd/pf_client_api.h
cp -f ${PUREFLASH_HOME}/build/bin/libs5common.a /usr/lib/libs5common.a
cp -f $DIR/PureFlash/pre_build_libs/$OSNAME/libzookeeper_mt.a /usr/lib/libzookeeper.a
cp -f $DIR/PureFlash/pre_build_libs/$OSNAME/libhashtable.a /usr/lib/libhashtable.a
mkdir build; cd build; ../configure  --enable-debug --enable-kvm  --target-list=x86_64-softmmu  --disable-linux-io-uring && ninja

info "Begin build docker"
cd $DIR/PureFlash/docker
./build-docker.sh $DIR/jconductor $DIR/PureFlash/build $DIR/qemu/build $DIR/fio