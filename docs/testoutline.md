# Test ouline of PureFlash

## 1. basic IO

## 2. cluster management

## 3. Snapshot
- create snapshot when volume in degraded state,
  point 1: some node in OFFLINE state, can jconductor handle `set_snapseq` failure correctly ?
- CoW fail in cow reading,
```
  app_context.error_handler->submit_error(t, PfMessageStatus::MSG_STATUS_AIOERROR); 
```
  this line may fail since t is not a fully initialized SubTask  