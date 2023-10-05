FROM pureflash/ubuntu-base:22.04
LABEL version="1.8.3"

RUN mkdir /etc/pureflash
COPY conf/pfc.conf /etc/pureflash/pfc.conf
COPY conf/pfs.conf /etc/pureflash/pfs.conf
COPY conf/pfc.conf /etc/pureflash/pf.conf
COPY apache-zookeeper-3.5.9-bin /opt/pureflash/apache-zookeeper-3.5.9-bin
RUN rm -rf /tmp/zookeeper
RUN mkdir /tmp/zookeeper
RUN mkdir /var/crash

RUN rm -rf /opt/pureflash/jconductor
COPY jconductor /opt/pureflash/jconductor
#RUN mv /opt/pureflash/jconductor/pfconductor.jar /opt/pureflash/pfconductor.jar

RUN rm -rf /opt/pureflash/mariadb
COPY mariadb /opt/pureflash/mariadb

COPY mariadb/mariadb.cnf /etc/mysql/mariadb.cnf
COPY mariadb/50-server.cnf /etc/mysql/mariadb.conf.d/50-server.cnf
COPY pfcli /opt/pureflash/pfcli
COPY pfdd /opt/pureflash/pfdd
COPY qemu-img /opt/pureflash/qemu-img
COPY pfs /opt/pureflash/pfs
COPY fio /opt/pureflash/fio
COPY run-all.sh /opt/pureflash/run-all.sh
COPY restart-pfc.sh /opt/pureflash/restart-pfc.sh
COPY restart-pfs.sh /opt/pureflash/restart-pfs.sh

#ENTRYPOINT 
CMD [ "/opt/pureflash/run-all.sh" ]

