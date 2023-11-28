# 心跳机制


## 设计和实现
- [x] 建立连接时(TCP或RDMA), 记录连接信息到服务端(PfRdmaServer或PfTcpServer)的客户端连接MAP记录中(client_ip_conn_map)
- [] 通过连接下发IO时, 在IO完成时, 更新连接上的最后通信时间
- [x] 开启线程, 检测记录中的连接(目前仅打印)
- [] 如果连接状态正常,且最后通信时间与当前时间相隔一定的时间后, 触发心跳发送
- [] TCP和RDMA实现心跳发送的事件类型
- [] 如果重发心跳一定的次数内, 都失败了, 说明网络异常了, 则关闭连接


## 详情
### TCP
- 服务端在接受连接成功时记录客户端连接信息(PfTcpServer::accept_connection())
- 在(int PfTcpServer::init())中开启连接检查线程


### RDMA
服务端在接受连接成功时记录客户端连接信息(on_connect_request)


