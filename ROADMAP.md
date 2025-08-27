# Roadmap

This document provides information on PureFlash development in current and upcoming releases. Community and contributor involvement is vital for successfully implementing all desired items for each release. We hope that the items listed below will inspire further engagement from the community to keep PureFlash progressing and shipping exciting and valuable features.

PureFlash follows a lean project management approach by splitting the development items into current, near term and future categories.

## Completed
The following features have been completed and included in version 1.8.2
 - Volume in replication mode, supports 1, 2 or 3 replicas
 - Snapshot
 - Failover between replicas
 - Cluster manager, node discovery and state monitoring
 - TCP protocol supporting
 - qemu, fio, nbd, iSCSI integration
 - Manually data balance
 - Manually data recovery after failure
 - aio and io_uring engine to access NVMe SSD
 - AOF(Append Only File) API, to use volume as AOF
 - Client multiple path and auto switch on network failure
 
## Current
This is the features in developing:
 - heartbeat between store nodes, client and store nodes
 - deploy with k8s operator
 - MetaDB HA with help of k8s
 - CSI driver


## Near Term

Typically the items under this category fall under next major release (after the current. e.g 1.9.0). To name a few backlogs (not in any particular order) on the near-term radar, where we are looking for additional help: 
 - Auto balance & recovery
 - Resource group, to seperate physical disk into group
 - RDMA protocol
 - Snapshot consistency group
 - NoF interface 
 - Black list
 - Multi-Queue to access NVMe SSD
 - OpenStack Cinder driver

## Future
As the name suggests this bucket contains items that are planned for future.
 - QoS and client SLA
 - EC & Dedup
 - Stretch Cluster
 - Remote disaster redundancy in async mode
 - Volume Clone
 - support to use NVMe-SSD, HDD, SMR-HDD, ZNS-SSD, Tape as under layer media.
 - Support Erasure Code on all type of medias
 - Quickly failover with help of sharable CXL memory pool
 - Support to use CXL memory pool as a distributed cache
 
# Getting involved with Contributions

We are always looking for more contributions. If you see anything above that you would love to work on, we welcome you to become a contributor and maintainer of the areas that you love. You can get started by commenting on the related issue or by creating a new issue.

#Release planning
 - v1.8.2, the latest release
 - v1.9.0 at 2023.12, all features in 'Current' stage will be included