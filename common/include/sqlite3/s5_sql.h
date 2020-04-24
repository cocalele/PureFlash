/**
 * Copyright (C), 2014-2015.
 * @file  
 *
 * Sql Meta data - includes tenant, quotaset, volume, nic and car meta data.
 * 
 * Meta data is stored in database to access. 
 *
 * @author xxx
 */

#ifndef __S5_SQL_CMETA_H__
#define __S5_SQL_CMETA_H__

#include "sqlite3.h"

#define MAX_SQL_LEN 1024

/** 
 * Struct db stores name and file handler of database.
 *
 * It is used to database operation.
 */
typedef struct db{
    const char* name;       ///< name of database
    sqlite3 *pDb;           ///< sqlite3 file handler
}db_t;


/** 
 * Struct rd_cb_param stores parameters for call back function to write data.
 *
 * It is used to store the result from SQL.
 */
typedef struct rd_cb_param{
	void* res_buf;
	int buf_size;                                       ///< buffer size
	int res_cnt;                                        ///< count
}rd_cb_param_t;

typedef struct blob_data_param
{
	void* blob_data;
	int blob_size;
}blob_data_param_t;

/** 
 * enum of sql types.
 *
 * It is used to distinguish update and select operation of sql.
 */
typedef enum {
	SQL_DML = 1,            ///< update operation of sql
	SQL_BLOB_DML,          ///< select operation of sql
	SQL_QUERY,          ///< select operation of sql
	SQL_BLOB_QUERY,          ///< select operation of sql
}SQL_TYPE;

/** 
 * Call back function template.
 *
 * It is used to specify call back function.
 *
 * @param[out]		param		            object pointer of rd_cb_param, used to write sql results to it.
 * @param[in]		n_column		        count of column.
 * @param[in]		colunm_value	        values of column.
 * @param[in]		column_names		    names of column.
 *
 * @return		    0 on success and negative error code for errors.
 */
typedef int(*SqlCallback)(rd_cb_param_t* param, int n_column, char** column_values, char** column_names);

/** 
 * Struct exec_sql_ctx_t stores sql and its type to access database, use call back function with call back parameters to get the results.
 *
 */
typedef struct exec_sql_ctx{
	char* sql;              ///< sql 
	SQL_TYPE type;          ///< type of sql
	int ret;                ///< execute sql result , ret=0 success, or set error code on ret
	SqlCallback cb_fun;     ///< sqlite3 callback function that read select result, just for select 
	union
	{
		rd_cb_param_t cb_param; ///< param of callback function, only for query operation
		blob_data_param_t blob_param;	///< for operation like insert or update, if item to operate is of blob type, this is blob data
	}param;
}exec_sql_ctx_t;

#ifdef __cplusplus
extern   "C" {
#endif 

/** 
 * Execute sql statement.
 *
 * This fucntion will perform a sql statement on database specified. Sql includes query, or modification operations.
 *
 * @param[in]	db			database handler
 * @param[in]	sql_ctx		sql context, with sql statement, sql type and parameters included.
 * @return		0 on success and negative error code if error occurs. Here error code is of sqlite3.
 * @retval	0			Success.
 * @retval	-ERROR_CODE	For detailed error code info, user can refer to 'http://www.sqlite.org/rescode.html'.
 */
int s5c_exec_sql(db_t* db, exec_sql_ctx_t* sql_ctx);

/** 
 * Open a database.
 *
 * Open database specified and set additional constraint. Like, set busy timeout for database operation, and turn on foreign constraint on database.
 *
 * @param[in,out]	db		Absolute path of database is contained in db, and also database handler, as a result of open operation, will be stored in it if fucntion returns successfully.
 * @return		0 on success and negative error code if error occurs. Here error code is of sqlite3.
 * @retval	0				Success.
 * @retval	-ERROR_CODE		For detailed error code info, user can refer to 'http://www.sqlite.org/rescode.html'.
 */
int s5c_open_db(db_t* db);


/** 
 * Close a database.
 *
 * Close database specified and release resource.
 *
 * @param[in,out]	db		Absolute path of database is contained in db, and also database handler to released is stored in it.
 * @return		0 on success and negative error code if error occurs. Here error code is of sqlite3.
 * @retval	0				Success.
 * @retval	-ERROR_CODE		For detailed error code info, user can refer to 'http://www.sqlite.org/rescode.html'.
 */
int s5c_close_db(db_t * db);

#ifdef __cplusplus
}
#endif 

#endif

