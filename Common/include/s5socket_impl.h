
#ifndef __S5SOCKET__
#define __S5SOCKET__

#ifdef __cplusplus
extern "C" {
#endif

#include <string.h>
#include "s5message.h"
#include "s5socket.h"
#include "s5list.h"

/**
 * The times of retry for reconnect
 */
#define	RETRY_CONN_TIMES	5

/**
 * Sleep time before reconnect. Unit: second
 */ 
#define	RETRY_SLEEP_TIME	1

/**
 * Collect the resource of one thread when it quits
 * 
 * @param[in]	pid	The thread needs to be joined
 * @return	No return. 
 */
void s5_wait_thread_end(pthread_t pid);

/**
 * The thread function of reaper thread. It is used to join the ended socket thread.
 *  
 * @param[in] param	The pointer to a thread id
 * @return NULL
 */
void *reaper_handle_thread(void *param);

/**
 * The entry in the reap_thread_list_head
 */
typedef struct s5_reap_thread_entry 
{
    s5_dlist_entry_t    thread_list_entry;	///< The real entry in a dlist
    pthread_t           thread_id;			///< The thread id of each reap entry
} s5_reap_thread_entry_t;

/**
 * The base function of S5 socket. 
 * 
 * The user is not allowed to create an instance for Socket.
 */
class Socket 
{
public:
	/**
	 * Destructor
	 */
	virtual ~Socket();

	/** 
     * Close the socket descriptor, and generate a new socket descriptor for S5 Socket.
     * 
     * This function is used when try to reconnect foreign address.
	 * 
	 * @return	No return.
     */
    void resetFd();
	
	/**
	 * Get the address of this Socket.
	 *
	 * To get detailed error info, user can check errorno.
     * ERRORS:
	 * @li	\b -EBADF  The argument sockfd is not a valid descriptor.
     * @li	\b -EFAULT The addr argument points to memory not in a valid part of the process address space.
     * @li	\b -EINVAL addrlen is invalid (e.g., is negative).
	 * @li	\b -ENOBUFS Insufficient resources were available in the system to perform the operation.
	 * @li	\b -ENOTSOCK The argument sockfd is a file, not a socket.
	 *
	 * @return the address.
	 * @retval char*	When success.
	 * @retval NULL	When the address not specified yet.
	 */	
	char* getLocalAddress();

	/**
	 * Get the port of this Socket.
	 *
     * To get detailed error info, user can check errorno.
     * ERRORS:
     * @li  \b -EBADF  The argument sockfd is not a valid descriptor.
     * @li  \b -EFAULT The addr argument points to memory not in a valid part of the process address space.
     * @li  \b -EINVAL addrlen is invalid (e.g., is negative).
     * @li  \b -ENOBUFS Insufficient resources were available in the system to perform the operation.
     * @li  \b -ENOTSOCK The argument sockfd is a file, not a socket.
	 *
	 * @return Get the local port.
	 * @retval int	The port number.
	 * @retval -1	Failed to get local port. 
	 */
	int getLocalPort();
   
	/**
	 * Set the port of current Socket.
	 *
	 * To get detailed error info, user can check errorno.
	 * ERRORS:
     * @li	\b -EACCES The address is protected, and the user is not the superuser.
	 * @li	\b -EADDRINUSE The given address is already in use.
	 * @li	\b -EBADF  sockfd is not a valid descriptor.
	 * @li	\b -EINVAL The socket is already bound to an address.
	 * @li	\b -ENOTSOCK sockfd is a descriptor for a file, not a socket.
	 *
	 * @param[in]	localPort	Set local port.
	 * @return	0	Success
	 * @return  -1  Failed to bind this port  
	 */
	int setLocalPort(unsigned short localPort);
	
	/** 
     * Set the address and port of current Socket.
     *
	 * To get detailed error info, user can check errorno.
     * ERRORS:
     * @li  \b -EACCES The address is protected, and the user is not the superuser.
     * @li  \b -EADDRINUSE The given address is already in use.
     * @li  \b -EBADF  sockfd is not a valid descriptor.
     * @li  \b -EINVAL The socket is already bound to an address.
     * @li  \b -ENOTSOCK sockfd is a descriptor for a file, not a socket.
	 * 
	 * @param[in]	localAddress Set the local address.
     * @param[in]   localPort   Set local port.
     * @return  0   Success
     * @return  -1  Failed to bind this port  
     */	
	int setLocalAddressAndPort(const char* localAddress, unsigned short localPort = 0);

	/**
	 * Shut down the oonnection, and close the socket descriptor.
	 *
	 * @return No return.
	 */
	void closeSocketDesc();

	/**
	 * Register the message handler for one message.
	 *
	 * @param[in] type	Message type.
	 * @param[in] recv_handle	The handler function will be called when receive the type of the message.
	 * @param[in] recv_param	The parameter for the message handler function
	 * @return	0	Success
	 * @return	-EINVAL	When input message type is invalid.
	 */	
	int register_recv_handle(msg_type_t type, recv_msg_handle recv_handle, void* recv_param);

	/** 
     * Unregister the message handler for one message.
     *
     * @param[in] type  Message type.
     * @return  0   Success
     * @return  -EINVAL When input message type is invalid.
     */ 	
	int unregister_recv_handle(msg_type_t type);

public:	
	volatile int sockDesc;              ///< Socket descriptor
	int type;							///< Socket type, such as SOCK_STREAM, SOCK_DGRAM					
	int protocol;						///< Socket protocal IPPROTO_TCP, OPPROTO_UDP
	recv_msg_handle   recv_handle_arr[MSG_TYPE_MAX];	///< The array of message handler.
	void*             recv_handle_param[MSG_TYPE_MAX];	///< The array of message handler param.
private:
	/**
	 * Prevent the user from trying to use value semantics on this object
	 * 
	 * @param[in] sock	The reference to the copied Socket.
	 * @return	No  return.
	 */
	Socket(const Socket &sock);

	/**
     * Prevent the user from trying to use value semantics on this object
     *
	 * @param[in]	sock	The reference to be assigned.
	 * @return No return.
	 */
	void operator=(const Socket &sock);

protected:

	/**
	 * Constructor
	 * 
	 * @param[in]	type	int, socket type 
	 * @param[in]	protocol	int, socket protocol
	 * @return	No return.
	 */	
	Socket(int type, int protocol);

	/**
     * Constructor
     * 
     * @param[in]   sockDesc    Socket descriptor 
	 * @return No return.
     */
	Socket(int sockDesc);
};

/**
 *   Socket which is able to connect, send, and receive
 */
class CommunicatingSocket : public Socket {
public:
	/**
	 *  Client connect function.
	 *
	 * To get detailed error info, user can check errorno.
     * ERRORS
     * @li  \b  -EACCES For Unix domain sockets, which are identified by pathname.
     * @li  \b  -EPERM  The user tried to connect to a broadcast address without having the 
     *                  socket broadcast flag enabled or the connection request failed because of a local firewall rule.
     * @li  \b  -EADDRINUSE  Local address is already in use.
     * @li  \b  -EAFNOSUPPORT   The passed address didn’t have the correct address family in its sa_family field
     * @li  \b  -EADDRNOTAVAIL   Non-existent interface was requested or the requested address was not local.
     * @li  \b  -EALREADY  The socket is non-blocking and a previous connection attempt has not yet been completed.
     * @li  \b  -EBADF  The file descriptor is not a valid index in the descriptor table.
     * @li  \b  -ECONNREFUSE     No-one listening on the remote address.
     * @li  \b  -EFAULT The socket structure address is outside the user’s address space
     * @li  \b  -EINPROGRESS The  socket is non-blocking and the connection cannot be completed immediately.  It is possible to select(2) or poll(2) for completion by selecting the socket for writing.  
     * @li  \b  -EINTR  The system call was interrupted by a signal that was caught; see signal(7).
     * @li  \b  -EISCONN    The socket is already connected.
     * @li  \b  -ENETUNREACH Network is unreachable.
     * @li  \b  -ENOTSOCK The file descriptor is not associated with a socket.
     * @li  \b  -ETIMEDOUT Timeout while attempting connection.  The server may be too busy to accept new connections.  Note that for IP sockets the timeout may be very long when syncookies are enabled on the server. 
     *
	 * @param[in]	foreignAddress	Foreign address which will connect to.
	 * @param[in]	foreignPort		Foreign port which will connect to.
	 * @return	   If the connection or binding succeeds, zero is returned.  
	 * @retval	   -1 When error.
	 */
	int _connect(const char* foreignAddress, unsigned short foreignPort);
	
	/**
	 * The internal implementation of send_msg.
	 * 
	 * @param[in] msg	The message will be sent.
	 * @return 0	when send successfully.
	 * @retval -EACCES	Write permission is denied on the destination socket file, or search permission is denied for one of the directories the path prefix.
     * @retval -EAGAIN or -EWOULDBLOCK  The socket is marked non-blocking and the requested operation would block.  
	 * @retval -EBADF  An invalid descriptor was specified.
	 * @retval -ECONNRESET Connection reset by peer.
	 * @retval -EDESTADDRREQ The socket is not connection-mode, and no peer address is set.
	 * @retval -EFAULT An invalid user space address was specified for an argument.
	 * @retval -EINTR  A signal occurred before any data was transmitted.
     * @retval -EINVAL Invalid argument passed.
     * @retval -EISCONN The connection-mode socket was connected already but a recipient was specified.
	 * @retval -EMSGSIZE The socket type requires that message be sent atomically, and the size of the message to be sent made this impossible.
	 * @retval -ENOBUFS The  output  queue  for  a network interface was full. 
	 * @retval -ENOMEM No memory available.
	 * @retval -ENOTCONN The socket is not connected, and no target has been given.
	 * @retval -ENOTSOCK The argument sockfd is not a socket.
	 * @retval -EOPNOTSUPP Some bit in the flags argument is inappropriate for the socket type.
	 * @retval -EPIPE  The local end has been shut down on a connection oriented socket.  
	 */	
	int send_msg_internal(const s5_message_t *msg);

	/** 
     * The internal implementation of receive_msg.
     *
	 * @li	\b	-EAGAIN or EWOULDBLOCK The socket is marked non-blocking and the receive operation would block, or a receive timeout had been set and the timeout expired before data was received.  
	 * @li	\b	-EBADF  The argument sockfd is an invalid descriptor.
	 * @li	\b	-ECONNREFUSED A remote host refused to allow the network connection (typically because it is not running the requested service).
	 * @li	\b	-EFAULT The receive buffer pointer(s) point outside the process’s address space.
	 * @li	\b	-EINTR  The receive was interrupted by delivery of a signal before any data were available; see signal(7).
	 * @li	\b	-EINVAL Invalid argument passed.
	 * @li	\b	-ENOMEM Could not allocate memory for recvmsg().
	 * @li	\b	-ENOTCONN The socket is associated with a connection-oriented protocol and has not been connected (see connect(2) and accept(2)).
	 * @li	\b	-ENOTSOCK The argument sockfd does not refer to a socket.
	 * 
     * @return Receive the message.
	 * @retval NULL Error when receive. 
     */ 	
	s5_message_t*   recv_msg_internal();
  
	/**
   	 * Send message not need to care it's reply.
   	 * it's reply will be received in recv_handle had registered.
     * it can not be co-used with send_msg_wait_reply.
	 *
     * @param[in] msg want to send.
	 * @return 0    when send successfully.
     * @retval -EACCES  Write permission is denied on the destination socket file, or search permission is denied for one of the directories the path prefix.
     * @retval -EAGAIN or -EWOULDBLOCK  The socket is marked non-blocking and the requested operation would block.  
     * @retval -EBADF  An invalid descriptor was specified.
     * @retval -ECONNRESET Connection reset by peer.
     * @retval -EDESTADDRREQ The socket is not connection-mode, and no peer address is set.
     * @retval -EFAULT An invalid user space address was specified for an argument.
     * @retval -EINTR  A signal occurred before any data was transmitted.
     * @retval -EINVAL Invalid argument passed.
     * @retval -EISCONN The connection-mode socket was connected already but a recipient was specified.
     * @retval -EMSGSIZE The socket type requires that message be sent atomically, and the size of the message to be sent made this impossible.
     * @retval -ENOBUFS The  output  queue  for  a network interface was full. 
     * @retval -ENOMEM No memory available.
     * @retval -ENOTCONN The socket is not connected, and no target has been given.
     * @retval -ENOTSOCK The argument sockfd is not a socket.
     * @retval -EOPNOTSUPP Some bit in the flags argument is inappropriate for the socket type.
     * @retval -EPIPE  The local end has been shut down on a connection oriented socket.  
	 */
	int send_msg(s5_message_t *msg);	
  
  	/**
   	 * Send message must wait it's reply.
   	 * it can not be co-used with send_msg.
	 * 
	 * @param[in]	msg	The message needs to send.
	 * @return The reply message.
	 * @retval NULL	Error when input msg is NULL, or receive no message.
	 */ 
	s5_message_t* send_msg_wait_reply(const s5_message_t *msg);	
  
	/**
	 * Get the foreign address, which needs to connect to.
	 *
	 * To get detailed error info, user can check errorno.
	 * ERRORS:
     * @li  \b  -EBADF  The argument sockfd is not a valid descriptor.
     * @li  \b  -EFAULT The addr argument points to memory not in a valid part of the process address space.
     * @li  \b  -EINVAL addrlen is invalid (e.g., is negative).
     * @li  \b  -ENOBUFS Insufficient resources were available in the system to perform the operation.
     * @li  \b  -ENOTCONN The socket is not connected.
     * @li  \b  -ENOTSOCK The argument sockfd is a file, not a socket. 
	 *
	 * @return foreign address
	 * @retval NULL when failed
	 */
	char* getForeignAddress();
  
 	/** 
     * Get the foreign address, which needs to connect to.
     *
	 * To get detailed error info, user can check errorno.
	 * ERRORS:
	 * @li	\b	-EBADF  The argument sockfd is not a valid descriptor.
     * @li	\b	-EFAULT The addr argument points to memory not in a valid part of the process address space.
	 * @li	\b	-EINVAL addrlen is invalid (e.g., is negative).
	 * @li	\b	-ENOBUFS Insufficient resources were available in the system to perform the operation.
	 * @li	\b	-ENOTCONN The socket is not connected.
	 * @li	\b	-ENOTSOCK The argument sockfd is a file, not a socket.
	 * 
     * @return foreign port
     * @retval -1 when failed
     */		
	int getForeignPort();

public:
	pthread_mutex_t lock_send; ///< The lock when send message  

protected:

	/**
	 * Constructor
	 *
	 * @param[in] type 	Socket type
	 * @param[in] protocol Socket protocal, such as IPPROTO_TCP, OPPROTO_UDP
	 */
	CommunicatingSocket(int type, int protocol);
	
    /** 
     * Constructor
	 * 
	 * @param[in] newConnSD	The new connnect socket descriptor for this socket.
     */
	CommunicatingSocket(int newConnSD);
	/**
	 * Destructor
	 */
	virtual ~CommunicatingSocket() {}

	pthread_mutex_t lock_recv; ///< The lock when receive message
};

/**
 * TCP socket for communication with other TCP sockets
 */
class S5TCPCltSocket : public CommunicatingSocket {
public:
	
	/**
	 * Construct a TCP socket with no connection
	 * 
	 * @return No return.
	 */
	S5TCPCltSocket();

	/**
	 * Destructor
	 */
	~S5TCPCltSocket();

	/**
	 * Construct a TCP socket with a connection to the given descriptor
	 *
	 * @param[in] newConnSD	givn descriptor
	 * @return No return.
	 */
	S5TCPCltSocket(int newConnSD);
  
	/**
	 * Client have two types.
	 *
	 * To get detailed error info, user can check errorno.
	 * ERRORS:
	 * @li	\b	-EACCES, -EPERM The user tried to connect to a broadcast address without having the socket broadcast flag enabled or the connection request failed because of a local firewall rule.
     * @li	\b	-EADDRINUSE	Local address is already in use.
	 * @li	\b	-EAFNOSUPPORT	The passed address didn’t have the correct address family in its sa_family field.
	 * @li	\b	-EADDRNOTAVAIL	Non-existent interface was requested or the requested address was not local.
	 * @li	\b	-EALREADY	The socket is non-blocking and a previous connection attempt has not yet been completed.
	 * @li	\b	-EBADF  The file descriptor is not a valid index in the descriptor table.
     * @li	\b	-ECONNREFUSED	No-one listening on the remote address.
	 * @li	\b	-EFAULT The socket structure address is outside the user’s address space.
	 * @li	\b	-EINPROGRESS The  socket is non-blocking and the connection cannot be completed immediately.  It is possible to select(2) or poll(2) for completion by selecting the socket for writing.  After select(2) indicates
     *                       writability, use getsockopt(2) to read the SO_ERROR option at level SOL_SOCKET to determine whether connect() completed successfully (SO_ERROR is zero) or unsuccessfully  (SO_ERROR  is  one  of  the
     *                       usual error codes listed here, explaining the reason for the failure).
	 * @li	\b	-EINTR  The system call was interrupted by a signal that was caught; see signal(7).
	 * @li	\b	-EISCONN	The socket is already connected.
	 * @li	\b	-ENETUNREACH	Network is unreachable.
	 * @li	\b	-ENOTSOCK	The file descriptor is not associated with a socket.
	 * @li	\b	-ETIMEDOUT	Timeout while attempting connection.  The server may be too busy to accept new connections.  Note that for IP sockets the timeout may be very long when syncookies are enabled on the server.
	 * 
	 * @param[in]	foreignAddress	The address to connect.
	 * @param[in]	foreignPort	The port to connect.
	 * @param[in]	autorcv	The type to receive masage reply. Default is RCV_TYPE_MANUAL.
	 * @param[in]	type	The type for the connection. Default is CONNECT_TYPE_TEMPORARY.
	 * @return	0	Success.
	 * @retval  -1	Failed.
	 */
	int connect(const char* foreignAddress, unsigned short foreignPort, s5_rcv_type_t autorcv = RCV_TYPE_MANUAL, s5_connect_type_t type = CONNECT_TYPE_TEMPORARY);

	pthread_t	  thread_recv_clt;	  ///< receive thread which to manage receiving s5_message_t s5tcpsocket.
	pthread_t	  thread_recv;		  ///< receive thread which to manage receiving s5_message_t s5tcpsocket of accept.
	pthread_t	  thread_keepalive;   ///< thread which handling keep-alive message.

	s5_rcv_type_t autorcv;			  ///< autoreceive flag, 1:auto 0:manual.
	s5_connect_type_t conntype;		  ///< CONNECT_TYPE_TEMPORARY or CONNECT_TYPE_STABLE
	int con_invalid;				  ///< 0: connection invalid, 1: connection valid.
	volatile int exitFlag;			  ///< The flag to exit working thread 
	char	foreign_addr[32];		  ///< The address will connect to.
	unsigned short foreign_port;	  ///< The port will connect to.
	s5connection_exception_handle exc_handle;	///< The handler for socket connect exception.
	void*	exc_handle_param;				    ///< The parameter for exception handler.
	void*	user_data;	///< The user data associated to this socket.
	static  s5_dlist_head_t  reap_thread_list_head; ///< The reaper thread for disconnected socket thread

	pthread_mutex_t handler_mutex;		 ///< The mutex for pthread_cond_t
	pthread_cond_t handler_cond;		 ///< The conditional varable for notify completion is done
	volatile BOOL handler_init;			 ///< Whether the handler is initialized or not

private:
	/**
	 * Initialize the handler lock of S5 Socket.
	 * 
	 * @return No return.
	 */
	void initMutexCond();
	
	/**
	 * Destroy the handler lock of S5 Socket.
	 *
	 * @return No return.
	 */
	void destroyMutexCond();
};

/**
 *   TCP socket class for the server
 */
class S5TCPServerSocket : public Socket {
public:
	/**
	 * Construct a TCP socket for use with a server, accepting connections
	 * on the specified port on any interface
	 * 
	 * @return No return.
	 */
	S5TCPServerSocket();

	/**
	 * Bind a TCP socket with a address, port and max connection number	
	 *
	 * To get detailed error info, user can check errorno.
     * ERRORS
     * @li  \b  -EAGAIN or EWOULDBLOCK  The socket is marked non-blocking and no connections are present to be accepted.  
     *                                  POSIX.1-2001 allows either error to be returned for this case, and does not require these constants to have the same
     *                                  value, so a portable application should check for both possibilities.
     * @li  \b  -EBADF  The descriptor is invalid.
     * @li  \b  -ECONNABORTED A connection has been aborted.
     * @li  \b  -EFAULT The addr argument is not in a writable part of the user address space.
     * @li  \b  -EINTR  The system call was interrupted by a signal that was caught before a valid connection arrived; see signal(7).
     * @li  \b  -EINVAL Socket is not listening for connections, or addrlen is invalid (e.g., is negative). Or, (accept4()) invalid value in flags.
     * @li  \b  -EMFILE The per-process limit of open file descriptors has been reached.
     * @li  \b  -ENFILE The system limit on the total number of open files has been reached.
     * @li  \b  -ENOBUFS, -ENOMEM  Not enough free memory.  This often means that the memory allocation is limited by the socket buffer limits, not by the system memory.
     * @li  \b  -ENOTSOCK The descriptor references a file, not a socket.
     * @li  \b  -EOPNOTSUPP The referenced socket is not of type SOCK_STREAM.
     * @li  \b  -EPROTO Protocol error.
     * @li  \b  -EPERM  Firewall rules forbid connection.
	 * 
	 * @param[in] localAddress the address of server socket.
	 * @param[in] localPort	the port of server socket.
	 * @param[in]	queueLen	the max connection number.
	 *
	 * @return 0 Success
	 * @return -1 Failed when bind or listen
	 */	
	int initServer(const char* localAddress, unsigned short localPort, int queueLen = 5);

  	/**
	 * Blocks until a new connection is established on this socket or error
	 *
	 * To get detailed error info, user can check errorno.
	 * ERRORS
     * @li	\b	-EAGAIN or EWOULDBLOCK	The socket is marked non-blocking and no connections are present to be accepted.  
	 *									POSIX.1-2001 allows either error to be returned for this case, and does not require these constants to have the same
     *									value, so a portable application should check for both possibilities.
	 * @li	\b	-EBADF  The descriptor is invalid.
	 * @li	\b	-ECONNABORTED A connection has been aborted.
	 * @li	\b	-EFAULT The addr argument is not in a writable part of the user address space.
     * @li	\b	-EINTR  The system call was interrupted by a signal that was caught before a valid connection arrived; see signal(7).
     * @li	\b  -EINVAL Socket is not listening for connections, or addrlen is invalid (e.g., is negative). Or, (accept4()) invalid value in flags.
     * @li	\b  -EMFILE The per-process limit of open file descriptors has been reached.
     * @li	\b	-ENFILE The system limit on the total number of open files has been reached.
     * @li	\b  -ENOBUFS, -ENOMEM  Not enough free memory.  This often means that the memory allocation is limited by the socket buffer limits, not by the system memory.
     * @li	\b	-ENOTSOCK The descriptor references a file, not a socket.
	 * @li	\b	-EOPNOTSUPP The referenced socket is not of type SOCK_STREAM.
     * @li  \b	-EPROTO Protocol error.
     * @li	\b	-EPERM  Firewall rules forbid connection.
	 *
	 * @param[in]	autorcv	receive type for this socket. Default is RCV_TYPE_MANUAL.
	 * @return new connection socket
	 * @return NILL when accept failed
	 */
	S5TCPCltSocket *accept(s5_rcv_type_t autorcv = RCV_TYPE_MANUAL);

private:
	/**
     * Set the status of this Socket from CLOSED to LISTEN
  	 * 
	 * If errors occur in process, -1 will be returned and errno is set appropriately. To get detailed error info, user 
	 * can check errorno.
	 * 
 	 * Error:
 	 * @li  \b -EADDRINUSE          Another socket is already listening on the same port.
 	 * @li  \b -EBADF				The argument sockfd is not a valid descriptor.
 	 * @li  \b -ENOTSOCK            The argument sockfd is not a socket.
 	 * @li  \b -EOPNOTSUPP			The socket is not of a type that supports the listen() operation.
	 * 
	 * @param[in]	queueLen	Set the max lineup connection number for this socket
	 * @return	0 Success
	 * @retval	-1 Failed
	 */
	int setListen(int queueLen = 5);
};


#ifdef __cplusplus
}
#endif


#endif	//__S5SOCKET__


