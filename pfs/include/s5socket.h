/*
 * Copyright (C) 2016 Liu Lele(liu_lele@126.com)
 *
 * This code is licensed under the GPL.
 */
#ifndef __s5socket__
#define __s5socket__

/**
* Copyright (C), 2014-2015.
* @file
* S5Scocket C API.
*
* This file includes all s5socket data structures and interfaces, which are used by S5 modules.
*/



#include <string.h>
#include "s5list.h"
#include "s5message.h"
#include "pf_utils.h"

/**
 * Receive message hook of s5socket.
 *
 * the first param is socket,second is s5message, and the third is hook's handle_param.
 */
typedef int(*recv_msg_handle)(void*, pf_message_t*, void*);


/**
 * Connection exception hook of s5socket
 *
 * the first param is socketParam,second is handle_param.
 */
typedef int(*s5connection_exception_handle)(void*, void*);

/**
 * The type of s5socket connection, used in API s5socket_connect.
 */
typedef enum pf_connect_type
{
	CONNECT_TYPE_TEMPORARY		=0,	///< temporary connection,no keepalive thread.
	CONNECT_TYPE_STABLE 		=1,	///< stable connection, with keepalive thread,and will reconnect if need.
	CONNECT_TYPE_MAX
} pf_connect_type_t;

/**
 * the type of s5socket-connection receive-message, used in API s5socket_connect or s5socket_accept.
 */
typedef enum pf_rcv_type
{
	RCV_TYPE_MANUAL		=0,		///< no auto-receive thread, caller receive manually.
	RCV_TYPE_AUTO 		=1,		///< with auto-receive thread,auto receive s5message and calling recv_msg_handle.
	RCV_TYPE_MAX
} pf_rcv_type_t;

/**
 * the type of s5socket.
 */
typedef enum pf_socket_type
{
	SOCKET_TYPE_CLT		=0,		///< client s5socket.
	SOCKET_TYPE_SRV		=1,		///< server s5socket.
	SOCKET_TYPE_MAX
} pf_socket_type_t;

typedef void* PS5CLTSOCKET;  ///< the pointer type of client s5socket.
typedef void* PS5SRVSOCKET;  ///< the pointer type of server s5socket.


/**
 * Create a s5socket client or server.
 *
 * @param[in]	type				SOCKET_TYPE_CLT or SOCKET_TYPE_SRV.
 * @param[in]	local_port		only needed for server socket to bind.
 * @param[in] 	local_address		only needed for server socket to bind.
 * @return	pointer to socket.
 * @retval	NULL		Error occur when no memory or can not bind local_port.
 * @retval	non-NULL	success.
 */
void* s5socket_create(pf_socket_type_t type, unsigned short local_port, const char* local_address);

/**
 * Release a s5socket.
 *
 * @param[in,out] 	socket	pointer to pointer to s5socket. after release, *socket will be set to NULL.
 * @param[in]	type		SOCKET_TYPE_CLT or SOCKET_TYPE_SRV.
 * @return 	0 on success, negative error code on failure.
 * @retval	0			success.
 * @retval	-EINVAL		the param of type is invalid.
 */
int s5socket_release(void** socket, pf_socket_type_t type);

/**
 * Connect to a s5socket, only used in client-s5socket.
 *
 * @param[in] socket				pointer to client-s5socket.
 * @param[in] foreign_address0		the foreign ip 0 address want to connect.
 * @param[in] foreign_address1		the foreign ip 1 address want to connect.
 * @param[in] foreign_port0			the foreign port 0 want to connect.
 * @param[in] foreign_port1			the foreign port 1 want to connect.
 * @param[in] autorcv				RCV_TYPE_MANUAL or RCV_TYPE_AUTO.
 * @param[in] type 				CONNECT_TYPE_TEMPORARY or CONNECT_TYPE_STABLE.
 * @return 	0 					on success, negative error code on failure.
 * @retval	0					success.
 * @retval	-EINVAL				Socket is NULL.
 * @retval	EACCES				The user tried to connect to a broadcast address without having the socket broadcast flag enabled or the  connection
              						request failed because of a local firewall rule.
 * @retval	EADDRINUSE			Local address is already in use.
 * @retval	EAFNOSUPPORT		The passed address didn't have the correct address family in its sa_family field.
 * @retval	EADDRNOTAVAIL		Non-existent interface was requested or the requested address was not local.
 * @retval	EALREADY			The socket is non-blocking and a previous connection attempt has not yet been completed.
 * @retval	EBADF				The file descriptor is not a valid index in the descriptor table.
 * @retval	ECONNREFUSED		No-one listening on the remote address.
 * @retval	EFAULT 				The socket structure address is outside the user's address space.
 * @retval	EINTR  				The system call was interrupted by a signal that was caught; see signal(7).
 * @retval	ENETUNREACH		Network is unreachable.
 * @retval	ENOTSOCK			The file descriptor is not associated with a socket.
 * @retval	ETIMEDOUT			Timeout  while attempting connection.  The server may be too busy to accept new connections.
 								Note that for IP sockets the timeout may be very long when syncookies are enabled on the server.
 * @retval	EPERM				The user tried to connect to a broadcast address without having the socket broadcast flag enabled or the  connection
              						request failed because of a local firewall rule.
 */
int s5socket_connect(PS5CLTSOCKET socket, const char* foreign_address0, const char* foreign_address1,
                     unsigned short foreign_port0, unsigned short foreign_port1,
					 pf_rcv_type_t autorcv, pf_connect_type_t type);

/**
 * Accept a s5socket, only used in server-s5socket.
 *
 * @param[in] socket	 	pointer to server-s5socket.
 * @param[in] autorcv 	RCV_TYPE_MANUAL or RCV_TYPE_AUTO.
 * @return	pointer to socket.
 * @retval	NULL		Error occur when param of srv is invalid.
 * @retval	non-NULL	success.
 */
PS5CLTSOCKET s5socket_accept(PS5SRVSOCKET socket, int autorcv);

/**
 * Register a hook to receive special type's  s5message for s5socket.
 *
 * @param[in] socket pointer to s5socket.
 * @param[in] type		SOCKET_TYPE_CLT or SOCKET_TYPE_SRV.
 * @param[in] msg_type	the type of s5message.
 * @param[in] recv_handle	the hook for receiving special s5message.
 * @param[in] recv_param		the param of hook.
 * @return	0 on success, negative error code on failure.
  * @retval	0			success.
 * @retval	-EINVAL		socket is invalid.
 * @retval	-EINVAL		type is invalid.
 * @retval	-EINVAL		msg_type is invalid.
 */
int s5socket_register_handle(void* socket, pf_socket_type_t type,
                             msg_type_t msg_type, recv_msg_handle recv_handle, void* recv_param);

/**
 * Unregister a hook for s5socket.
 *
 * @param[in] socket pointer to s5socket.
 * @param[in] type		SOCKET_TYPE_CLT or SOCKET_TYPE_SRV.
 * @param[in] msg_type	the type of s5message, search the hook referenced it.
 * @return	0 on success, negative error code on failure.
 * @retval	0			success.
 * @retval	-EINVAL		socket is invalid.
 * @retval	-EINVAL		type is invalid.
 * @retval	-EINVAL		msg_type is invalid.
 */
int s5socket_unreigster_handle(void* socket, pf_socket_type_t type,
                               msg_type_t msg_type);

/**
 * Send s5message.
 *
 * @param[in] socket pointer to client-s5socket.
 * @param[in] msg		pointer to s5message.
 * @return	0 on success, negative error code on failure.
 * @retval	0					success.
 * @retval	-EPERM		s5socket is used to handle API s5socket_send_msg_wait_reply.
 * @retval	-EINVAL		msg_type is invalid.
 * @retval	-EACCES (For Unix domain sockets, which are identified by pathname) Write permission is denied  on  the  destination  socket
              file, or search permission is denied for one of the directories the path prefix.  (See path_resolution(7).)
 * @retval	-EAGAIN or EWOULDBLOCK
              The  socket  is marked non-blocking and the requested operation would block.  POSIX.1-2001 allows either error to be
              returned for this case, and does not require these constants to have the  same  value,  so  a  portable  application
              should check for both possibilities.
 * @retval	-EBADF  An invalid descriptor was specified.
 * @retval	-ECONNRESET Connection reset by peer.
 * @retval	-EDESTADDRREQ The socket is not connection-mode, and no peer address is set.
 * @retval	-EFAULT An invalid user space address was specified for an argument.
 * @retval	-EINVAL Invalid argument passed.
 * @retval	-EISCONN The connection-mode socket was connected already but a recipient was specified.  (Now either this error is returned,
              or the recipient specification is ignored.)
 * @retval	-EMSGSIZE The socket type requires that message be sent atomically, and the size of the message to be sent made this  impossi-
              ble.
 * @retval	-ENOBUFS The output queue for a network interface was full.  This generally indicates that the interface has stopped sending,
              but may be caused by transient congestion.  (Normally, this does not occur in  Linux.   Packets  are  just  silently
              dropped when a device queue overflows.)
 * @retval	-ENOMEM No memory available.
 * @retval	-ENOTCONN The socket is not connected, and no target has been given.
 * @retval	-ENOTSOCK The argument sockfd is not a socket.
 * @retval	-EOPNOTSUPP Some bit in the flags argument is inappropriate for the socket type.
 * @retval	-EPIPE  The local end has been shut down on a connection oriented socket.  In this case the process will also receive a SIG-
              PIPE unless MSG_NOSIGNAL is set.
 */
int s5socket_send_msg(PS5CLTSOCKET socket, pf_message_t *msg);

/**
 * Send s5message and receive a reply s5message.
 *
 * @param[in] socket pointer to client-s5socket.
 * @param[in] msg		pointer to s5message.
 * @return	pointer to reply s5message.
 * @retval	NULL		Error occur when param of socket is invalid.
 * @retval	NULL		Error occur when param of msg is invalid.
 * @retval	non-NULL	success.
 */
pf_message_t* s5socket_send_msg_wait_reply(PS5CLTSOCKET socket, pf_message_t *msg);

/**
 * Get the foreign ip of remote s5socket connected with the local s5socket.
 *
 * @param[in] socket pointer to client-s5socket.
 * @return	pointer to ip address.
 * @retval	NULL		error occur when param of socket is invalid.
 * @retval	non-NULL	success.
 */
char* s5socket_get_foreign_ip(PS5CLTSOCKET socket);

/**
 * Get the foreign port of remote s5socket connected with the local s5socket.
 *
 * @param[in] socket pointer to client-s5socket.
 * @return	bigger than 0 on success, negative error code on failure.
 * @retval	bigger than 0 success.
 * @retval	-EINVAL		socket is invalid or addrlen is invalid (e.g., is negative).
 * @retval	-EFAULT		The addr argument points to memory not in a valid part of the process address space.
 * @retval	-ENOBUFS	Insufficient resources were available in the system to perform the operation.
 * @retval	-ENOTCONN	The socket is not connected.
 * @retval	-ENOTSOCK	The argument sockfd is a file, not a socket.
 */
int s5socket_get_foreign_port(PS5CLTSOCKET socket);

/**
 * Set user's private user_data to special field of s5socket.
 *
 * @param[in] socket pointer to client-s5socket.
 * @param[in] user_data pointer to user's private data.
 * @return	0 on success, negative error code on failure.
 * @retval	0					success.
 * @retval	-EINVAL		socket is invalid.
 */
int s5socket_set_user_data(PS5CLTSOCKET socket, void* user_data);

/**
 * Get user's private user_data from special field of s5socket.
 *
 * @param[in] socket pointer to client-s5socket.
 * @return	pointer to user's private data.
 * @retval	NULL		Error occur when param of socket is invalid.
 * @retval	non-NULL	success.
 */
void* s5socket_get_user_data(PS5CLTSOCKET socket);


/**
 * Register a hook for receiving exception when stable connection can not keepalive normally.
 *
 * @param[in] socket pointer to client-s5socket.
 * @param[in] exc_handle pointer to hook used to handle exception.
 * @param[in] handle_param pointer to hook's param.
 * @return	0 on success, negative error code on failure.
 * @retval	0			success.
 * @retval	-EINVAL		socket is invalid.
 */
int s5socket_register_conn_exception_handle(PS5CLTSOCKET socket, s5connection_exception_handle exc_handle, void* handle_param);

/**
 * Unregister a hook for receiving exception when stable connection can not keepalive normally.
 *
 * @param[in] socket pointer to client-s5socket.
 * @return	0 on success, negative error code on failure.
 * @retval	0			success.
 * @retval	-EINVAL		socket is invalid.
 */
int s5socket_unregister_conn_exception_handle(PS5CLTSOCKET socket);

/**
 * Create reaper thread.
 *
 * Reaper thread is used to clean up the resources of threads which are
 * finished, but not detached or joined.
 *
 * @param[out] reap_socket_thread	The thread id to reap socket thread.
 * @return Return the result of pthread_create.
 */
int s5socket_create_reaper_thread(pthread_t* reap_socket_thread);

/**
 * Get the flag whether associated socket is init or not.
 * 
 * @param[in]	socket	Client socket pointer. This pointer should be managed by the user.
 * @return	Whether the handler is initialized or not.
 * @retval	True	Iinitialized.
 * @retval	False	Uninitialized.
 */ 
BOOL s5socket_get_handler_init_flag(PS5CLTSOCKET socket);

/**
 * Set the initialization flag of one socket handler as specified.
 * 
 * @param[in]	socket	Client socket pointer. This pointer should be managed by the user.
 * @param[in]	init	Set as initialized or not
 * @return  No return.
 */ 
void s5socket_set_handler_init_flag(PS5CLTSOCKET socket, BOOL init);

/**
 * Lock the handler of input socket.
 * 
 * @param[in]   socket  Client socket pointer. This pointer should be managed by the user.
 * @return  No return.
 */ 
void s5socket_lock_handler_mutex(PS5CLTSOCKET socket);

/**
 * Unlock the handler of input socket.
 * 
 * @param[in]   socket  Client socket pointer. This pointer should be managed by the user.
 * @return  No return.
 */
void s5socket_unlock_handler_mutex(PS5CLTSOCKET socket);

/**
 * Wait the conditional variable of the handler associated to input socket.
 * 
 * @param[in]   socket  Client socket pointer. This pointer should be managed by the user.
 * @return  No return. 
 */
void s5socket_wait_cond(PS5CLTSOCKET socket);

/**
 * Wake up the conditional variable of the handler associated to input socket.
 * 
 * @param[in]   socket  Client socket pointer. This pointer should be managed by the user.
 * @return  No return. 
 */
void s5socket_signal_cond(PS5CLTSOCKET socket);



#endif	/*__s5socket__*/

