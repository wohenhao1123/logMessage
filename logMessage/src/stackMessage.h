#ifndef STACK_MESSAGE_H
#define STACK_MESSAGE_H

#include "rlog.h"

class StackMessage{
public:
    StackMessage(char machine_id[128] = {}, int level = LOG_LEVEL::WARNING);
    ~StackMessage();
    void setLogLevel(int level);

private:
};


#endif /* STACK_MESSAGE_H */
