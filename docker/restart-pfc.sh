#!/bin/bash
set -m

DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"

#JAVA_HOME=/opt/pureflash/jdk-17.0.6
export PATH=/opt/pureflash:$PATH

OLD_PID=$(ps -f |grep jconductor |grep java|awk '{print $2}')
if [ "$OLD_PID" != "" ]; then
	echo "OLD_PID:$OLD_PID "
	kill -2 $OLD_PID
fi

echo "Restart PureFlash jconductor..."
JCROOT=$DIR/jconductor
nohup java  -classpath $JCROOT/pfconductor.jar:$JCROOT/lib/*  \
   -Dorg.slf4j.simpleLogger.showDateTime=true \
   -Dorg.slf4j.simpleLogger.dateTimeFormat="[yyyy/MM/dd H:mm:ss.SSS]" \
   -XX:+HeapDumpOnOutOfMemoryError \
   -Xmx2G \
   com.netbric.s5.conductor.Main -c /etc/pureflash/pfc.conf > /var/log/pfc.log 2>&1 &
status=$?
if [ $status -ne 0 ]; then
  echo "Failed to start jconductor: $status"
  exit $status
fi


