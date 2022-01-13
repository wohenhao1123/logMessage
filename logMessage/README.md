日志产生介绍
1.日志产生在运行目录下的“log”文件夹下，以当前“日期_机器ID”为文件夹名，当前时间为日志文件的文件名。例：log/20220112_id178x/081605.log。
2.单个日志文件最大10MB，如果日志文件>10MB，则将多余数据保存到新的日志文件中，新文件以当前时间命名，以此类推。
3.软件运行期间日志数据跨天，则将跨天数据写入新日期文件夹下的新文件中。上一天日志文件打包上传，日志压缩包文件名为“log.zip”。
4.日志log文件夹最大存储空间为200MB，如果>200MB，则删除时间最早的log文件。


使用说明
日志数据分5个等级，分别为FATAL=1，CRITICAL=2，ERROR=3，WARNING=4，INFO=5，数值依次递增，等级依次递减。等级越低（数字越大），显示信息越多。即如果设置等级为WARNING，则日志文件会显示等级为FATAL，CRITICAL，ERROR，WARNING的所有日志数据。
使用时包含“stackMessage.h”，“rlog.h”文件。初始化 stackMessage时设置机器ID，日志数据等级。
如StackMessage stackM(“id137w”, LOG_LEVEL::WARNING);
可直接使用LOG_FATAL，LOG_CRITICAL，LOG_ERROR，LOG_WARNING，LOG_INFO来设置日志文件显示数据。log模块使用详见example/main.cpp。


example使用

cd exmaple
mkdir build
cd build
cmake ../
make
./LogMessage

