PureFlash Deployment
====================

## Config Zookeeper
1. to make zookeeper reponse quickly when node is down, suggest to change `tickTime` and `syncLimit` in _zoo.cfg_
```
# The number of milliseconds of each tick
tickTime=300
# The number of ticks that can pass between
# sending a request and getting an acknowledgement
syncLimit=3
```
