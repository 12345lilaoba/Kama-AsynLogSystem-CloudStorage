/* ************************************************************************
> File Name:     test.cpp
> Created Time:  Thu 07 Sep 2023 06:37:16 PM CST
> Description:
 ************************************************************************/
#define DEBUG_LOG
#include "Service.hpp"
#include <thread>
using namespace std;

storage::DataManager *data_;
ThreadPool* tp=nullptr;
mylog::Util::JsonData* g_conf_data;
void service_module()
{
    storage::Service s;
    mylog::GetLogger("asynclogger")->Info("service step in RunModule");
    s.RunModule();
}

void log_system_module_init()
{
    g_conf_data = mylog::Util::JsonData::GetJsonData();
    tp = new ThreadPool(g_conf_data->thread_count);
    std::shared_ptr<mylog::LoggerBuilder> Glb(new mylog::LoggerBuilder());
    Glb->BuildLoggerName("asynclogger");
    Glb->BuildLoggerFlush<mylog::RollFileFlush>("./logfile/RollFile_log",
                                              g_conf_data->roll_file_max_size,
                                              g_conf_data->roll_file_max_count,
                                              g_conf_data->roll_file_max_age_days);
    // The LoggerManger has been built and is managed by members of the LoggerManger class
    //The logger is assigned to the managed object, and the caller lands the log by invoking the singleton managed object
    mylog::LoggerManager::GetInstance().AddLogger(Glb->Build());
}
int main(int argc, char *argv[])
{
    if (argc > 1)
    {
        storage::Config::SetConfigFile(argv[1]);
    }
    if (argc > 2)
    {
        mylog::Util::JsonData::SetConfigFile(argv[2]);
    }
    log_system_module_init();
    data_ = new storage::DataManager();

    thread t1(service_module);

    t1.join();
    delete(tp);
    return 0;
}
