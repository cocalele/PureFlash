#ifndef afs_cluster_h__
#define afs_cluster_h__

/**
 * 建立向zookeeper的连接，
 * @param zk_ip_port zookeeper的集群IP地址，afs主程序从配置文件的
 *    [zookeeper]
 *    ip=192.168.0.253:2181,localhost:2181
 * 部分获得。上面这个例子中，zk_ip_port参数就是"192.168.0.253:2181,localhost:2181"
 * @return 0表示成功，
 *         负数表示失败
 * 
 * @implementation
 *  该函数的实现，调用 
 *            static zhandle_t* zookeeper_handler;
 *            zookeeper_handler = createClient(zk_ip_port, &ctx);
 * 成功建立zookeeper连接后，zookeeper_handler作为静态变量保留，其他的API实现中会使用该变量
 */
int init_cluster(const char* zk_ip_port);

/**
 * 向zookeeper注册节点，注册的过程就是在zookeeper的/s5/stores节点下面，建立如下的节点结构：
 *         /s5/stores/
 *               |
 *               +<mngt_ip>   #store节点的管理IP, 内容也为管理IP
 *                   |
 *                   +state  #状态， 内容为：ERROR, WARN, OK
 *                   +alive  #EPHEMERAL类型，表示该store节点是否在线，alive存在就表示在线,内容为空
 *                   +trays
 */
int register_store(const char* mngt_ip);

/**
 * 向zookeeper注册一个tray，注册的过程就是在zookeeper的/s5/stores/<mngt_ip>trays下对应存储系节点下面，建立如下的节点结构：
 * /s5/stores/<mngt_ip>/trays
 *						|
 *						+ <UUID>  #tray 的UUID作为结点名字
 *							|
 *							+devname  #该tray的device name
 *							+capacity #容量
 *							+state    #该tray的状态, 内容为:ERROR, WARN, OK
 *							+online   # EPHEMERAL类型，该tray是否在线，online节点存在就表示在线,内容为空
 * 该函数可以调用多次，每次注册一个tray
 */
int register_tray(const char* mngt_ip, const char* uuid, const char* devname, int64_t capacity);

#endif // afs_cluster_h__
