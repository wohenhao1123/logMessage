#ifndef __RING_LOG_H__
#define __RING_LOG_H__

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>
#include <pthread.h>
#include <time.h>
#include <sys/time.h>
#include <sys/types.h>//getpid, gettid
#include <map>
#include <vector>

using namespace std;

enum LOG_LEVEL
{
    FATAL = 1,
    CRITICAL,
    ERROR,
    WARNING,
    INFO,

    DEBUG,
    // TRACE,
};

extern pid_t gettid();

struct utc_timer
{
    utc_timer()
    {
        struct timeval tv;
        gettimeofday(&tv, NULL);
        //set _sys_acc_sec, _sys_acc_min
        _sys_acc_sec = tv.tv_sec;
        _sys_acc_min = _sys_acc_sec / 60;
        //use _sys_acc_sec calc year, mon, day, hour, min, sec
        struct tm cur_tm;
        localtime_r((time_t*)&_sys_acc_sec, &cur_tm);
        year = cur_tm.tm_year + 1900;
        mon  = cur_tm.tm_mon + 1;
        day  = cur_tm.tm_mday;
        hour  = cur_tm.tm_hour;
        min  = cur_tm.tm_min;
        sec  = cur_tm.tm_sec;
        reset_utc_fmt();
    }

    uint64_t get_curr_time(int* p_msec = nullptr)
    {
        struct timeval tv;
        //get current ts
        gettimeofday(&tv, nullptr);
        if (p_msec)
            *p_msec = tv.tv_usec / 1000;
        //if not in same seconds
        if ((uint32_t)tv.tv_sec != _sys_acc_sec)
        {
            sec = tv.tv_sec % 60;
            _sys_acc_sec = tv.tv_sec;
            //or if not in same minutes
            if (_sys_acc_sec / 60 != _sys_acc_min)
            {
                //use _sys_acc_sec update year, mon, day, hour, min, sec
                _sys_acc_min = _sys_acc_sec / 60;
                struct tm cur_tm;
                localtime_r((time_t*)&_sys_acc_sec, &cur_tm);
                year = cur_tm.tm_year + 1900;
                mon  = cur_tm.tm_mon + 1;
                day  = cur_tm.tm_mday;
                hour = cur_tm.tm_hour;
                min  = cur_tm.tm_min;
                //reformat utc format
                reset_utc_fmt();
            }
            else
            {
                //reformat utc format only sec
                reset_utc_fmt_sec();
            }
        }
        return tv.tv_sec;
    }

    int year, mon, day, hour, min, sec;
    char utc_fmt[20];

private:
    void reset_utc_fmt()
    {
        snprintf(utc_fmt, 20, "%d-%02d-%02d %02d:%02d:%02d", year, mon, day, hour, min, sec);
    }
    
    void reset_utc_fmt_sec()
    {
        snprintf(utc_fmt + 17, 3, "%02d", sec);
    }

    uint64_t _sys_acc_min;
    uint64_t _sys_acc_sec;
};

class cell_buffer
{
public:
    enum buffer_status
    {
        FREE,
        HALF,
        FULL
    };

    cell_buffer(uint32_t len): 
    status(FREE), 
    prev(NULL), 
    next(NULL), 
    _total_len(len), 
    _used_len(0)
    {
        _data = new char[len];
        if (!_data)
        {
            fprintf(stderr, "no space to allocate _data\n");
            exit(1);
        }
    }

    uint32_t avail_len() const { return _total_len - _used_len; }

    bool empty() const { return _used_len == 0; }

    void append(const char* log_line, uint32_t len)
    {
        if (avail_len() < len)
            return ;
        memcpy(_data + _used_len, log_line, len);
        _used_len += len;
    }

    void clear()
    {
        _used_len = 0;
        status = FREE;
    }

    void persist(FILE* fp)
    {
        uint32_t wt_len = fwrite(_data, 1, _used_len, fp);
        if (wt_len != _used_len)
        {
            fprintf(stderr, "write log to disk error, wt_len %u, used_len %u\n", wt_len, _used_len);
        }
    }

    buffer_status status;

    cell_buffer* prev;
    cell_buffer* next;

private:
    cell_buffer(const cell_buffer&);
    cell_buffer& operator=(const cell_buffer&);

    uint32_t _total_len;
    uint32_t _used_len;
    char* _data;
};

class ring_log
{
public:
    //for thread-safe singleton
    static ring_log* ins()
    {
        pthread_once(&_once, ring_log::init);
        return _ins;
    }

    static void init()
    {
        while (!_ins) _ins = new ring_log();
    }

    void init_path(const char* root_dir, const char* machine_id,
                   const char* log_dir, const char* file_name, int level);

    int get_level() const { return _level; }
    void set_level(int level) {_level = level; }

   pthread_t getWriteThreadID() const {return wThreadID;}
   void setWriteThreadID(pthread_t id){wThreadID = id;}
   void getCurrnetFile(char *log_path);

    void persist();

    void try_append(const char* lvl, const char* format, ...);

    void persistFinally();          //write when program finished or terminated
    unsigned long long getLogDirSize(const char* log_dir);
    void getLogDirFiles(const char *log_dir, vector<string> &vec);
    void getSonDir(const char *log_dir, vector<string> &vec);
    void deleteLogDirFile(const char* log_dir);
    void setCurrentDay(int day);

    void zipDir(const char* log_dir);  //zip log dir
    void uploadZipFile(const char* log_dir);    

private:
    ring_log();

    bool decis_file(int year, int mon, int day, int hour, int min, int sec);

    ring_log(const ring_log&);
    const ring_log& operator=(const ring_log&);

    int _buff_cnt;

    cell_buffer* _curr_buf;
    cell_buffer* _prst_buf;

    cell_buffer* last_buf;

    FILE* _fp;
    pid_t _pid;
    pthread_t wThreadID;        //write file thread id
    int _year, _mon, _day, _log_cnt;
    char _prog_name[128];     //log file name
    char _log_dir[512];         //log file path
    char _root_dir[128];         //log file root path
    char _machine_id[128];     //machine id
    unsigned long long totalSize;
//    map<string, unsigned long> fileListMap;
    vector<string> zipFileVec;

    bool _env_ok;       //if log dir ok
    bool isOpened;      //log file is opened
    int _level;
    uint64_t _lst_lts;//last can't log error time(s) if value != 0, log error happened last time
    
    utc_timer _tm;

    static pthread_mutex_t _mutex;
    static pthread_cond_t _cond;

    static uint32_t _one_buff_len;

    //singleton
    static ring_log* _ins;
    static pthread_once_t _once;



};

void* be_thdo(void* args);

// #define LOG_MEM_SET(mem_lmt) \
//     do \
//     { \
//         if (mem_lmt < 90 * 1024 * 1024) \
//         { \
//             mem_lmt = 90 * 1024 * 1024; \
//         } \
//         else if (mem_lmt > 1024 * 1024 * 1024) \
//         { \
//             mem_lmt = 1024 * 1024 * 1024; \
//         } \
//         ring_log::_one_buff_len = mem_lmt; \
//     } while (0)

#define LOG_INIT(root_dir, machine_id, log_dir, prog_name, level) \
    do \
    { \
        ring_log::ins()->init_path(root_dir, machine_id, log_dir, prog_name, level); \
        pthread_t tid; \
        pthread_create(&tid, NULL, be_thdo, NULL); \
        ring_log::ins()->setWriteThreadID(tid); \
        pthread_detach(tid); \
    } while (0)

//format: [LEVEL][yy-mm-dd h:m:s.ms][tid]file_name:line_no(func_name):content
// #define LOG_TRACE(fmt, args...) \
//     do \
//     { \
//         if (ring_log::ins()->get_level() >= TRACE) \
//         { \
//             ring_log::ins()->try_append("[TRACE]", "[%u]%s:%d(%s): " fmt "\n", \
//                     gettid(), __FILE__, __LINE__, __FUNCTION__, ##args); \
//         } \
//     } while (0)

#define LOG_DEBUG(fmt, args...) \
    do \
    { \
        if (ring_log::ins()->get_level() >= DEBUG) \
        { \
            ring_log::ins()->try_append("[DEBUG]", "[%u]%s:%d(%s): " fmt "\n", \
                    gettid(), __FILE__, __LINE__, __FUNCTION__, ##args); \
        } \
    } while (0)

#define LOG_INFO(fmt, args...) \
    do \
    { \
        if (ring_log::ins()->get_level() >= INFO) \
        { \
            ring_log::ins()->try_append("[INFO]", "[%u]%s:%d(%s): " fmt "\n", \
                    gettid(), __FILE__, __LINE__, __FUNCTION__, ##args); \
        } \
    } while (0)

// #define LOG_NORMAL(fmt, args...) \
//     do \
//     { \
//         if (ring_log::ins()->get_level() >= INFO) \
//         { \
//             ring_log::ins()->try_append("[INFO]", "[%u]%s:%d(%s): " fmt "\n", \
//                     gettid(), __FILE__, __LINE__, __FUNCTION__, ##args); \
//         } \
//     } while (0)

#define LOG_WARNING(fmt, args...) \
    do \
    { \
        if (ring_log::ins()->get_level() >= WARNING) \
        { \
            ring_log::ins()->try_append("[WARNING]", "[%u]%s:%d(%s): " fmt "\n", \
                    gettid(), __FILE__, __LINE__, __FUNCTION__, ##args); \
        } \
    } while (0)

#define LOG_ERROR(fmt, args...) \
    do \
    { \
        if (ring_log::ins()->get_level() >= ERROR) \
        { \
            ring_log::ins()->try_append("[ERROR]", "[%u]%s:%d(%s): " fmt "\n", \
                gettid(), __FILE__, __LINE__, __FUNCTION__, ##args); \
        } \
    } while (0)

#define LOG_CRITICAL(fmt, args...) \
    do \
    { \
        if (ring_log::ins()->get_level() >= CRITICAL) \
        { \
            ring_log::ins()->try_append("[CRITICAL]", "[%u]%s:%d(%s): " fmt "\n", \
                gettid(), __FILE__, __LINE__, __FUNCTION__, ##args); \
        } \
    } while (0)

#define LOG_FATAL(fmt, args...) \
    do \
    { \
        ring_log::ins()->try_append("[FATAL]", "[%u]%s:%d(%s): " fmt "\n", \
            gettid(), __FILE__, __LINE__, __FUNCTION__, ##args); \
    } while (0)

// #define TRACE(fmt, args...) \
//     do \
//     { \
//         if (ring_log::ins()->get_level() >= TRACE) \
//         { \
//             ring_log::ins()->try_append("[TRACE]", "[%u]%s:%d(%s): " fmt "\n", \
//                     gettid(), __FILE__, __LINE__, __FUNCTION__, ##args); \
//         } \
//     } while (0)

#define DEBUG(fmt, args...) \
    do \
    { \
        if (ring_log::ins()->get_level() >= DEBUG) \
        { \
            ring_log::ins()->try_append("[DEBUG]", "[%u]%s:%d(%s): " fmt "\n", \
                    gettid(), __FILE__, __LINE__, __FUNCTION__, ##args); \
        } \
    } while (0)

#define INFO(fmt, args...) \
    do \
    { \
        if (ring_log::ins()->get_level() >= INFO) \
        { \
            ring_log::ins()->try_append("[INFO]", "[%u]%s:%d(%s): " fmt "\n", \
                    gettid(), __FILE__, __LINE__, __FUNCTION__, ##args); \
        } \
    } while (0)

// #define NORMAL(fmt, args...) \
//     do \
//     { \
//         if (ring_log::ins()->get_level() >= INFO) \
//         { \
//             ring_log::ins()->try_append("[INFO]", "[%u]%s:%d(%s): " fmt "\n", \
//                     gettid(), __FILE__, __LINE__, __FUNCTION__, ##args); \
//         } \
//     } while (0)

#define WARNING(fmt, args...) \
    do \
    { \
        if (ring_log::ins()->get_level() >= WARNING) \
        { \
            ring_log::ins()->try_append("[WARNING]", "[%u]%s:%d(%s): " fmt "\n", \
                    gettid(), __FILE__, __LINE__, __FUNCTION__, ##args); \
        } \
    } while (0)

#define ERROR(fmt, args...) \
    do \
    { \
        if (ring_log::ins()->get_level() >= ERROR) \
        { \
            ring_log::ins()->try_append("[ERROR]", "[%u]%s:%d(%s): " fmt "\n", \
                gettid(), __FILE__, __LINE__, __FUNCTION__, ##args); \
        } \
    } while (0)

#define CRITICAL(fmt, args...) \
    do \
    { \
        if (ring_log::ins()->get_level() >= CRITICAL) \
        { \
            ring_log::ins()->try_append("[CRITICAL]", "[%u]%s:%d(%s): " fmt "\n", \
                gettid(), __FILE__, __LINE__, __FUNCTION__, ##args); \
        } \
    } while (0)

#define FATAL(fmt, args...) \
    do \
    { \
        ring_log::ins()->try_append("[FATAL]", "[%u]%s:%d(%s): " fmt "\n", \
            gettid(), __FILE__, __LINE__, __FUNCTION__, ##args); \
    } while (0)

#endif
