# 1. What's PureFlash


PureFlash is a ServerSAN system designed for flash based storage device, such as PCIe flash card, NVMe SSD, SATA SSD. 
PureFlash has the following features:

  * 1) provide block storage to client
  * 2) multi-replications on different for data protection
  * 3) multi-path for client 
  * 4) RDMA based client access 
  * 5) thin provision
  * 6) uninterruptedly scale out
  * 7) volume snap shot
  * 8) volume clone
  
# 2. Why need a new ServerSAN software?


Flash storage device is totally different than traditional HDD. The essential different is that SSD don't need seek time
before read/write. So SSD has outstanding random read/write performance. 

Almost all traditional storage system has a deep software stack from client's access to data's stored on disk. This deep 
software stack exhaust system's compute power and make the SSD useless. PureFlash simplify software stack by the following ways:
 * 1) Use RDMA instead of TCP between server nodes and client
 * 2) manage raw SSD directly instead of using file system
 * 3) provide block service only

# 3. Software design


The whole system include 3 modules (View graph with tabstop=4 and monospaced font)
<pre>			   
                                                            +---------------+
                                                            |               |
                                                       +--->+  MetaDB       |
                                                       |    |  (HA DB)      |
                             +------------------+      |    +---------------+
                             |                  +------+
                             | pfconductor      |           +---------------+
                        +---->  (Max 5 nodes)   +----------->               |
                        |    +--------+---------+           | Zookeeper     |
                        |             |                     | (3 nodes)     |
                        |             |                     +------^--------+
+-------------------+   |             |                            |
|                   +---+    +--------v---------+                  |
| pfbd  tcmu        |        |                  |                  |
| (User and         +------->+ pfs              +------------------+
| space client)     |        | (Max 1024 nodes) |
+-------------------+        +------------------+

</pre>

## 3.1 pfs, S5 All Flash System
  This module is the server daemon, provide all data service on store. include:
   1) SSD disk management
   2) Networ interface (RDMA and TCP protocol)
   3) IO handling
  
  There're at most 1024 S5afs nodes in a cluster. All s5afs works in acitve mode, since every s5afs need to provide data service.
  
## 3.2 pfconductor
  This is the control module to conduct all players in storage cluster. A cluster should has at least 2 s5conductor nodes and at most 5 are supported.
  pfconductor works in active-standby mode. only one conductor is active. All others in standby.
  This module is programed in Java and reside in repository: https://github.com/cocalele/pfconductor
  
## 3.3 Zookeeper
  All conductor and afs nodes(instance) register themself to zookeeper, so the active conductor can discovery services in cluster.

## 3.4 MetaDB
  MetaDB is a MariaDB cluster with HA.   
  
## 3.5 pfbd 
  pfbd is user space client. also a virtio-block Qemu driver is provided in https://github.com/cocalele/qemu/tree/pfbd

# networks ports
49162  store node TCP port
49160  store node RDMA port

49180  conductor HTTP port
49181  store node HTTP port

# Try PureFlash
the easyest way to try PureFlash is to use docker.
Suppose you have a NVMe SSD, e.g. nvme1n1, make sure the data is no more needed.
```
# dd if=/dev/zero of=/dev/nvme1n1 bs=1M count=100 oflag=direct
# docker pull pureflash/pureflash:latest
# docker run -ti --rm  --env PFS_DISKS=/dev/nvme1n1 --ulimit core=-1 --privileged  -e TZ=Asia/Shanghai  --network host  pureflash/pureflash:latest
# pfcli list_store
+----+---------------+--------+
| Id | Management IP | Status |
+----+---------------+--------+
|  1 |     127.0.0.1 |     OK |
+----+---------------+--------+
 
# pfcli list_disk
+----------+--------------------------------------+--------+
| Store ID |                 uuid                 | Status |
+----------+--------------------------------------+--------+
|        1 | 9ae5b25f-a1b7-4b8d-9fd0-54b578578333 |     OK |
+----------+--------------------------------------+--------+

#let's create a volume
# pfcli create_volume -v test_v1 -s 128G --rep 1

#run fio test
# /opt/pureflash/fio -name=test -ioengine=pfbd -volume=test_v1 -iodepth=16  -rw=randwrite -size=128G -bs=4k -direct=1
```
