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
  
1.2 PfServerIocb
  PfServerIocb consisted with following members:
     - 3 BufferDescriptors (cmd, data, reply bd)
     - one or more subtasks
  PfServerIocb can't be released until 1) the posted bds all complete. 2) all subtask complete. 
  if a subtask is reexecuted for timeout, PfServerIocb¡¡should not release until the subtask completed as
  many times as it was started.
  
  on any time a bd is posted to network, its related resource should be reserved by ref_cnt. resource include:
  1) connection this bd was posted into. 2) Server/Client IOCB this bd belongs to. 