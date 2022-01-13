#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <execinfo.h>
#include <iostream>

#include "stackMessage.h"

using namespace std;

#define BACKTRACE_SIZE 50

void dump()
{
    ring_log::ins()->persistFinally();              //write finally log

    printf("Dump stack start...\n");

    char logFile[1024] = {};
    ring_log::ins()->getCurrnetFile(logFile);
    FILE *fp = nullptr;
    fp = fopen(logFile, "a");
    if(fp == nullptr)
    {
        fclose(fp);
        return;
    }
    //fputs("\n=================>>>catch signal " + to_string(signo) + "<<=====================\n", fp);
    fputs("Dump stack start...\n", fp);

    int i = 0, nptrs = 0;
    void *buf[BACKTRACE_SIZE];
    char **strings;
    nptrs = backtrace(buf, BACKTRACE_SIZE);

    printf("backtrace() returned %d addresses\n", nptrs);
    strings = backtrace_symbols(buf, nptrs);

    string str = "backtrace() returned " + to_string(nptrs) + " addresses \n";
    fputs(str.c_str(), fp);

    //backtrace_symbols_fd(buf, nptrs, fileno(fp));

    if (strings == nullptr)
    {
        perror("backtrace_symbols");
        exit(EXIT_FAILURE);
    }
    for (i = 0; i < nptrs; i++)
    {
        printf(" [%02d] %s\n", i, strings[i]);

        string str = " [" + to_string(i) + "] " + strings[i] + "\n";
        //fwrite(str.c_str(), sizeof(str), 1, fp);
        fputs(str.c_str(), fp);
    }
    printf("Dump stack end...\n");
    fputs("Dump stack end...\n", fp);
    free(strings);
    fclose(fp);
}

void signal_handler(int signo)
{
        printf("\n=================>>>catch signal %d<<<=====================\n", signo);
        dump();
        signal(signo, SIG_DFL);
        raise(signo);
}

StackMessage::StackMessage(char machine_id[128], int level)
{
    signal(SIGSEGV, signal_handler);

    // current date/time
    time_t now = time(nullptr);
    tm *ltm = localtime(&now);
    char dirPath[128] = {};
    sprintf(dirPath, "%d%02d%02d", 1900+ltm->tm_year, 1+ltm->tm_mon, ltm->tm_mday);
    char fileName[16] = {};
    sprintf(fileName, "%02d%02d%02d", ltm->tm_hour, ltm->tm_min, ltm->tm_sec);
    ring_log::ins()->setCurrentDay(ltm->tm_mday);
    LOG_INIT("log", machine_id, dirPath, fileName, level);


}

StackMessage::~StackMessage()
{
    pthread_t threadid = ring_log::ins()->getWriteThreadID();
    pthread_join(threadid, NULL);
    ring_log::ins()->persistFinally();
}

void StackMessage::setLogLevel(int level)
{
    ring_log::ins()->set_level(level);
}

