1. What's PureFlash
===================
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
  
2. Why need a new ServerSAN software?
=====================================
Flash storage device is totally different than traditional HDD. The essential different is that SSD don't need seek time
before read/write. So SSD has outstanding random read/write performance. 

Almost all traditional storage system has a deep software stack from client's access to data's stored on disk. This deep 
software stack exhaust system's compute power and make the SSD useless. PureFlash simplify software stack by the following ways:
 * 1) Use RDMA instead of TCP between server nodes and client
 * 2) manage raw SSD directly instead of using file system
 * 3) provide block service only

