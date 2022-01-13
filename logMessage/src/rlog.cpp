#include "rlog.h"

#include <errno.h>
#include <unistd.h>     //access, getpid
#include <assert.h>     //assert
#include <stdarg.h>     //va_list
#include <sys/stat.h>   //mkdir
#include <dirent.h>
#include <sys/syscall.h>    //system call
#include <stdio.h>
#include <map>
#include <iostream>
#include "zip.h"

using namespace std;

#define MEM_USE_LIMIT (20u * 1024 * 1024)    //20MB            
//#define LOG_USE_LIMIT (/*1u * 1024*/2u * 1024 * 1024)//1GB
#define LOG_LEN_LIMIT (1 * 1024)//1K
#define RELOG_THRESOLD 5
#define BUFF_WAIT_TIME 1
#define LOG_FILE_MAX_LIMIT (10u *1024 *1024)    //10M
#define LOG_DIR_MAX_SIZE (200u * 1024 *1024)    //200M

pid_t gettid()
{
    return syscall(__NR_gettid);
}

pthread_mutex_t ring_log::_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t ring_log::_cond = PTHREAD_COND_INITIALIZER;

ring_log* ring_log::_ins = NULL;
pthread_once_t ring_log::_once = PTHREAD_ONCE_INIT;
uint32_t ring_log::_one_buff_len = 2*1024*1024;        //2MB
map<string, unsigned long> fileListMap;

ring_log::ring_log():
    _buff_cnt(3),
    _curr_buf(NULL),
    _prst_buf(NULL),
    _fp(NULL),
    _log_cnt(0),
    _env_ok(false),
    isOpened(false),
    _level(INFO),
    _lst_lts(0),
    totalSize(4096),    //log_dir self size
    _tm()
{
    //create double linked list
    cell_buffer* head = new cell_buffer(_one_buff_len);
    if (!head)
    {
        fprintf(stderr, "no space to allocate celfl_buffer\n");
        //exit(1);
    }
    cell_buffer* current;
    cell_buffer* prev = head;
    for (int i = 1;i < _buff_cnt; ++i)
    {
        current = new cell_buffer(_one_buff_len);
        if (!current)
        {
            fprintf(stderr, "no space to allocate cell_buffer\n");
            //exit(1);
        }
        current->prev = prev;
        prev->next = current;
        prev = current;
    }
    prev->next = head;
    head->prev = prev;

    _curr_buf = head;
    _prst_buf = head;

    _pid = getpid();
}

void ring_log::init_path(const char* root_dir, const char* machine_id, const char* log_dir, const char* file_name, int level)
{
    pthread_mutex_lock(&_mutex);

    mkdir(root_dir, 0777);
    if(access(root_dir, F_OK | W_OK) == -1)
    {
        fprintf(stderr, "logdir: %s error: %s\n", root_dir, strerror(errno));
    }

    strncpy(_machine_id, machine_id, 128);
    strncpy(_root_dir, root_dir, 128);
    strncpy(_log_dir, log_dir, 512);
    //name format:  name_year-mon-day-t[tid].log.n
    strncpy(_prog_name, file_name, 128);

    char filePath[1024] = {};
    sprintf(filePath, "%s/%s_%s", _root_dir, _log_dir, _machine_id);
    mkdir(filePath, 0777);
    //查看是否存在此目录、目录下是否允许创建文件
    if (access(filePath, F_OK | W_OK) == -1)
    {
        fprintf(stderr, "logdir: %s error: %s\n", filePath, strerror(errno));
    }
    else
    {
        _env_ok = true;
    }
    if (level > DEBUG)
        level = DEBUG;
    if (level < FATAL)
        level = FATAL;
    _level = level;

    //zip old dir
    vector<string> fileVec;
    getSonDir(root_dir, fileVec);
    for (auto dirP : fileVec)
    {
        if(strcmp(dirP.c_str(), filePath) == 0)
            continue;
        zipDir(dirP.c_str());
        ///upload suspend //achieve later///////////////////////////////////
        uploadZipFile("log/log.zip");
    }

    chdir(filePath);
    char log_path[128] = {};
    sprintf(log_path, "%s.log", _prog_name);
    _fp = fopen(log_path, "a");
    if(_fp)
    {
        isOpened = true;
    }
    pthread_mutex_unlock(&_mutex);
    chdir("../../");

    //delete oldest log dir if dir size > LOG_DIR_MAX_SIZE
    deleteLogDirFile(root_dir);
}

void ring_log::getCurrnetFile(char *log_path)
{
    sprintf(log_path, "%s/%s_%s/%s.log", _root_dir, _log_dir, _machine_id, _prog_name);
}

void ring_log::persist()
{
    while (true)
    {
        //check if _prst_buf need to be persist
        pthread_mutex_lock(&_mutex);
        if (_prst_buf->status != cell_buffer::FULL)
        {
            struct timespec tsp;
            struct timeval now;
            gettimeofday(&now, NULL);
            tsp.tv_sec = now.tv_sec;
            tsp.tv_nsec = now.tv_usec * 1000;//nanoseconds
            tsp.tv_sec += BUFF_WAIT_TIME;//wait for 1 seconds
            pthread_cond_timedwait(&_cond, &_mutex, &tsp);
        }
        if (_prst_buf->empty())
        {
            //give up, go to next turn
            pthread_mutex_unlock(&_mutex);
            continue;
        }

        if (_prst_buf->status == cell_buffer::FREE)
        {
            if(_curr_buf == _prst_buf)
            {
                _curr_buf->status = cell_buffer::FULL;
                _curr_buf = _curr_buf->next;
            }
        }

        int year = _tm.year, mon = _tm.mon, day = _tm.day;
        int hour = _tm.hour, min = _tm.min, sec = _tm.sec;
        pthread_mutex_unlock(&_mutex);

        //decision which file to write
        if (!decis_file(year, mon, day, hour, min, sec))
            continue;
        //write
        _prst_buf->persist(_fp);
        fflush(_fp);

        pthread_mutex_lock(&_mutex);
        _prst_buf->clear();
        _prst_buf = _prst_buf->next;

        pthread_mutex_unlock(&_mutex);
    }
}

void ring_log::try_append(const char* lvl, const char* format, ...)
{
    int ms;
    uint64_t curr_sec = _tm.get_curr_time(&ms);
    if (_lst_lts && curr_sec - _lst_lts < RELOG_THRESOLD)
        return ;

    char log_line[LOG_LEN_LIMIT];
    int prev_len = snprintf(log_line, LOG_LEN_LIMIT, "%s[%s.%03d]", lvl, _tm.utc_fmt, ms);

    va_list arg_ptr;
    va_start(arg_ptr, format);

    //TO OPTIMIZE IN THE FUTURE: performance too low here!
    int main_len = vsnprintf(log_line + prev_len, LOG_LEN_LIMIT - prev_len, format, arg_ptr);

    va_end(arg_ptr);

    uint32_t len = prev_len + main_len;

    _lst_lts = 0;
    bool tell_back = false;

    pthread_mutex_lock(&_mutex);
    int avail = _curr_buf->avail_len();
    if (_curr_buf->status != cell_buffer::FULL && _curr_buf->avail_len() >= len)
    {
        _curr_buf->append(log_line, len);
        _curr_buf->status = cell_buffer::HALF;
    }
    else
    {
        //1. _curr_buf->status = cell_buffer::FREE but _curr_buf->avail_len() < len
        //2. _curr_buf->status = cell_buffer::FULL
        //if (_curr_buf->status == cell_buffer::FREE)
        if (_curr_buf->status != cell_buffer::FULL)
        {
            _curr_buf->status = cell_buffer::FULL;//set to FULL
            cell_buffer* next_buf = _curr_buf->next;
            //tell backend thread
            tell_back = true;

            //it suggest that this buffer is under the persist job
            if (next_buf->status == cell_buffer::FULL)
            {
                //if mem use < MEM_USE_LIMIT, allocate new cell_buffer
                if (_one_buff_len * (_buff_cnt + 1) > MEM_USE_LIMIT)
                {
                    fprintf(stderr, "no more log space can use\n");
                    _curr_buf = next_buf;
                    _lst_lts = curr_sec;
                }
                else
                {
                    cell_buffer* new_buffer = new cell_buffer(_one_buff_len);
                    _buff_cnt += 1;
                    new_buffer->prev = _curr_buf;
                    _curr_buf->next = new_buffer;
                    new_buffer->next = next_buf;
                    next_buf->prev = new_buffer;
                    _curr_buf = new_buffer;
                }
            }
            else
            {
                //next buffer is free, we can use it
                _curr_buf = next_buf;
            }
            if (!_lst_lts)
                _curr_buf->append(log_line, len);
        }
        else//_curr_buf->status == cell_buffer::FULL, assert persist is on here too!
        {
            _lst_lts = curr_sec;
        }
    }
    pthread_mutex_unlock(&_mutex);
    if (tell_back)
    {
        pthread_cond_signal(&_cond);
    }
}

void ring_log::persistFinally()
{
    if(!isOpened)
    {
        if (_prst_buf != _curr_buf)
        {
            char log_path[1024] = {};
            sprintf(log_path, "%s/%s.log", _log_dir, _prog_name);
            _fp = fopen(log_path, "a");
            isOpened = true;
        }
        else
        {
            if(_prst_buf->status != cell_buffer::FREE)
            {
                char log_path[1024] = {};
                sprintf(log_path, "%s/%s.log", _log_dir, _prog_name);
                _fp = fopen(log_path, "a");
                isOpened = true;
            }
        }
    }
    while (_prst_buf != _curr_buf)
    {
        _prst_buf->persist(_fp);
        _prst_buf->status = cell_buffer::FREE;

        pthread_mutex_lock(&_mutex);
        _prst_buf->clear();
        _prst_buf = _prst_buf->next;
        pthread_mutex_unlock(&_mutex);
    }
    if(_prst_buf->status != cell_buffer::FREE)
    {
        _prst_buf->persist(_fp);
        _prst_buf->status = cell_buffer::FREE;

        pthread_mutex_lock(&_mutex);
        _prst_buf->clear();
        _prst_buf = _prst_buf->next;
        pthread_mutex_unlock(&_mutex);
    }

    fflush(_fp);
    fclose(_fp);
}

unsigned long long ring_log::getLogDirSize(const char *log_dir)
{
    DIR *dp;
    struct dirent *entry;
    struct stat statbuf;

    if ((dp = opendir(log_dir)) == NULL)
    {
        fprintf(stderr, "Cannot open dir: %s\n", log_dir);
        return 0;
    }
    chdir(log_dir);

    while ((entry = readdir(dp)) != NULL)
    {
        lstat(entry -> d_name, &statbuf);
        if (S_ISDIR(statbuf.st_mode))
        {
            if (strcmp(".", entry -> d_name) == 0 || strcmp("..", entry -> d_name) == 0)
            {
                if(strcmp(".", entry -> d_name)==0 && strcmp(_root_dir, log_dir) != 0
                   && strcmp(_log_dir, log_dir) != 0 )
                {
                    char dirP[64] = {};
                    sprintf(dirP, "../%s", log_dir);
                    int rmd = rmdir(dirP);     //delete success if dir is null,else no work
                    int c = 0;
                }

                continue;
            }
            totalSize += statbuf.st_size;
            getLogDirSize(entry -> d_name);
        }
        else
        {
            totalSize += statbuf.st_size;

            char testFile[128] = {};
            sprintf(testFile, "%s/%s", log_dir, entry -> d_name);
            fileListMap[string(testFile)] = statbuf.st_size;
        }
    }
    chdir("..");
    closedir(dp);
    return totalSize;
}

void ring_log::getLogDirFiles(const char *log_dir, vector<string> &vec)
{
    DIR *dp;
    struct dirent *entry;
    struct stat statbuf;
    if ((dp = opendir(log_dir)) == NULL)
    {
        fprintf(stderr, "Cannot open dir: %s\n", log_dir);
        return;
    }
    chdir(log_dir);

    while ((entry = readdir(dp)) != NULL)
    {
        lstat(entry -> d_name, &statbuf);
        if (S_ISDIR(statbuf.st_mode))
        {
            if (strcmp(".", entry -> d_name) == 0 || strcmp("..", entry -> d_name) == 0)
            {
                continue;
            }
            getLogDirFiles(entry -> d_name, vec);
        }
        else
        {
            char testFile[128] = {};
            sprintf(testFile, "%s/%s", log_dir, entry -> d_name);
            vec.push_back(testFile);
        }
    }
    chdir("..");
    closedir(dp);
}

void ring_log::getSonDir(const char *log_dir, vector<string> &vec)
{
     DIR *dp;
    struct dirent *entry;
    struct stat statbuf;
    if ((dp = opendir(log_dir)) == NULL)
    {
        fprintf(stderr, "Cannot open dir: %s\n", log_dir);
        return;
    }
    chdir(log_dir);

    while ((entry = readdir(dp)) != NULL)
    {
        lstat(entry -> d_name, &statbuf);
        if (S_ISDIR(statbuf.st_mode))
        {
            if (strcmp(".", entry -> d_name) == 0 || strcmp("..", entry -> d_name) == 0)
            {
                continue;
            }
            char testFile[128] = {};
            sprintf(testFile, "%s/%s", log_dir, entry -> d_name);
            vec.push_back(testFile);
            //getSonDir(entry -> d_name, vec);
        }
    }
    chdir("..");
    closedir(dp);
}

void ring_log::deleteLogDirFile(const char *log_dir)
{
    fileListMap.clear();
    totalSize = 4096;

    //delete oldest log dir if dir size > LOG_DIR_MAX_SIZE
    if(getLogDirSize(log_dir) > LOG_DIR_MAX_SIZE && fileListMap.size()>1)
    {
        for(auto iter:fileListMap)
        {
            unsigned long fileSize = iter.second;
            chdir(log_dir);
            totalSize -= fileSize;
            remove(iter.first.c_str());
            if(totalSize <= LOG_DIR_MAX_SIZE)
                break;
            chdir("..");
        }
    }
}

void ring_log::setCurrentDay(int day)
{
    _day = day;
}

void ring_log::zipDir(const char *log_dir)
{
    zipFileVec.clear();
    getLogDirFiles(log_dir, zipFileVec);
    chdir("..");

    int dbErr = 0;
    struct zip *zipFile = NULL;
    struct zip_source *zipSrcFile = NULL; 

    zipFile = zip_open("log/log.zip", ZIP_CREATE | ZIP_TRUNCATE, &dbErr);
    if(!zipFile)
    {
        zip_close(zipFile);
        cout<<"zip file created failure！\n";
        return;
    }
    for(int i = 0; i < zipFileVec.size(); i++)
    {
        zipSrcFile = zip_source_file(zipFile, zipFileVec.at(i).c_str(), 0, -1);
        if(!zipSrcFile)
        {
            continue;
            //zip_close(zipFile);
            //return;
        }
        zip_file_add(zipFile, zipFileVec.at(i).c_str(), zipSrcFile, ZIP_FL_OVERWRITE);
    }
    zip_close(zipFile);

    //delete files and log_dir
    for(auto iter:zipFileVec)
        remove(iter.c_str());
    rmdir(log_dir);
}

 void ring_log::uploadZipFile(const char* log_dir)
 {

 }

bool ring_log::decis_file(int year, int mon, int day, int hour, int min, int sec)
{
    if(!_env_ok)
        return _fp != NULL;

    if(_fp)
    {
        if(!isOpened)
        {
            char log_path[1024] = {};
            sprintf(log_path, "%s/%s_%s/%02d%02d%02d.log", _root_dir, _log_dir, _machine_id, _year, _mon, _day);
            _fp = fopen(log_path, "a");
            if(_fp)
                isOpened = true;
            return _fp != NULL;
        }
        else if (_day != day)
        {
            unsigned long long size = ftell(_fp);
            fclose(_fp);
            isOpened = false;

            totalSize += size;
            if(totalSize > LOG_DIR_MAX_SIZE)
                deleteLogDirFile(_root_dir);

            char log_path[128] = {};
            chdir(_root_dir);
            chdir("../");
            _year = year, _mon = mon, _day = day;
            sprintf(_log_dir, "%d%02d%02d", _year, _mon, _day);
            char filePath[128] = {};
            sprintf(filePath, "%s/%s_%s", _root_dir, _log_dir, _machine_id);
            mkdir(filePath, 0777);
            sprintf(log_path, "%s/%s_%s/%02d%02d%02d.log", _root_dir, _log_dir, _machine_id, hour, min, sec);
            sprintf(_prog_name, "%02d%02d%02d", hour, min, sec);
            _fp = fopen(log_path, "a");
            if (_fp)
                isOpened = true;

            //zip and upload
            zipDir(filePath);
            ///upload suspend //achieve later///////////////////////////////////
            uploadZipFile("log/log.zip");
        }
        else
        {
            unsigned long long size = ftell(_fp);
            if(size > LOG_FILE_MAX_LIMIT)     //signal file size > LOG_FILE_MAX_LIMIT
            {
                fclose(_fp);
                isOpened = false;

                //create new file, totalSize++
                totalSize += size;
                if(totalSize > LOG_DIR_MAX_SIZE)
                    deleteLogDirFile(_root_dir);

                char log_path[128] = {};
                chdir(_root_dir);
                chdir("../");
                sprintf(log_path, "%s/%s_%s/%02d%02d%02d.log", _root_dir, _log_dir, _machine_id, hour, min, sec);
                sprintf(_prog_name, "%02d%02d%02d", hour, min, sec);
                _fp = fopen(log_path, "a");
                if(_fp)
                    isOpened = true;
            }
        }
    }
    else
    {
        char log_path[1024] = {};
        sprintf(log_path, "%s/%s_%s/%02d%02d%02d.log", _root_dir, _log_dir, _machine_id, _year, _mon, _day);
        _fp = fopen(log_path, "a");
        if(_fp)
            isOpened = true;
        return _fp != NULL;
    }

    return _fp != NULL;
}

void* be_thdo(void* args)
{
    ring_log::ins()->persist();
    return NULL;
}
