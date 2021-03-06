FROM pf-base:1.0
LABEL version="1.1"
RUN mkdir /etc/pureflash
COPY conf/pfc.conf /etc/pureflash/pfc.conf
COPY conf/pfs.conf /etc/pureflash/pfs.conf
COPY conf/pfc.conf /etc/pureflash/pf.conf
COPY apache-zookeeper-3.5.9-bin /opt/pureflash/apache-zookeeper-3.5.9-bin
COPY jconductor /opt/pureflash/jconductor
COPY mariadb /opt/pureflash/mariadb
COPY mariadb/mariadb.cnf /etc/mysql/mariadb.cnf
COPY mariadb/50-server.cnf /etc/mysql/mariadb.conf.d/50-server.cnf
COPY pfcli /opt/pureflash/pfcli
COPY pfdd /opt/pureflash/pfdd
COPY qemu-img /opt/pureflash/qemu-img
COPY pfs /opt/pureflash/pfs
COPY fio /opt/pureflash/fio
COPY run-all.sh /opt/pureflash/run-all.sh

#ENTRYPOINT 
CMD [ "/opt/pureflash/run-all.sh" ]

