/*
 * Copyright (C) 2016 Liu Lele(liu_lele@126.com)
 *
 * This code is licensed under the GPL.
 */
#ifndef afs_cluster_h__
#define afs_cluster_h__
#include <stdint.h>
#include <uuid/uuid.h>

#include "pf_utils.h"

//node state
#define NS_OK "OK"
#define NS_WARN "WARN"
#define NS_ERROR	"ERROR"

//TRAY state
#define TS_OK "OK"
#define TS_WARN "WARN"
#define TS_ERROR	"ERROR"

/**
 * 建立向zookeeper的连接，
 * @param zk_ip_port zookeeper的集群IP地址，afs主程序从配置文件的
 *    [zookeeper]
 *    ip=192.168.0.253:2181,localhost:2181
 * 部分获得。上面这个例子中，zk_ip_port参数就是"192.168.0.253:2181,localhost:2181"
 * @return 0表示成功，
 *         负数表示失败, 数字为-errno
 *
 * @implementation
 *  该函数的实现，调用
 *            static zhandle_t* zookeeper_handler;
 *            zookeeper_handler = createClient(zk_ip_port, &ctx);
 * 成功建立zookeeper连接后，zookeeper_handler作为静态变量保留，其他的API实现中会使用该变量
 */
int init_cluster(const char* zk_ip_port, const char* cluster_name);

/**
 * 向zookeeper注册节点，注册的过程就是在zookeeper的/s5/stores节点下面，建立如下的节点结构：
 *         /s5/stores/
 *               |
 *               +<id>   #store节点的ID,
 *                   |
 *                   +mngt_ip #内容为store节点的管理IP,
 *                   +state  #状态， 内容为：ERROR, WARN, OK
 *                   +alive  #EPHEMERAL类型，表示该store节点是否在线，alive存在就表示在线,内容为空
 *                   +trays
 * @remark
 * register_store不从创建sate和 alive节点，需要在节点初始化完毕后，调用set_store_node_state函数创建此二节点。
 * @return 0 on success, negative value on error. On error, an error message is logged.
 * @retval ZNONODE the parent node does not exist.
 * @retval ZNOAUTH the client does not have permission.
 * @retval ZNOCHILDRENFOREPHEMERALS cannot create children of ephemeral nodes.
 * @retval ZBADARGUMENTS - invalid input parameters
 * @retval ZINVALIDSTATE - zhandle state is either ZOO_SESSION_EXPIRED_STATE or ZOO_AUTH_FAILED_STATE
 * @retval ZMARSHALLINGERROR - failed to marshall a request; possibly, out of memory
 */
int register_store_node(int store_id, const char* mngt_ip);

/**
 * 向zookeeper注册一个tray，注册的过程就是在zookeeper的/<cluster_name>/stores/<id>trays下对应存储系节点下面，建立如下的节点结构：
 * /s5/stores/<id>/trays
 *						|
 *						+ <UUID>  #tray 的UUID作为结点名字
 *							|
 *							+devname  #该tray的device name
 *							+capacity #容量
 *							+state    #该tray的状态, 内容为:ERROR, WARN, OK
 *							+object_size # object size
 *							+online   # EPHEMERAL类型，该tray是否在线，online节点存在就表示在线,内容为空
 * 该函数可以调用多次，每次注册一个tray
 * register_tray不从创建sate和 online节点，需要在节点初始化完毕后，调用set_tray_state函数创建此二节点。
 * @return 0 on success, negative value on error. On error, an error message is logged.
 * @retval ZNONODE the parent node does not exist.
 * @retval ZNOAUTH the client does not have permission.
 * @retval ZNOCHILDRENFOREPHEMERALS cannot create children of ephemeral nodes.
 * @retval ZBADARGUMENTS - invalid input parameters
 * @retval ZINVALIDSTATE - zhandle state is either ZOO_SESSION_EXPIRED_STATE or ZOO_AUTH_FAILED_STATE
 * @retval ZMARSHALLINGERROR - failed to marshall a request; possibly, out of memory
 */
int register_tray(int store_id, const uuid_t uuid, const char* devname, int64_t capacity, int64_t object_size);
int register_shared_disk(int store_id, const uuid_t uuid, const char* devname, int64_t capacity, int64_t obj_size);

/**
 * set store node's state. create `state` and `alive` node on zookeeper, if not exists.
 * @seealso register_store for tree structure of store node in zookeeper
 * @param store_id  ID of store node, use as ID of store node
 * @param state store node's state, value can NS_OK, NS_WARN, NS_ERROR
 * @param alive TRUE for alive, FALSE for not
 * @return 0 on success, negative value on error. On error, an error message is logged.
 * @retval ZNONODE the parent node does not exist.
 * @retval ZNOAUTH the client does not have permission.
 * @retval ZNOCHILDRENFOREPHEMERALS cannot create children of ephemeral nodes.
 * @retval ZBADARGUMENTS - invalid input parameters
 * @retval ZINVALIDSTATE - zhandle state is either ZOO_SESSION_EXPIRED_STATE or ZOO_AUTH_FAILED_STATE
 * @retval ZMARSHALLINGERROR - failed to marshall a request; possibly, out of memory
*/
int set_store_node_state(int store_id, const char* state, BOOL alive);

/**
 * set tray state. create `state` and `online` node on zookeeper, if not exists.
 * @seealso register_tray for tree structure of tray node in zookeeper.
 * @param store_id ID of store node of the tray
 * @param tray_uuid uuid of tray to set
 * @param state, tray state, can be TS_OK, TS_WARN, TS_ERROR
 * @online TRUE for online, FALSE for not
 * @return 0 on success, negative value on error. On error, an error message is logged.
 * @retval ZNONODE the parent node does not exist.
 * @retval ZNOAUTH the client does not have permission.
 * @retval ZNOCHILDRENFOREPHEMERALS cannot create children of ephemeral nodes.
 * @retval ZBADARGUMENTS - invalid input parameters
 * @retval ZINVALIDSTATE - zhandle state is either ZOO_SESSION_EXPIRED_STATE or ZOO_AUTH_FAILED_STATE
 * @retval ZMARSHALLINGERROR - failed to marshall a request; possibly, out of memory
 */
int set_tray_state(int store_id, const uuid_t tray_uuid, const char* state, BOOL online);

int set_tray_free_size(int store_id, const uuid_t tray_uuid, int64_t free_size);

int register_port(int store_id, const char* ip, int purpose);
#endif // afs_cluster_h__
