#ifndef __BDD_LOG_H__
#define __BDD_LOG_H__
#include <stdio.h>
#include <time.h>


#define BDD_LOG_FILE "/var/log/bdd.log"

#define LOG2FILE
#ifdef LOG2FILE

#define LOG_ERROR(fmt, arg...) \
	do { \
		time_t rawtime = time(NULL); \
		char stime[64]; \
		strftime(stime, sizeof(stime), "%Y-%m-%d %H:%M:%S", localtime(&rawtime)); \
		FILE* logfile = fopen(BDD_LOG_FILE, "a+"); \
		fprintf(logfile, "[%s] Error: " fmt "\n", stime, ##arg); \
		fclose(logfile); \
		printf("[%s] Error: " fmt "\n", stime, ##arg); \
	}while(0)
#define LOG_WARN(fmt, arg...) \
	do { \
		time_t rawtime = time(NULL); \
		char stime[64]; \
		strftime(stime, sizeof(stime), "%Y-%m-%d %H:%M:%S", localtime(&rawtime)); \
		FILE* logfile = fopen(BDD_LOG_FILE, "a+"); \
		fprintf(logfile, "[%s] Warning: " fmt "\n", stime, ##arg); \
		fclose(logfile); \
		printf("[%s] Warning: " fmt "\n", stime, ##arg); \
	}while(0)
#define LOG_INFO(fmt, arg...) \
	do { \
		time_t rawtime = time(NULL); \
		char stime[64]; \
		strftime(stime, sizeof(stime), "%Y-%m-%d %H:%M:%S", localtime(&rawtime)); \
		FILE* logfile = fopen(BDD_LOG_FILE, "a+"); \
		fprintf(logfile,"[%s] Info: " fmt " \n", stime, ##arg); \
		fclose(logfile); \
		printf("[%s] Info: " fmt "\n", stime, ##arg); \
	}while(0)
#define LOG_TRACE(fmt, arg...) \
	do { \
		time_t rawtime = time(NULL); \
		char stime[64]; \
		strftime(stime, sizeof(stime), "%Y-%m-%d %H:%M:%S", localtime(&rawtime)); \
		FILE* logfile = fopen(BDD_LOG_FILE, "a+"); \
		fprintf(logfile, "[%s] Trace: " fmt "\n", stime, ##arg); \
		fclose(logfile); \
		printf("[%s] Trace: " fmt "\n", stime, ##arg); \
	}while(0)

#else

#define LOG_ERROR(fmt, arg...) \
	do { \
		printf(fmt, ##arg); \
	}while(0)
#define LOG_WARN(fmt, arg...) \
	do { \
		printf(fmt, ##arg); \
	}while(0)
#define LOG_INFO(fmt, arg...) \
	do { \
		printf(fmt, ##arg); \
	}while(0)
#define LOG_TRACE(fmt, arg...) \
	do { \
		printf(fmt, ##arg); \
	}while(0)
#endif

#endif //__BDD_LOG_H__
