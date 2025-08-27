For Chinese version, please visist [中文版README](./README_cn.md)

# 1. What's PureFlash

PureFlash is an open source ServerSAN implementation, that is, through a large number of general-purpose servers, plus PureFlash software system, to construct a set of distributed SAN storage that can meet the various business needs of enterprises.

The idea behind PureFlash comes from the fully hardware-accelerated flash array S5, so while PureFlash itself is a software-only implementation, its storage protocol is highly hardware-friendly. It can be considered that PureFlash's protocol is the NVMe protocol plus cloud storage feature enhancements, including snapshots, replicas, shards, cluster hot upgrades and other capabilities.


  
# 2. Why need a new ServerSAN software?
PureFlash is a storage system designed for the all-flash era. At present, the application of SSD disks is becoming more and more extensive, and there is a trend of fully replacing HDDs. The significant difference between SSD and HDD is the performance difference, which is also the most direct difference in user experience, and with the popularity of NVMe interface, the difference between the two is getting bigger and bigger, and this nearly hundredfold difference in quantity is enough to bring about a qualitative change in architecture design. For example, the performance of HDDs is very low, far lower than the performance capabilities of CPUs and networks, so the system design criterion is to maximize the performance of HDDs, and to achieve this goal can be at the expense of CPU and other resources. In the NVMe era, the performance relationship has been completely reversed, and the disk is no longer the bottleneck, but the CPU and network have become the bottleneck of the system. That method of consuming CPU to optimize IO is counterproductive.

Therefore, we need a new storage system architecture to fully exploit the capabilities of SSDs and improve the efficiency of the system. PureFlash is designed to simplify IO stack, separate data path and control path, and prioritize fast path as the basic principles to ensure high performance and high reliability, and provide block storage core capabilities in the cloud computing era.

# 3. Software design
Almost all current distributed storage systems have a very deep software stack, from the client software to the final server-side SSD disk, the IO path is very long. This deep software stack consumes a lot of system computing resources on the one hand, and on the other hand, the performance advantages of SSDs are wiped out. PureFlash is designed with the following principles in mind:
  - "Less is more", remove the complex logic on the IO path, use the unique BoB (Block over Bock) structure, and minimize the hierarchy
  - "Resource-centric", around CPU resources, SSD resource planning software structure, number of threads. Instead of planning according to the usual needs of software code logic
  - "Control/Data Separation", the control part is developed in Java, and the data path is developed in C++, each taking its own strengths

In addition, PureFlash "uses TCP in RDMA mode" on the network model, instead of the usual "use RDMA as faster TCP", RDMA must configure the one-sided API and the two-sided API correctly according to business needs. This not only makes RDMA used correctly, but also greatly improves the efficiency of TCP use.

Here is the structure diagram of our system:

The whole system include 5 modules (View graph with tabstop=4 and monospaced font)
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
| pfbd/pfkd/tcmu    |        |                  |                  |
| (User and kernel  +------->+ pfs              +------------------+
| space client)     |        | (Max 1024 nodes) |
+-------------------+        +------------------+

</pre>

## 3.1 pfs, PureFlash Store
  This module is the storage service daemon that provides all data services, including:
   1) SSD disk space management
   2) Network Interface Services (RDMA and TCP protocols)
   3) IO request processing
  
A PureFlash cluster can support up to 1024 pfs storage nodes. All PFS provide services to the outside world, so all nodes are working in the active state.
  
## 3.2 pfconductor
    This module is the cluster control module. A production deployment should have at least 2 pfconductor nodes (up to 5). Key features include:
    1) Cluster discovery and state maintenance, including the activity of each node, the activity of each SSD, and the capacity
	2) Respond to users' management requests, create volumes, snapshots, tenants, etc
	3) Cluster operation control, volume opening/closing, runtime fault handling
  This module is programmed in Java, and the code repository URL： https://github.com/cocalele/pfconductor
  
## 3.3 Zookeeper
  Zookeeper is a module in the cluster that implements the Paxos protocol to solve the network partitioning problem. All pfconductor and pfs instances are registered with zookeeper so that active pfconductor can discover other members in the entire cluster.

## 3.4 MetaDB
  MetaDB is used to hold cluster metadata, and we use MariaDB here. Production deployment requires the Galaera DB plug-in to ensure it's HA.
  
## client application
  There are two types of client interfaces: user mode and kernel mode. User mode is accessed by applications in the form of APIs, which are located in libpfbd.

### 3.5.1 pfdd 
  pfdd is a dd-like tool, but has access to the PureFlash volume，https://github.com/cocalele/PureFlash/blob/master/common/src/pf_pfdd.cpp

### 3.5.2 fio
  A FIO branch that supports PFBD. Can be used to test PureFlash with direct access to PureFlash volume。repository URL：https://github.com/cocalele/fio.git 

### 3.5.3 qemu
  A qemu branch with PFBD enabled, support to access PureFlash volume from VM. repository URL: https://github.com/cocalele/qemu.git

### 3.5.4 kernel driver
  PureFlash provides a free Linux kernel mode driver, which can directly access pfbd volumes as block devices on bare-metal machines, and then format them into arbitrary file systems, which can be accessed by any application without API adaptation.
  
  The kernel driver is ideal for Container PV and database scenarios.

### 3.5.5 nbd
  A nbd implementation to support access PureFlash volume as nbd device， repository URL： https://gitee.com/cocalele/pfs-nbd.git

  After compile, you can attach a volume like bellow:
``` 
    # pfsnbd  /dev/nbd3 test_v1 
```

### 3.5.6 iSCSI
  A LIO backend implementation to use PureFlash volume as LIO backend device，so it ban be accessed via iSCSI. repository URL：https://gitee.com/cocalele/tcmu-runner.git
 
  # networks ports
  - 49162  store node TCP port
  - 49160  store node RDMA port

  - 49180  conductor HTTP port
  - 49181  store node HTTP port

# Try PureFlash
the easiest way to try PureFlash is to use docker.
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
