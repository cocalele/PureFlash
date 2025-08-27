#!/bin/bash
set -m

DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"

#JAVA_HOME=/opt/pureflash/jdk-17.0.6
#export PATH=/opt/pureflash:$JAVA_HOME/bin:$PATH
export PATH=/opt/pureflash:$PATH

echo "Start MariaDB..."
mysql_install_db --user=mysql --ldata=/var/lib/mysql
/usr/bin/mysqld_safe --user=mysql --datadir='/var/lib/mysql' --log-error=/var/log/mysql/error.log &
status=$?
if [ $status -ne 0 ]; then
  echo "Failed to start mysql: $status"
  exit $status
fi
if [ "$PFS_DISKS" != "" ]; then
	echo "Use disk $PFS_DISKS specified from environment variable PFS_DISKS";
else
	echo "Use data file /opt/pureflash/disk1.dat as disk, only for testing"
	if [ ! -f /opt/pureflash/disk1.dat ]; then
	  echo "Create disk file ..."
	  truncate -s 20G /opt/pureflash/disk1.dat
	fi
	export PFS_DISKS="/opt/pureflash/disk1.dat"
fi
	
i=0
for d in ${PFS_DISKS//,/ }; do
	sed -i "/__TRAY_PLACEHOLDER__/i [tray.$i]\n\tdev = $d" /etc/pureflash/pfs.conf
	i=$((i+1))
done
 sed -i "/__TRAY_PLACEHOLDER__/d" /etc/pureflash/pfs.conf

echo "Waiting mysql start ..."
sleep 3
if  ! mysql -e "use s5" ; then
  echo "initialize database s5 ..."
  mysql -e "source /opt/pureflash/mariadb/init_s5metadb.sql"
  mysql -e "GRANT ALL PRIVILEGES ON *.* TO 'pureflash'@'%' IDENTIFIED BY '123456'" 
fi


echo "Start Zookeeper..."
ZK_HOME=$DIR/apache-zookeeper-3.5.9-bin
$ZK_HOME/bin/zkServer.sh start
status=$?
if [ $status -ne 0 ]; then
  echo "Failed to start zookeeper: $status"
  exit $status
fi
sleep 2
while !  lsof -i -P -n | grep 2181  ; do echo waiting zk; sleep 1; done
echo "Start PureFlash jconductor..."
JCROOT=$DIR/jconductor
java  -classpath $JCROOT/pfconductor.jar:$JCROOT/lib/*  \
   -Dorg.slf4j.simpleLogger.showDateTime=true \
   -Dorg.slf4j.simpleLogger.dateTimeFormat="[yyyy/MM/dd H:mm:ss.SSS]" \
   -XX:+HeapDumpOnOutOfMemoryError \
   -Xmx2G \
   com.netbric.s5.conductor.Main -c /etc/pureflash/pfc.conf &> /var/log/pfc.log &
status=$?
if [ $status -ne 0 ]; then
  echo "Failed to start jconductor: $status"
  exit $status
fi
sleep 3
ulimit -c unlimited
echo "/var/crash/core-%p-%e-%t" > /proc/sys/kernel/core_pattern

export LD_LIBRARY_PATH=$DIR:$LD_LIBRARY_PATH
echo "Start PureFlash store..."
$DIR/pfs -c /etc/pureflash/pfs.conf &> /var/log/pfs.log &
status=$?
if [ $status -ne 0 ]; then
  echo "Failed to start pfs: $status"
  exit $status
fi

echo -n "Waiting disk to be initialized for first run, this may take 60 seconds or longer accord to you disk ..."
while ! pfcli list_disk &>/dev/null ; do
	sleep 1
	echo -n "."
done
echo "Disk ready"

echo "Welcome to PureFlash(https://github.com/cocalele/PureFlash) all-in-one box!"
cd
if [ "$NOBASH" == "" ] ; then
	bash
fi


