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


rm -rf jconductor/com
mkdir jconductor
assert cp -rp ../../jconductor/out/production/jconductor/com jconductor/
assert cp -rp ../../jconductor/res/init_s5metadb.sql  mariadb/
assert cp -rp ../../jconductor/pfcli  .
assert cp -rp ../build_deb/bin/pfs .
assert cp -rp ../build_deb/bin/pfdd .
assert cp -rp ../../qemu/build/qemu-img .
assert cp -rp ../../fio/fio .

docker build .
