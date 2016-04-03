/* bdd.c - xxxx */

/*
 * Copyright (c) 2015 NetBric Systems, Inc.
 *
 * The right to copy, distribute, modify or otherwise make use
 * of this software may be licensed only pursuant to the terms
 * of an applicable NetBric license agreement.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/epoll.h>
#include <sys/timerfd.h>
#include <sys/ioctl.h>
#include <sys/file.h>
#include <errno.h>
#include <stddef.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>
#include <sys/stat.h>
#include "s5ioctl.h"
//#include "s5conductor.h"
#include "bdd_log.h"

/* defines */
typedef unsigned char BOOL;
#define TRUE 1
#define FALSE 0

#define MAXEVENTS 2
#define MAX_RETRY_UNMAP_TIMES 2
#define INVALID_DEV_ID  (-1)
#define RECORD_FILE "/etc/s5/bdd_record.dat"
#define BDDAEMONLOCK "/var/tmp/bddaemonlock"
#define RECORD_LEN (MAX_DEVICE_NUM * sizeof(struct bdevice))

static struct bdevice mgt_device_ctx[MAX_DEVICE_NUM];
static char command_buffer[MAX_BDD_MESSAGE_LEN];
static int bdd_write_record();

static int bdd_set_socket(int sfd)
{
    int flags, ret;

    flags = fcntl(sfd, F_GETFL, 0);

    if(flags == -1)
    {
        perror("fcntl");
        return -1;
    }

    flags |= O_NONBLOCK;
    ret = fcntl(sfd, F_SETFL, flags);

    if(ret == -1)
    {
        perror("fcntl");
        return -1;
    }
    return 0;
}

static int bdd_create_socket(const char *server)
{
    int sfd;
    socklen_t len;
    struct sockaddr_un svr_un;//, cli_un;
    sfd = socket(AF_UNIX, SOCK_STREAM, 0);
    memset(&svr_un, 0, sizeof(svr_un));
    svr_un.sun_family = AF_UNIX;
    strncpy(svr_un.sun_path, server, strlen(server));
    len = offsetof(struct sockaddr_un, sun_path)+strlen(server);
    unlink(svr_un.sun_path);
    if(bind(sfd, (struct sockaddr *)&svr_un, len) == -1)
    {
        LOG_ERROR("Error: Bind %s failed. errno(%d).\n", server, errno);
        return -1;
    }
    return sfd;
}

static int bdd_spy_timer()
{
    int fd;
    struct itimerspec ts;
    ts.it_value.tv_sec = 6;
    ts.it_value.tv_nsec = 0;
    ts.it_interval.tv_sec = 6;
    ts.it_interval.tv_nsec = 0;
    fd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK);
    if(fd == -1)
    {
        LOG_ERROR("fail to create timerfd.");
        return -1;
    }
    if(timerfd_settime(fd, 0, &ts, NULL) == -1)
    {
        LOG_ERROR("fail to set timerfd.");
        return -1;
    }

    return fd;
}

static int bdd_s5bd_ioctl(struct ioctlparam *ictl_arg, unsigned long cmd)
{
    int fd;

    fd = open("/dev/s5bd", O_RDWR);
    if (fd < 0)
    {
        LOG_ERROR("open /dev/s5bd failed.");
		return -1;
	}

    if (ioctl(fd, cmd, ictl_arg) < 0)
    {
    	close(fd);
        return -2;
    }

    close(fd);
    return 0;
}

static struct bdevice * bdd_check_dev_mapped (struct device_info *dinfo)
{
    int i = 0;
	struct bdevice *bdev = NULL;

    for (i = 0; i < MAX_DEVICE_NUM; ++i)
    {
		bdev = &mgt_device_ctx[i];
		if (bdev->bddev_id == INVALID_DEV_ID)
			continue;

		/*printf("dinfo->dev_name %s ctx devname %s \r\n", dinfo->dev_name,
			mgt_device_ctx[i].dinfo.dev_name);*/

		//dev_name match
		if((strlen(dinfo->dev_name) > 0 &&
            strcmp(bdev->dinfo.dev_name, dinfo->dev_name) == 0))
		{
			goto found;
		}

		//tenant_name && volume_name match
		if(bdev->dinfo.toe_use_mode == TOE_MODE_CONDUCTOR &&
				strcmp(bdev->dinfo.mode_conductor.volume_name,
					dinfo->mode_conductor.volume_name) == 0 &&
				strcmp(bdev->dinfo.mode_conductor.tenant_name,
					dinfo->mode_conductor.tenant_name) == 0)
		{
			goto found;
		}
		else
		{
			continue;
		}

found:
		LOG_INFO("device(%s)-bddev_id(%d) exists.",
				dinfo->dev_name, bdev->bddev_id);
		return bdev;
    }

    return NULL;
}

static struct bdevice * bdd_find_bddev(void)
{
	int i;

	for (i = 0; i < MAX_DEVICE_NUM; i ++)
	{
		if (mgt_device_ctx[i].bddev_id != INVALID_DEV_ID)
			continue;
		else
		{
			mgt_device_ctx[i].bddev_id = i;
			return &mgt_device_ctx[i];
		}
	}

	return  NULL;
}

static void bdd_release_bddev(struct bdevice * dev_ctx)
{
	memset(dev_ctx, 0, sizeof(struct bdevice));
	dev_ctx->bddev_id = INVALID_DEV_ID;
}


/*
 * return value:
 * 0 : sucess.
 * -1: conductor register volume failed.
 * -2: ioctl error.
 * -3: conductor unregister volume failed.
 * -4: device exists when map or non exists when unmap
 */

static void bdd_exec_command()
{
    int i = 0;
	int ret = 0;
    struct bdd_message *bdd_msg = (struct bdd_message*)command_buffer;
    struct device_info *dinfo = (struct device_info*)(command_buffer +
		sizeof(struct bdd_message));
    struct bdevice * pbdev = NULL;
	struct ioctlparam ictl_arg;
	assert(bdd_msg->msg_type % 2 == 0);
    assert(bdd_msg->valid_msg_len <= MAX_BDD_MESSAGE_LEN);

	LOG_INFO("command(%d). 0:map, 2:unmap, 4:list;", bdd_msg->msg_type);

    switch (bdd_msg->msg_type)
    {
        case BDD_MSG_MAP:

			LOG_INFO("map 111.");
            if ((pbdev = bdd_check_dev_mapped(dinfo)) != NULL)
            {
                LOG_WARN("device exit, can't map.");
				bdd_msg->status = BDD_MSG_STATUS_DEVICE_EXISTS;
                return;
            }

			LOG_INFO("map 222.");
			pbdev = bdd_find_bddev();
			if (pbdev == NULL)
			{
				bdd_msg->status = BDD_MSG_STATUS_TOO_MANY_DEVICES;
				LOG_WARN("no free bddev context.");
				return;
			}
			LOG_INFO("map 333.");

			memset(&ictl_arg, 0, sizeof(struct ioctlparam));

  			/* get parameter for ioctrl */

			memcpy(&pbdev->dinfo, dinfo, sizeof(struct device_info));
			memcpy(&ictl_arg.bdev, pbdev, sizeof(struct bdevice));

			LOG_INFO("map 444.");

			/* map need toe ip, port, volume id, volume size */

            ret = bdd_s5bd_ioctl(&ictl_arg, MAP_DEVICE);
            if (ret == 0)
            {
				if(ictl_arg.retval != 0)
				{
					switch(dinfo->toe_use_mode)
					{
						case TOE_MODE_DEBUG:
							LOG_WARN("failed to map toe_ip(0x%x)-toe_port(%d) from driver, ret(%d).",
									dinfo->mode_debug.toe_ip,
									dinfo->mode_debug.toe_port,
									ictl_arg.retval);
							break;
						case TOE_MODE_CONDUCTOR:
							LOG_WARN("failed to map volume(%s)-tenant(%s) from driver, ret(%d).",
									dinfo->mode_conductor.volume_name,
									dinfo->mode_conductor.tenant_name,
									ictl_arg.retval);
							break;
						default:
							break;
					}
					bdd_release_bddev(pbdev);
					bdd_msg->status = ictl_arg.retval;
					return;
				}

				LOG_INFO("map 555.");
            	/* if user doesn't assign name, save the default name */

#if 1
				assert(strlen(dinfo->dev_name));
#else
            	if (strlen(dinfo->dev_name) == 0)
					sprintf(dinfo->dev_name, "s5bd%d", pbdev->bddev_id);
#endif

				bdd_write_record();
				bdd_msg->status = BDD_MSG_STATUS_OK;
				return;
            }
            else
            {
				switch(dinfo->toe_use_mode)
				{
					case TOE_MODE_DEBUG:
						LOG_WARN("map toe_ip(0x%x)-toe_port(%d) ioctl failed, ret(%d).",
								dinfo->mode_debug.toe_ip,
								dinfo->mode_debug.toe_port,
								ret);
						break;
					case TOE_MODE_CONDUCTOR:
						LOG_WARN("map volume(%s)-tenant(%s) ioctl failed, ret(%d).",
								dinfo->mode_conductor.volume_name,
								dinfo->mode_conductor.tenant_name,
								ret);
						break;
					default:
						break;
				}
            	bdd_release_bddev(pbdev);
				bdd_msg->status = BDD_MSG_STATUS_IOCTL_FAILED;
                return;
            }

            break;

        case BDD_MSG_UNMAP:

			LOG_INFO("unmap 111.");
            if ((pbdev = bdd_check_dev_mapped(dinfo)) == NULL)
			{
				LOG_WARN("device doesn't exit, can't unmap.");
				bdd_msg->status = BDD_MSG_STATUS_DEVICE_NON_EXISTS;
				return;
			}
			LOG_INFO("unmap 222.");

			/* unmap only need s5bd_id parameter */

			memcpy(&ictl_arg.bdev, pbdev, sizeof(struct bdevice));

            ret = bdd_s5bd_ioctl(&ictl_arg, UNMAP_DEVICE);
            if(ret == 0 && ictl_arg.retval != 0)
			{
				i = 0;
				while(ret == 0 && (ictl_arg.retval == -EBUSY || ictl_arg.retval == -EAGAIN) && i < MAX_RETRY_UNMAP_TIMES)
				{
					++i;
					usleep(100000);//100ms
					ret = bdd_s5bd_ioctl(&ictl_arg, UNMAP_DEVICE);
					if(ret != 0)
					{
						break;
					}
				}
				if(ret == 0)
				{
					switch(dinfo->toe_use_mode)
					{
						case TOE_MODE_DEBUG:
							LOG_WARN("failed to unmap toe_ip(0x%x)-toe_port(%d) from driver, ret(%d).",
									dinfo->mode_debug.toe_ip,
									dinfo->mode_debug.toe_port,
									ictl_arg.retval);
							break;
						case TOE_MODE_CONDUCTOR:
							LOG_WARN("failed to unmap volume(%s)-tenant(%s) from driver, ret(%d).",
									dinfo->mode_conductor.volume_name,
									dinfo->mode_conductor.tenant_name,
									ictl_arg.retval);
							break;
						default:
							break;
					}
					bdd_msg->status = ictl_arg.retval;
//					return;
				}
			}

			if(ret == 0 && ictl_arg.retval == 0)
			{
				switch(dinfo->toe_use_mode)
				{
					case TOE_MODE_DEBUG:
						LOG_WARN("unmap toe_ip(0x%x)-toe_port(%d) ioctl sucess.",
								dinfo->mode_debug.toe_ip,
								dinfo->mode_debug.toe_port);
						break;
					case TOE_MODE_CONDUCTOR:
						LOG_WARN("unmap volume(%s)-tenant(%s) ioctl success.",
								dinfo->mode_conductor.volume_name,
								dinfo->mode_conductor.tenant_name);
						break;
					default:
						break;
				}
				memcpy(dinfo, &pbdev->dinfo, sizeof(struct device_info));
            	bdd_release_bddev(pbdev);
				bdd_write_record();
				bdd_msg->status = BDD_MSG_STATUS_OK;
			}

			if (ret != 0)
			{
				switch(dinfo->toe_use_mode)
				{
					case TOE_MODE_DEBUG:
						LOG_WARN("unmap toe_ip(0x%x)-toe_port(%d) ioctl failed, ret(%d).",
								dinfo->mode_debug.toe_ip,
								dinfo->mode_debug.toe_port,
								ret);
						break;
					case TOE_MODE_CONDUCTOR:
						LOG_WARN("unmap volume(%s)-tenant(%s) ioctl failed, ret(%d).",
								dinfo->mode_conductor.volume_name,
								dinfo->mode_conductor.tenant_name,
								ret);
						break;
					default:
						break;
				}
				bdd_msg->status = BDD_MSG_STATUS_IOCTL_FAILED;
				return;
			}
			LOG_INFO("unmap 333.");
            break;
        case BDD_MSG_LIST:

			assert(bdd_msg->device_info_num == 0);

			bdd_msg->status = BDD_MSG_STATUS_OK;
            for (i = 0; i < MAX_DEVICE_NUM; ++i)
            {
				pbdev = &mgt_device_ctx[i];
                if (pbdev->bddev_id != INVALID_DEV_ID)
				{

					memcpy(&ictl_arg.bdev, pbdev, sizeof(struct bdevice));
					ret = bdd_s5bd_ioctl(&ictl_arg, LIST_DEVICE);
					if (ret == 0 && ictl_arg.retval == 0)
					{
						memcpy(&bdd_msg->dinfo[bdd_msg->device_info_num ++],
								&ictl_arg.bdev.dinfo, sizeof(struct device_info));
						continue;
					}
					else
					{
						bdd_msg->status = (ret != 0) ? BDD_MSG_STATUS_IOCTL_FAILED
							: ictl_arg.retval;
						LOG_ERROR("Failed to list devices, ret(%d), ioctl ret(%d).",
								ret, ictl_arg.retval);
						break;
					}
				}
            }
			LOG_INFO("list device number %d.", bdd_msg->device_info_num);
            break;

        default:
            assert(0);
            break;
    }
}

static int bdd_write_record()
{
	int fd;
	int ret;

	/* rewrite whole mgt_device_ctx */
	if ((fd = open(RECORD_FILE, O_CREAT|O_SYNC|O_RDWR)) == -1)
	{
		LOG_ERROR("failed to open file, ret(%d)", errno);
		return -1;
	}

	if (lseek(fd, 0, SEEK_SET) == -1)
	{
		LOG_ERROR("failed to seek file error.");
		return -1;
	}

	if ((ret = write(fd, mgt_device_ctx, RECORD_LEN)) != RECORD_LEN)
	{
		LOG_ERROR("failed to write file error %d.", ret);
		return -1;
	}

	close(fd);
	return 0;
}

int bdd_check_recordfile (void)
{
	int fd;
	int ret;
	int i;
	struct bdevice *bdev;


	/* check if the RECORD_FILE already exit */
	if ((fd = open (RECORD_FILE, O_SYNC|O_RDWR)) == -1)
	{

		LOG_INFO("init mgt_device_ctx.");
		for (i = 0; i < MAX_DEVICE_NUM; i++)
		{
			memset(&mgt_device_ctx[i], 0, sizeof(struct bdevice));
			mgt_device_ctx[i].bddev_id = INVALID_DEV_ID;
		}
		return 0;
	}
	struct stat file_stat;
	fstat(fd, &file_stat);
	if (file_stat.st_size != RECORD_LEN)
	{
		LOG_ERROR("Record file length check failed, skip loading.");
		for (i = 0; i < MAX_DEVICE_NUM; i++)
		{
			memset(&mgt_device_ctx[i], 0, sizeof(struct bdevice));
			mgt_device_ctx[i].bddev_id = INVALID_DEV_ID;
		}
		close(fd);
		return 0;
	}

	/* read configuration data from the file */
	if ((ret = read (fd, mgt_device_ctx, RECORD_LEN)) != RECORD_LEN)
	{
		LOG_ERROR("failed to read file, length error %d.", ret);
		return -1;
	}

	LOG_INFO("read out mgt_device_ctx.");

	for(i = 0; i < MAX_DEVICE_NUM; ++i)
	{
		bdev = &mgt_device_ctx[i];
		if(bdev->bddev_id == INVALID_DEV_ID)
			continue;
		LOG_INFO("\nbddev_id(%d)-toe_mode(%d)\n",
				bdev->bddev_id, bdev->dinfo.toe_use_mode);

		switch(bdev->dinfo.toe_use_mode)
		{
			case TOE_MODE_DEBUG:
				LOG_INFO("ip(0x%x)-port(%d)-vid(%lu)-vsize(%lu).\n",
						bdev->dinfo.mode_debug.toe_ip,
						bdev->dinfo.mode_debug.toe_port,
						bdev->dinfo.mode_debug.volume_id,
						bdev->dinfo.mode_debug.volume_size);
				break;
			case TOE_MODE_CONDUCTOR:
				LOG_INFO("vname(%s)-tname(%s)-dname(%s)-" \
						"cdt1-ip(0x%x)-cdt1-port(%d)-" \
						"cdt2-ip(0x%x)-cdt2-port(%d)",
						bdev->dinfo.mode_conductor.volume_name,
						bdev->dinfo.mode_conductor.tenant_name,
						bdev->dinfo.dev_name,
						bdev->dinfo.mode_conductor.conductor_list[0].front_ip,
						bdev->dinfo.mode_conductor.conductor_list[0].front_port,
						bdev->dinfo.mode_conductor.conductor_list[1].front_ip,
						bdev->dinfo.mode_conductor.conductor_list[1].front_port);
				break;
			default:
				break;
		}
	}

	return 1;
}

static BOOL bdd_check_disk_installed(const char *dev_name)
{
	char full_dev_name[MAX_DEVICE_NAME_LEN + 16];
	memset(full_dev_name, 0, MAX_DEVICE_NAME_LEN + 16);
	sprintf(full_dev_name, "/dev/%s", dev_name);
	if(-1 == access(full_dev_name, F_OK))
	{
		LOG_INFO("failed to open %s.", full_dev_name);
		return FALSE;
	}

	LOG_INFO("%s already installed.", dev_name);
	return TRUE;
}

int bdd_rebuild_diskfrom_recordfile()
{
	int i;
	int ret;
	struct ioctlparam ictl_arg;
	struct bdevice *bdev = NULL;

	for (i = 0 ; i < MAX_DEVICE_NUM; i ++)
	{
		bdev = &mgt_device_ctx[i];
		if (((bdev->bddev_id != INVALID_DEV_ID) &&
			bdd_check_disk_installed(bdev->dinfo.dev_name) == FALSE))
		{
			memset(&ictl_arg, 0, sizeof(struct ioctlparam));
			memcpy(&ictl_arg.bdev, bdev, sizeof(struct bdevice));
			assert(i == bdev->bddev_id);

			LOG_INFO("rebuild map s5bd %d.", bdev->bddev_id);

			ret = bdd_s5bd_ioctl(&ictl_arg, MAP_DEVICE);
			if (ret == 0)
			{
				/*bdd_write_record();*/
			}
			else
			{
				LOG_ERROR("failed to rebuild s5bd device %d.", ret);
				bdd_release_bddev(bdev);
//				return -1;
			}
		}

	}

	return 0;
}

int bdd_recv_command (struct epoll_event * event)
{
	int count;
	count = recv(event->data.fd, command_buffer, MAX_BDD_MESSAGE_LEN,
		MSG_NOSIGNAL|MSG_WAITALL);
	if(count != MAX_BDD_MESSAGE_LEN)
	{
		LOG_WARN("failed to recv from fd(%d), count(%d).", event->data.fd, count);
		return -1;
	}
	return 0;
}

int bdd_reply_command (struct epoll_event * event)
{
	int count;

	struct bdd_message * reply_msg = (struct bdd_message * )command_buffer;

	reply_msg->msg_type += 1;

	count = send(event->data.fd, command_buffer, MAX_BDD_MESSAGE_LEN,
			MSG_NOSIGNAL|MSG_WAITALL);
	if(count != MAX_BDD_MESSAGE_LEN)
	{
		LOG_WARN("failed to send to fd(%d) .count(%d).", event->data.fd, count);
		return -1;
	}
	return 0;
}

void print_usage()
{
	printf("Usage: bdd [-d]\n");
	printf("\t-d\toptional parameter, daemon mode\n");
}

int main(int argc, char *argv[])
{
    int sfd, ret;
    int efd;
    int fd;
    struct epoll_event event, event_timer;
    struct epoll_event events[MAXEVENTS];

	/* runing mode */

	if(argc == 1)
	{
		LOG_INFO("normal mode.");
	}
	else if(argc == 2 && strcmp(argv[1], "-d") == 0)
	{
		LOG_INFO("daemon mode.");
	}
	else
	{
		print_usage();
		if(argc == 2 && (strcmp(argv[1], "-h") == 0
					|| strcmp(argv[1], "--help") == 0))
			return 0;
		else
			return -1;
	}

	memset(events, 0, MAXEVENTS * sizeof(struct epoll_event));

	/* only one bdd exists. */
	if ((fd = open(BDDAEMONLOCK, O_RDONLY|O_CREAT)) == -1)
	{
		LOG_ERROR("failed to open %s, ret(%d)", BDDAEMONLOCK, errno);
		return -1;
	}
	ret = flock(fd, LOCK_EX | LOCK_NB);
	if(ret == 0)
	{
		LOG_INFO("bdd init...");
	}
	else if(ret == -1)
	{
		LOG_ERROR("an bdd instance exist.");
		return -1;
	}
	else
		assert(0);

	ret = bdd_check_recordfile();
	if (ret < 0)
		return -1;

	if (ret == 1)
	{
		if (bdd_rebuild_diskfrom_recordfile() < 0)
			return -1;
	}

    sfd = bdd_create_socket(BDDAEMON);
    if (sfd == -1)
        abort();

    ret = bdd_set_socket(sfd);
    if(ret == -1)
        abort();

    ret = listen(sfd, SOMAXCONN);
    if(ret == -1)
    {
        perror("ERROR listen " BDDAEMON);
        abort();
    }

    efd = epoll_create1(0);

    if(efd == -1)
    {
        perror("ERROR: epoll_create");
        abort();
    }

    event.data.fd = sfd;
    event.events = EPOLLIN;
    ret = epoll_ctl(efd, EPOLL_CTL_ADD, sfd, &event);

    if(ret == -1)
    {
        perror("ERROR: event epoll_ctl");
        abort();
    }

    int timerfd = bdd_spy_timer();
    if(timerfd == -1)
    {
        perror("ERROR: timer event");
        abort();
    }
    event_timer.data.fd = timerfd;
    event_timer.events = EPOLLIN;
    ret = epoll_ctl(efd, EPOLL_CTL_ADD, timerfd, &event_timer);

    if(ret == -1)
    {
        perror("ERROR: event timer epoll_ctl");
        abort();
    }

	if(argc == 2 && strcmp(argv[1], "-d") == 0)
	{
		daemon(1, 0);
	}

    while(1)
    {
        int n, i;

        n = epoll_wait(efd, events, MAXEVENTS, -1);

        for(i = 0; i < n; i++)
        {
            if ((events[i].events & EPOLLERR) ||
            	(events[i].events & EPOLLHUP) ||
                (!(events[i].events & EPOLLIN)))
            {
                close(events[i].data.fd);
                continue;
            }
            else if(sfd == events[i].data.fd)
            {
                while(1)
                {
                    struct sockaddr in_addr;
                    socklen_t in_len;
                    int infd;
                    char hbuf[NI_MAXHOST], sbuf[NI_MAXSERV];

                    in_len = sizeof in_addr;
                    infd = accept(sfd, &in_addr, &in_len);

                    if (infd == -1)
                    {
                        if ((errno == EAGAIN) ||
                        	(errno == EWOULDBLOCK))
                        {
                            /* We have processed all incoming connections. */
                            break;
                        }
                        else
                        {
                            perror("accept");
                            break;
                        }
                    }

                    ret = getnameinfo(&in_addr, in_len,
                    	hbuf, sizeof hbuf,
                    	sbuf, sizeof sbuf,
                    	NI_NUMERICHOST | NI_NUMERICSERV);

                    if(ret == 0)
                    {
                        /*printf("Open connection on descriptor %d "
                               "(host=%s)\n", infd, hbuf);*/
                    }

                    event.data.fd = infd;
                    event.events = EPOLLIN;
                    ret = epoll_ctl(efd, EPOLL_CTL_ADD, infd, &event);

                    if(ret == -1)
                    {
                        perror("epoll_ctl");
                        abort();
                    }
                }

                continue;
            }
            else if(timerfd == events[i].data.fd)
            {
                /*wake up every 2 seconds to send udp message. */
                while(1)
                {
                    ssize_t count;
                    uint64_t num;

                    count = read(events[i].data.fd, &num, sizeof(uint64_t));

                    if(count == -1)
                    {
                        /* If errno == EAGAIN, that means we have read all
                           data. So go back to the main loop. */
                        if(errno != EAGAIN)
                        {
                            perror("read");
                        }

                        break;
                    }
                    else if(count == sizeof(uint64_t))
                    {
                        count = read(events[i].data.fd, &num, sizeof(uint64_t));
//                      printf("read again count = %zd, errno = %d.\n", count, errno);
                        break;
                    }
                    else
                    {
                        LOG_ERROR("read length %zu error.", count);
                        abort();
                    }
                }
            }
            else
            {
				if (bdd_recv_command(&events[i]) == -1)
					goto err_socket;

				struct bdd_message * reply_msg = (struct bdd_message * )command_buffer;
				if(reply_msg->version == VERSION_NUM)
				{
					(void) bdd_exec_command();
				}
				else
				{
					reply_msg->version = VERSION_NUM;
					reply_msg->status = BDD_MSG_STATUS_VERSION_NOT_MATCH;
				}

				if (bdd_reply_command(&events[i]) == -1)
					goto err_socket;

			err_socket:
                /*printf("Closed connection on descriptor %d\n", events[i].data.fd);*/
                close(events[i].data.fd);
            }
        }
    }

    close(sfd);
	flock(fd, LOCK_UN);
	close(fd);

    return EXIT_SUCCESS;
}

