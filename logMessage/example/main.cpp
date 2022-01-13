#include <iostream>
#include "stackMessage.h"
#include "rlog.h"
using namespace std;

class TEST{
public:
    void setValue(int num){value = num;}
    int value;
};

int main(int argc, char **argv)
{
    char machineID[128] = "xid1234";
    StackMessage stackM(machineID, LOG_LEVEL::WARNING );

    LOG_ERROR("my number is 10");
    LOG_INFO("info  my number is 10");
    LOG_WARNING("warning  my number is 10");
    LOG_WARNING("warning  my number is 11");

    //TEST *test = nullptr;
    //test->setValue(5);

    return 0;
}
