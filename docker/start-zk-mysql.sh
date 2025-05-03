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

echo "Zk start ok"

