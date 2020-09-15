1. Object life cycle management

reference count is used to manage object life cycle. include:
1) Network connection
2) Client/Server IOCB


1.1 Network connection
  Here the connection not refers to TCP/RDMA connection object in OS kernel, but the object in PureFlash. i.e.
  the object of PfConnection. We can't not release PfConnection immediately on underlying connection closed
  because 1) There may still event in epoll processing loop. 2) There may still some event in dispatch queue
  that will need PfConnection in later process.
    
  will increased each time a BufferDescriptor is posted in.   
  will decreased when BufferDescriptor is completed, whether complete in success or failure.  
  A special case, if a network operation timed out, and bd is resend, ref_cnt of the network will 
  be increase again. until all the multiple posted bds completed, the ref_cnt of connection can reach to 0.
  
  For server side connection, its lifecycle is:  
     a) first created in PfServer::accept_connection, ref_cnt := 1  
     b) post_recv for receive handshake message, ref_cnt := 2, and down to 1 on receive complete  
     c) post_send for reply handshake message, ref_cnt := 2, and down to 1 on send complete  
     d) post_recv io_depth * 2 bd for cmd receive, ref_cnt increase to 1 + io_depth * 2  
     e) on a cmd recv compleete, ref_cnt down to (1 + io_depth * 2) - 1  
     f) for WRITE op, post_receive called to continue receive data, so data_bd is posted into connection.
        and ref_cnt inc to (1 + io_depth * 2) again. and down to (1 + io_depth * 2) - 1 on data received.  
     g) during this stage now, IO was in processing by pfs server. e.g. replicator, or disk.  
     h) On IO process complete, post_send called to send reply. ref_cnt increased to (1 + io_depth * 2).  
     i) in IDLE state, connection keep ref_cnt (1 + io_depth * 2), because (io_depth * 2) receive bd in waiting  
     j) on closing, the (io_depth * 2) receive bd completed in FLUSH_ERROR state. and ref_cnt down to 1  
     k) at last of close, fd was removed from epoll list. the last ref_cnt down to 0. and memory reclaimed.  
  For client side connection, its lifecycle is:  
     a) a connection succeeded created in PfConnectionPool::get_conn, add_ref to 1
     b) each time an IO was issued, volume proc will 1) post a recv for reply and then 2) post a send for cmd.
        this two operation will add ref_cnt by 2. i.e. ref_cnt += 2  
     c) on cmd send complete, ref_cnt -= 1; immediately, if it's a write IO, a post_send for data will inc ref_cnt
      again, ref_cnt += 1;  
     d) on reply recevie complete, ref_cnt -= 1; immediately, if it's a read IO, a post_read for data will inc ref_cnt
     again, ref_cnt += 1
     e) on data (send for write)/(recv for reead) complete, ref_cnt -= 1; at this moment, i.e. IDLE for no IO on
     flying, connection should has ref_cnt == 1
     f) on connection closed, ref_cnt decrease to 0. and memory will be released.   
     
1.2 PfServerIocb
  PfServerIocb consisted with following members:
     - 3 BufferDescriptors (cmd, data, reply bd)
     - one or more subtasks
  PfServerIocb can't be released until 1) the posted bds all complete. 2) all subtask complete. 
  if a subtask is reexecuted for timeout, PfServerIocb¡¡should not release until the subtask completed as
  many times as it was started.  
     During an IO's life cycle, the following action will occur:  
  <pre>
     1) receive cmd  
     2) receive data  
     3) send subtask to local disk  
     4) send 2 replicas to replicator  
       4.1) replicator send cmd to remote node  
       4.2) replicator send data to remote node  
       4.3) replicator receive reply from remote node  
     5) io poller get event from local disk  
     6) send reply to client  
 </pre> 
  We should:  
  <pre>
     a) +1 to Iocb::ref_cnt before 1) and -1 after 6) complete.
       on any abnormal complete like connection lose, the bd was complete in flush error, -1 to ref_cnt
     b) +1 before 3) and 4.1). -1 after 4.3) and 5)
     c) if a subtask timeout, 
  </pre>
    
  on any time a bd is posted to network, its related resource should be reserved by ref_cnt. resource include:
  1) connection this bd was posted into. 2) Server/Client IOCB this bd belongs to. 