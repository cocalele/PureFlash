#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <afs_main.h>
#include "afs_cluster.h"
#include "zookeeper.h"
#include "s5_log.h"

#define ZK_TIMEOUT_MSEC 3000

int init_cluster(const char* zk_ip_port, const char* cluster_name)
{
    int rc = app_context.zk_client.init(zk_ip_port, ZK_TIMEOUT_MSEC, cluster_name);
	if (rc)
	{
		S5LOG_ERROR("Failed to connect zk, errno:%d", rc);
		return rc;
	}
	return 0;
}

/*
 * set node's content, create node if node not exists.
 * on error, an error message will be logged.
 * @param node name of node to update or create
 * @param value buffer to node content, pass NULL if node has no value, or clear value of node
 * @param val_len length of value, in byte. pass -1 if value is NULL.
 *
 * @return ZOK on success, negative value on error
 * @retval ZNONODE the parent node does not exist.
 * @retval ZNOAUTH the client does not have permission.
 * @retval ZNOCHILDRENFOREPHEMERALS cannot create children of ephemeral nodes.
 * @retval ZBADARGUMENTS - invalid input parameters
 * @retval ZINVALIDSTATE - zhandle state is either ZOO_SESSION_EXPIRED_STATE or ZOO_AUTH_FAILED_STATE
 * @retval ZMARSHALLINGERROR - failed to marshall a request; possibly, out of memory
 *
 *
 */
static int zk_update(const char* node, const char* value, int val_len)
{
	int rc = zoo_create(app_context.zk_client.zkhandle, node, NULL, -1, &ZOO_OPEN_ACL_UNSAFE, 0, NULL, 0);
	if (rc != ZOK && rc != ZNODEEXISTS)
	{
		S5LOG_ERROR("Failed to create zookeeper node %s rc:%d", node, rc);
		return rc;
	}
	rc = zoo_set(app_context.zk_client.zkhandle, node, value, value ? val_len : -1, -1);
	if (rc != ZOK)
	{
		S5LOG_ERROR("Failed to update zookeeper node value node:%s rc:%d", node, rc);
		return rc;
	}
	return ZOK;
}

int set_store_node_state(const char* mngt_ip, const char* state, BOOL alive)
{
	char zk_node_name[64];
	int rc;
	snprintf(zk_node_name, sizeof(zk_node_name), "/s5/stores/%s/state", mngt_ip);
	if ((rc = zk_update(zk_node_name, state, (int)strlen(state))) != ZOK)
		return rc;

	snprintf(zk_node_name, sizeof(zk_node_name), "/s5/stores/%s/alive", mngt_ip);
	if(alive)
	{
		rc = zoo_create(app_context.zk_client.zkhandle, zk_node_name, NULL, -1, &ZOO_OPEN_ACL_UNSAFE, ZOO_EPHEMERAL, NULL, 0);
		if (rc != ZOK && rc != ZNODEEXISTS)
		{
			S5LOG_ERROR("Failed to create zookeeper node %s rc:%d", zk_node_name, rc);
			return rc;
		}
	}
	else
	{
		rc = zoo_delete(app_context.zk_client.zkhandle, zk_node_name, -1);
		return rc;
	}
	return ZOK;

}
int register_store_node(const char* mngt_ip)
{
	char zk_node_name[64];
	int rc;
	if ((rc = zk_update("/s5", NULL, 0)) != ZOK)
		return rc;
	if ((rc = zk_update("/s5/stores", NULL, 0)) != ZOK)
		return rc;

	snprintf(zk_node_name, sizeof(zk_node_name), "/s5/stores/%s", mngt_ip);
	if ((rc = zk_update(zk_node_name, NULL, 0)) != ZOK)
		return rc;


	snprintf(zk_node_name, sizeof(zk_node_name), "/s5/stores/%s/trays", mngt_ip);
	if ((rc = zk_update(zk_node_name, NULL, 0)) != ZOK)
		return rc;
	return 0;
}

int set_tray_state(const char* mngt_ip, const uuid_t uuid, const char* state, BOOL online)
{
	char zk_node_name[128];
	char uuid_str[64];
	int rc;
	uuid_unparse(uuid, uuid_str);
	snprintf(zk_node_name, sizeof(zk_node_name), "/s5/stores/%s/trays/%s/state", mngt_ip, uuid_str);
	if ((rc = zk_update(zk_node_name, NULL, 0)) != ZOK)
		return rc;
	snprintf(zk_node_name, sizeof(zk_node_name), "/s5/stores/%s/trays/%s/online", mngt_ip, uuid_str);
	if (online)
	{
		rc = zoo_create(app_context.zk_client.zkhandle, zk_node_name, NULL, -1, &ZOO_OPEN_ACL_UNSAFE, ZOO_EPHEMERAL, NULL, 0);
		if (rc != ZOK && rc != ZNODEEXISTS)
		{
			S5LOG_ERROR("Failed to create zookeeper node %s rc:%d", zk_node_name, rc);
			return rc;
		}
	}
	else
	{
		rc = zoo_delete(app_context.zk_client.zkhandle, zk_node_name, -1);
		return rc;
	}
	return ZOK;
}

int register_tray(const char* mngt_ip, const uuid_t uuid, const char* devname, int64_t capacity)
{
	char zk_node_name[128];
	char value_buf[128];
	char uuid_str[64];
	int rc;
	uuid_unparse(uuid, uuid_str);
	snprintf(zk_node_name, sizeof(zk_node_name), "/s5/stores/%s/trays/%s", mngt_ip, uuid_str);
	if ((rc = zk_update(zk_node_name, NULL, 0)) != ZOK)
		return rc;

	snprintf(zk_node_name, sizeof(zk_node_name), "/s5/stores/%s/trays/%s/devname", mngt_ip, uuid_str);
	if ((rc = zk_update(zk_node_name, devname, (int)strlen(devname))) != ZOK)
		return rc;


	snprintf(zk_node_name, sizeof(zk_node_name), "/s5/stores/%s/trays/%s/capacity", mngt_ip, uuid_str);
	int len = snprintf(value_buf, sizeof(value_buf), "%ld", capacity);
	if ((rc = zk_update(zk_node_name, value_buf, (int)len)) != ZOK)
		return rc;

	return 0;
}
