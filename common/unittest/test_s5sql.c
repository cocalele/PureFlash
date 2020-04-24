#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "s5_sql.h"
#include "s5utils.h"
#include "s5log.h"

#define DB_PATH "/var/tmp/test.db"

typedef struct student_binary_info
{
	char name[32];
	int age;
	int gender;
}student_binary_info_t;

typedef struct student_info
{
	int idx;
	student_binary_info_t info;
}student_info_t;

int student_sel_cb(rd_cb_param_t* param, int n_column, char** column_values, char** column_names);

int main()
{
	int ofs = 0;
	student_binary_info_t bin_info[5];
	int i;
	for (i = 0; i < 5; i++)
	{
		ofs = snprintf(bin_info[i].name, 32, "stundent_%d", i);
		bin_info[i].name[ofs] = 0;
		bin_info[i].gender = i % 2;
		bin_info[i].age = 20 + i;
	}
	
	db_t test_db;
	test_db.name = DB_PATH;
	int ret = s5c_open_db(&test_db);
	S5ASSERT(ret == 0);
	exec_sql_ctx_t sql_ctx;
	char sql[1024];
	for (i = 0; i < 5; i++)
	{
		ofs = snprintf(sql, 1024, "insert into t_student(bin_info) values(?);");
		sql[ofs] = 0;
		sql_ctx.sql = sql;
		sql_ctx.param.blob_param.blob_data = &bin_info[i];
		sql_ctx.param.blob_param.blob_size = sizeof(student_binary_info_t);
		sql_ctx.type = SQL_BLOB_DML;
		ret = s5c_exec_sql(&test_db, &sql_ctx);
		S5ASSERT(ret == 0);
	}
	
	//select info from db
	student_info_t students[5];
	ofs = snprintf(sql, 1024, "select * from t_student;");
	sql[ofs] = 0;
	sql_ctx.type = SQL_BLOB_QUERY;
	sql_ctx.param.cb_param.res_buf = students;
	sql_ctx.param.cb_param.res_cnt = 0;
	sql_ctx.param.cb_param.buf_size = 5 * sizeof(student_info_t);
	sql_ctx.cb_fun = student_sel_cb;

	ret = s5c_exec_sql(&test_db, &sql_ctx);
	S5ASSERT(ret == 0);

	//printf info
	for (i = 0; i < 5; i++)
	{
		printf("Student %d Info:\n", i);
		printf("Id: %d\n", students[i].idx);
		printf("Name: %s\n", students[i].info.name);
		printf("Gender: %d\n", students[i].info.gender);
		printf("Age: %d\n", students[i].info.age);
	}
	
}

int student_sel_cb(rd_cb_param_t* param, int n_column, char** column_values, char** column_names)
{
	rd_cb_param_t * rd_cb_param = ( rd_cb_param_t*)param;
	S5ASSERT(rd_cb_param->buf_size >= rd_cb_param->res_cnt);
	int idx  =  rd_cb_param->res_cnt;
	student_info_t* student = (student_info_t*)rd_cb_param->res_buf + idx ;
	student->idx = atoi(column_values[0]);
	void* read_pos = (void*)column_values[1];
	int size = *((int*)read_pos);
	read_pos += sizeof(int);
	S5ASSERT(size == sizeof(student_binary_info_t));
	memcpy(&student->info, read_pos, sizeof(student_binary_info_t));
	rd_cb_param->res_cnt++;
	return 0;
}



