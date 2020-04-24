/**
 * Copyright (C), 2014-2015.
 * @file  
 *
 * s5c_meta_sql - meta database sql operation related.
 *
 * @author xxx
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

#include "pf_log.h"
#include "pf_utils.h"
#include "pf_conf.h"
#include "pf_sql.h"

#ifndef MAX_SQL_LEN
#define MAX_SQL_LEN 1024
#endif

int s5c_set_foreign_check_on(db_t* db)
{
	char* errMsg;
	int ret;
	char sql[MAX_SQL_LEN] = {0};
	int off = 0;
	off += snprintf(sql, MAX_SQL_LEN, "pragma foreign_keys=on");
	sql[off]=0;
	ret = sqlite3_exec(db->pDb, sql, NULL, NULL, &errMsg);

	if(ret != SQLITE_OK){
		S5LOG_ERROR("Failed to exec sql[%s] failed! err:%d, errMsg:%s\n", sql, ret, errMsg);
		ret = -ret;
	}
	return ret;
}

int s5c_open_db(db_t* db){
	int ret =  sqlite3_open(db->name, &db->pDb);
	if(ret != SQLITE_OK){
		S5LOG_ERROR("Failed to open db %s failed! err:%d\n",db->name, ret);
		return ret;
	}
	ret = sqlite3_busy_timeout(db->pDb, 200000);
	if(ret != SQLITE_OK){
		S5LOG_ERROR("Failed to set db %s busy timeout interval failed! err:%d\n",db->name, ret);
		return ret;
	}
	ret = s5c_set_foreign_check_on(db);
	if(ret != SQLITE_OK){
		S5LOG_ERROR("Failed to set foreign_keys check on %s failed! err:%d\n",db->name, ret);
		return ret;
	}
	return 0;
}

int s5c_close_db(db_t* db){
        int ret =  sqlite3_close(db->pDb);
        if(ret != SQLITE_OK){
            S5LOG_ERROR("Failed to close db %s failed! err:%d\n", db->name, ret);
        }
	return ret;
}

int  s5c_executeSQL(db_t* db, exec_sql_ctx_t* sql_ctx){
	char* errMsg = NULL;
       int ret;

	 S5LOG_INFO("Exec select sql[%s] start!\n", sql_ctx->sql);
	 ret = sqlite3_exec(db->pDb, sql_ctx->sql,NULL, NULL, &errMsg);
	  
	if(ret != SQLITE_OK){
         S5LOG_ERROR("Failed to exec sql[%s] failed! err:%d, errMsg:%s\n", sql_ctx->sql, ret, errMsg);
	     ret = -ret;
    }

	S5LOG_INFO("Exec select sql[%s] end!\n", sql_ctx->sql);
	return ret;
}


int  s5c_executeBlobSQL(db_t* db, exec_sql_ctx_t* sql_ctx){
	int ret;
	S5LOG_INFO("Exec select sql[%s] start!\n", sql_ctx->sql);
	sqlite3_stmt *stat;
	ret = sqlite3_prepare_v2(db->pDb, sql_ctx->sql, -1, &stat, NULL);
	if (ret != SQLITE_OK)
	{
		S5LOG_ERROR("Failed to S5 database error: %s", sqlite3_errmsg(db->pDb));;
		goto FINALIZE_AND_EXIT;
	}
	
	ret = sqlite3_bind_blob(stat, 1, sql_ctx->param.blob_param.blob_data, sql_ctx->param.blob_param.blob_size, NULL);
	if (ret != SQLITE_OK)
	{
		S5LOG_ERROR("Failed to S5 database error: %s", sqlite3_errmsg(db->pDb));
		goto FINALIZE_AND_EXIT;
	}		
	
	ret = sqlite3_step(stat);
	if (ret != SQLITE_DONE)
	{
		S5LOG_ERROR("S5 database error(%d): %s", ret, sqlite3_errmsg(db->pDb));
		ret = sqlite3_reset(stat);
		S5LOG_ERROR("S5 database error(%d): %s", ret, sqlite3_errmsg(db->pDb));
		goto FINALIZE_AND_EXIT;
	}		
	ret = 0;
FINALIZE_AND_EXIT:
	sqlite3_finalize(stat);
	return -ret;	 
}

int  s5c_executeSelectSQL(db_t* db, exec_sql_ctx_t* sql_ctx)
{
	char* errMsg = NULL;
	int ret;

	S5LOG_INFO("Exec select sql[%s] start!\n", sql_ctx->sql);
	ret = sqlite3_exec(db->pDb, sql_ctx->sql,(int (*)(void*, int, char**, char**))sql_ctx->cb_fun, &sql_ctx->param.cb_param, &errMsg);

	if(ret != SQLITE_OK){
		S5LOG_ERROR("Failed to exec sql[%s] failed! err:%d, errMsg:%s", sql_ctx->sql, ret, errMsg);
		ret = -ret;
	}

	S5LOG_INFO("Exec select sql[%s] end!\n", sql_ctx->sql);
	return ret;
}

int  s5c_executeBlobSelectSQL(db_t* db, exec_sql_ctx_t* sql_ctx)
{
	sqlite3_stmt *stat;
	const char *pzTail = NULL;
	int result = 0;
	result = sqlite3_prepare(db->pDb, sql_ctx->sql, -1, &stat, &pzTail);
	if(result != SQLITE_OK)
	{
		S5LOG_ERROR("Failed to exec sqlite3_prepare for sql[%s]. err:%d", sql_ctx->sql, result);
		return result;
	}
	int nColumn = sqlite3_column_count(stat);
	char** columnNames = 0;
	void** columnValues = 0;
	columnNames = (char**)malloc((size_t)(2 * nColumn) * sizeof(const char*));
	S5ASSERT(columnNames);
	for(int i=0; i<nColumn; i++)
	{
		columnNames[i] = (char *)sqlite3_column_name(stat, i);
		/* sqlite3VdbeSetColName() installs column names as UTF8
		** strings so there is no way for sqlite3_column_name() to fail. */
		S5ASSERT( columnNames[i]!=0 );
	}
	columnValues = (void**)&columnNames[nColumn];
	result = sqlite3_step(stat);
	while (result == SQLITE_ROW) /* sqlite3_step() has another row ready */
	{

		for (int k = 0; k < nColumn; k++)
		{
			int colType = sqlite3_column_type(stat, k);
			switch (colType)
			{
			case SQLITE_INTEGER:
				columnValues[k] = malloc(sizeof(int64_t));
				*((int64_t*)columnValues[k]) = sqlite3_column_int64(stat, k);
				break;
			case SQLITE_FLOAT:
				columnValues[k] = malloc(sizeof(double));
				*((double*)columnValues[k]) = sqlite3_column_double(stat, k);
				break;
			case SQLITE_BLOB:
				{
					const void* bi = sqlite3_column_blob(stat, k);
					int size = sqlite3_column_bytes(stat,k);
					columnValues[k] = malloc(sizeof(char) * (size_t)size + sizeof(int));
					void* write_pos = columnValues[k];
					*((int*)write_pos) = size;
					write_pos += sizeof(int);
					memcpy(write_pos, bi, (size_t)size);
				}
				break;
			case SQLITE_TEXT:
				{
					const void* text_buf = (const char*)sqlite3_column_text(stat, k);
					int size = sqlite3_column_bytes(stat,k);
					columnValues[k] = malloc(sizeof(char) * (size_t)size + sizeof(int));
					void* write_pos = columnValues[k];
					*((int*)write_pos) = size;
					write_pos += sizeof(int);
					memcpy(write_pos, text_buf, (size_t)size);
				}
				break;
			default:
				S5ASSERT(0);
				break;
			}
		}
		sql_ctx->cb_fun(&sql_ctx->param.cb_param, nColumn, (char**)columnValues, columnNames);
		result = sqlite3_step(stat);
		for (int i = 0; i < nColumn; i++)
		{
			free(columnValues[i]);
		}
	}
	S5ASSERT(result == SQLITE_DONE);
	free(columnNames);
	sqlite3_finalize(stat);
	return 0;
}
			
int s5c_exec_sql(db_t* db, exec_sql_ctx_t* sql_ctx)
{
     if(sql_ctx->type == SQL_DML)
		 return s5c_executeSQL(db, sql_ctx);
	 else if(sql_ctx->type == SQL_BLOB_DML)
		 return s5c_executeBlobSQL(db, sql_ctx);
	 else if(sql_ctx->type == SQL_QUERY)
		 return s5c_executeSelectSQL(db, sql_ctx); 
	 else if(sql_ctx->type == SQL_BLOB_QUERY)
		 return s5c_executeBlobSelectSQL(db, sql_ctx); 
	 else
		 return -EINVAL;
}

