For English version, please visist [README_en.md](./README_en.md)

# 1. PureFlash是什么

PureFlash是一个开源的ServerSAN实现，也就是通过大量的通用服务器，加上PureFlash的软件系统，构造出一套能满足企业各种业务需求的分布式SAN存储。



PureFlash的思想来自于全硬件加速闪存阵列S5, 因此虽然PureFlash本身是纯软件实现，但其存储协议对硬件加速是高度友好的。可以认为PureFlash的协议就是NVMe 协议加上云存储特性增强，包括快照、副本、shard、集群热升级等能力。

  
# 2. 为什么要做一个全新ServerSAN实现?

PureFlash是为全闪存时代而设计的存储系统。当前SSD盘的应用越来越广泛，大有全面取代HDD的趋势。SSD与HDD的显著区别就是性能差异，这也是用户体验最直接的差异，而且随着NVMe接口的普及，二者差异越来大，这种近百倍的量变差异足以带来架构设计上的质变。举个例子，原来HDD的性能很低，远远低于CPU、网络的性能能力，因此系统设计的准则是追求HDD的性能最大化，为达到这个目标可以以消耗CPU等资源为代价。而到了NVMe时代，性能关系已经完全颠倒了，盘不再是瓶颈，反而CPU、网络成为系统的瓶颈。那种消耗CPU以优化IO的方法只能适得其反。

因此我们需要一套全新的存储系统架构，以充分发挥SSD的能力，提高系统的效率。PureFlash的设计思想以简化IO stack, 数据通路与控制通路分离，快速路径优先为基本原则，确保高性能与高可靠性，提供云计算时代块存储核心能力。

# 3. Software design
当前的分布式存储系统几乎都有着非常深的软件栈，从客户端软件到最终服务端SSD盘，IO路径非常长。这个深厚的软件栈一方面消耗了大量的系统计算资源，另一方面也让SSD的性能优势荡然无存。PureFlash的设计贯彻了下面的几条原则：
  - “少就是多”， 去掉IO路径上复杂逻辑，使用独有BoB(Block over Bock)结构，将层级最小化
  - “资源为中心”， 围绕CPU资源，SSD资源规划软件结构、线程数量。而不是通常的根据软件代码逻辑需要进行规划
  - “控制/数据分离”， 控制部分使用java开发，数据路径使用C++开发，各取所长

此外PureFlash在网络模型上“以RDMA的模式使用TCP", 而不是通常的”把RDMA当成更快的TCP使用"， RDMA一定要将one-sided API与two-sided API 根据业务需要正确的配置。这不但使得RDMA得到了正确的使用，而且让TCP使用效率也大大提高。

下面是我们这个系统的结构图：
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

## 3.1 pfs, PureFlash Store
  这个模块是存储服务守护进程，提供所有的数据服务，包括：
   1) SSD盘空间管理
   2) 网络接口服务 (RDMA 和 TCP 协议)
   3) IO请求处理
  
  一个PureFlash集群最多可以支持1024个pfs存储节点。所有的pfs都对外提供服务，因此所有的节点都工作在active状态。
  
## 3.2 pfconductor
  这个模块是集群控制模块。一个产品化的部署应该有至少2个pfconductor节点（最多5个）。主要功能包括：
    1) 集群发现与状态维护，包括每个节点的活动与否，每个SSD的活动与否，容量
	2) 响应用户的管理请求，创建volume, 快照，租户等
	3) 集群运行控制，volume的打开/关闭，运行时故障处理
  这个模块用Java编写，位于另外一个代码库： https://github.com/cocalele/pfconductor
  
## 3.3 Zookeeper
  Zookeeper是集群中实现了Paxos协议的模块，解决网络分区问题。所有的pfconductor和pfs实例都注册到zookeeper, 这样活动的pfconductor就能发现整个集群中的其他成员。

## 3.4 MetaDB
  MetaDB是用来保存集群元数据的，我们这里使用的MariaDB。生产部署时需要配合Galaera DB插件，确保拥有HA特性。
  
## client端支持
client接口分两类：用户态和内核态。用户态以API形式给应用访问，这些API位于libpfbd中。
### 3.5.1 pfdd 
  pfdd是一个类似dd的工具，但是能访问PureFlash volume， https://github.com/cocalele/qemu/tree/pfbd

### 3.5.2 fio
  支持pfbd的 fio，可以使用fio直接访问pureflash对其进行性能测试。代码库在：https://github.com/cocalele/fio.git 

### 3.5.3 qemu
  pfbd也已经集成到了qemu里面，可以直接对接给VM使用。代码库在：https://gitee.com/cocalele/qemu.git

### 3.5.4 内核态驱动
  PureFlash提供了免费的内核态驱动，在物理机上可以直接将pfbd卷呈现成块设备，然后可以格式化成任意的文件系统，任何应用无需API适配就可以访问。
  
  内核驱动非常适合容器PV和数据库场景使用。

### 3.5.5 nbd对接
  支持将PureFlash volume以nbd的形式挂载到主机端， 代码库在： https://gitee.com/cocalele/pfs-nbd.git

### 3.5.6 iSCSI对接
  支持将PureFlash volume作为LIO的后端设备，提供iSCSI接口。 代码库在：https://gitee.com/cocalele/tcmu-runner.git

# 网络端口
 下面是pureflash使用到的网络端口，可以在出问题时检查服务是否正常。
 
49162  store node TCP port

49160  store node RDMA port

49180  conductor HTTP port

49181  store node HTTP port

# 尝试 PureFlash
 最方便尝试PureFlash的方法是使用容器.
 假定你已经有一个NVMe盘，比如, nvme1n1, 请确保这个盘上数据你已经不再需要. 然后按下面的步骤操作：

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
