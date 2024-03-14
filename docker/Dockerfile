FROM docker.io/pureflash/pureflash-dev:1.9.1-x64 AS builder

WORKDIR /
RUN wget https://gitee.com/cocalele/PureFlash/raw/master/docker/build-all.sh
RUN bash ./build-all.sh

FROM ubuntu:22.04
LABEL version="1.9.1"
ARG DEBIAN_FRONTEND=noninteractive
ENV TZ=Asia/ShangHai
RUN ln -snf /usr/share/zoneinfo/$TZ /etc/localtime && echo $TZ > /etc/timezone

#COPY apt-ali-sources-2204-arm.list /etc/apt/sources.list 
COPY apt-ali-sources-2204.list /etc/apt/sources.list 
RUN apt update

RUN apt install -y cgdb curl gzip jq libaio1 libcurl4   libibverbs1 libicu-dev libjsoncpp25 librdmacm1 readline-common libstdc++6 libtool libuuid1 tar unzip  util-linux vim wget  net-tools 

#To install mariadb-server
RUN apt install -y mariadb-server

#To install java for zk and jconductor
RUN  apt install -y default-jre



RUN mkdir /etc/pureflash
COPY --from=builder /PureFlash/docker/conf/pfc.conf /etc/pureflash/pfc.conf
COPY --from=builder /PureFlash/docker/conf/pfs.conf /etc/pureflash/pfs.conf
COPY --from=builder /PureFlash/docker/conf/pfc.conf /etc/pureflash/pf.conf
RUN rm -rf /tmp/zookeeper
RUN mkdir /tmp/zookeeper
RUN mkdir /var/crash

RUN rm -rf /opt/pureflash/jconductor
RUN rm -rf /opt/pureflash/mariadb

RUN mkdir -p /opt/pureflash/jconductor
RUN mkdir -p /opt/pureflash/mariadb

COPY --from=builder /jconductor/pfconductor.jar /opt/pureflash/jconductor/
COPY --from=builder /jconductor/lib /opt/pureflash/jconductor/lib/


COPY --from=builder /jconductor/res/apache-zookeeper-3.5.9-bin.tar.gz  /opt/pureflash/
RUN tar xzf /opt/pureflash/apache-zookeeper-3.5.9-bin.tar.gz -C /opt/pureflash/
RUN mv /opt/pureflash/apache-zookeeper-3.5.9-bin/conf/zoo_sample.cfg /opt/pureflash/apache-zookeeper-3.5.9-bin/conf/zoo.cfg

COPY --from=builder  /jconductor/res/init_s5metadb.sql  /opt/pureflash/mariadb/
COPY --from=builder  /jconductor/pfcli  /opt/pureflash


COPY --from=builder  /PureFlash/build/bin/* /opt/pureflash/
#COPY --from=builder  /PureFlash/build/bin/pfdd /opt/pureflash/
COPY --from=builder  /qemu/build/qemu-img /opt/pureflash/
COPY --from=builder  /fio/fio /opt/pureflash/
COPY --from=builder  /PureFlash/docker/run-all.sh /opt/pureflash/run-all.sh
COPY --from=builder  /PureFlash/docker/restart-pfc.sh /opt/pureflash/restart-pfc.sh
COPY --from=builder  /PureFlash/docker/restart-pfs.sh /opt/pureflash/restart-pfs.sh

COPY --from=builder /PureFlash/docker/mariadb/mariadb.cnf /etc/mysql/mariadb.cnf
COPY --from=builder /PureFlash/docker/mariadb/50-server.cnf /etc/mysql/mariadb.conf.d/50-server.cnf



#COPY --from=builder  /jconductor /opt/pureflash/jconductor
#RUN mv /opt/pureflash/jconductor/pfconductor.jar /opt/pureflash/pfconductor.jar


#COPY mariadb /opt/pureflash/mariadb

#COPY pfcli /opt/pureflash/pfcli
#COPY pfdd /opt/pureflash/pfdd
#COPY qemu-img /opt/pureflash/qemu-img
#COPY pfs /opt/pureflash/pfs
#COPY fio /opt/pureflash/fio
#COPY run-all.sh /opt/pureflash/run-all.sh
#COPY restart-pfc.sh /opt/pureflash/restart-pfc.sh
#COPY restart-pfs.sh /opt/pureflash/restart-pfs.sh

#COPY librte_eal.so.23 /lib/
#COPY librte_mempool.so.23 /lib/
#COPY librte_ring.so.23 /lib/
#COPY librte_bus_pci.so.23 /lib/
#COPY librte_kvargs.so.23 /lib/
#COPY librte_telemetry.so.23 /lib/
#COPY librte_pci.so.23 /lib/


#ENTRYPOINT 
CMD [ "/opt/pureflash/run-all.sh" ]

