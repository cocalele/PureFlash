#ifndef __RT_INFO_HEADER__
#define __RT_INFO_HEADER__
#ifdef __cplusplus
extern "C"{
#endif

#define VMRSS_LINE 15//VMRSS所在行
#define PROCESS_ITEM 14//进程CPU时间开始的项数

typedef struct        //声明一个occupy的结构体
{
        unsigned int user;  //从系统启动开始累计到当前时刻，处于用户态的运行时间，不包含 nice值为负进程。
        unsigned int nice;  //从系统启动开始累计到当前时刻，nice值为负的进程所占用的CPU时间
        unsigned int system;//从系统启动开始累计到当前时刻，处于核心态的运行时间
        unsigned int idle;  //从系统启动开始累计到当前时刻，除IO等待时间以外的其它等待时间iowait (12256) 从系统启动开始累计到当前时刻，IO等待时间(since 2.5.41)
}total_cpu_occupy_t;

typedef struct
{
    pid_t pid;//pid号
    unsigned int utime;  //该任务在用户态运行的时间，单位为jiffies
    unsigned int stime;  //该任务在核心态运行的时间，单位为jiffies
    unsigned int cutime;//所有已死线程在用户态运行的时间，单位为jiffies
    unsigned int cstime;  //所有已死在核心态运行的时间，单位为jiffies
}process_cpu_occupy_t;

    int get_phy_mem(const pid_t p);//获取占用物理内存
    int get_total_mem();//获取系统总内存
    unsigned int get_cpu_total_occupy();//获取总的CPU时间
    unsigned int get_cpu_process_occupy(const pid_t p);//获取进程的CPU时间
    const char* get_items(const char* buffer,int ie);//取得缓冲区指定项的起始地址
    
    extern double get_pcpu(pid_t p);//获取进程CPU占用
    extern double get_pmem(pid_t p);//获取进程内存占用
    extern int get_rmem(pid_t p);//获取真实物理内存
    
    
#ifdef __cplusplus
}
#endif

#endif
