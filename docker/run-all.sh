#!/bin/bash
set -m

DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"

JAVA_HOME=/usr/lib/jvm/jdk-15/
export PATH=/opt/pureflash:$JAVA_HOME/bin:$PATH

echo "Start MariaDB..."
mysql_install_db --user=mysql --ldata=/var/lib/mysql
/usr/bin/mysqld_safe --user=mysql --datadir='/var/lib/mysql' --log-error=/var/log/mysql/error.log &
status=$?
if [ $status -ne 0 ]; then
  echo "Failed to start mysql: $status"
  exit $status
fi

if [ ! -f /opt/pureflash/disk1.dat ]; then
  echo "Create disk file ..."
  truncate -s 20G /opt/pureflash/disk1.dat
fi
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
echo "Start PureFlash jconductor..."
JCROOT=$DIR/jconductor
$JAVA_HOME/bin/java  -classpath $JCROOT:$JCROOT/lib/*  -Dorg.slf4j.simpleLogger.showDateTime=true -Dorg.slf4j.simpleLogger.dateTimeFormat="[yyyy/MM/dd H:mm:ss.SSS]" com.netbric.s5.conductor.Main -c /etc/pureflash/pfc.conf &> /var/log/pfc.log &
status=$?
if [ $status -ne 0 ]; then
  echo "Failed to start jconductor: $status"
  exit $status
fi

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

bash

