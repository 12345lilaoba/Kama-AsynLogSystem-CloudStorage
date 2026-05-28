#define DEBUG_LOG
#include "../../log_system/logs_code/ThreadPoll.hpp"
#include "Service.hpp"
#include <functional>
#include <memory>
#include <thread>
using namespace std;

mylog::Util::JsonData *g_conf_data;
void service_module(storage::DataManager &data_manager) {
  storage::Service s(data_manager);
  mylog::GetLogger("asynclogger")->Info("service step in RunModule");
  s.RunModule();
}

std::unique_ptr<ThreadPool> log_system_module_init() {
  g_conf_data = mylog::Util::JsonData::GetJsonData();
  std::unique_ptr<ThreadPool> thread_pool(
      new ThreadPool(g_conf_data->thread_count));
  ThreadPool *thread_pool_ptr = thread_pool.get();
  mylog::AsyncWorker::SetTaskSubmitter(
      [thread_pool_ptr](std::function<void()> task) {
        return thread_pool_ptr->enqueue(std::move(task));
      });

  auto build_roll_logger = [](const std::string &name,
                              const std::string &prefix) {
    std::shared_ptr<mylog::LoggerBuilder> builder(new mylog::LoggerBuilder());
    builder->BuildLoggerName(name);
    builder->BuildLoggerFlush<mylog::RollFileFlush>(
        prefix, g_conf_data->roll_file_max_size,
        g_conf_data->roll_file_max_count, g_conf_data->roll_file_max_age_days);
    mylog::LoggerManager::GetInstance().AddLogger(builder->Build());
  };

  build_roll_logger("asynclogger", "./logfile/system/System_log");
  build_roll_logger("access_logger", "./logfile/access/Access_log");
  build_roll_logger("storage_logger", "./logfile/storage/Storage_log");
  return thread_pool;
}
int main(int argc, char *argv[]) {
  if (argc > 1) {
    storage::Config::SetConfigFile(argv[1]);
  }
  if (argc > 2) {
    mylog::Util::JsonData::SetConfigFile(argv[2]);
  }
  std::unique_ptr<ThreadPool> thread_pool = log_system_module_init();
  storage::DataManager data_manager;

  thread t1(service_module, std::ref(data_manager));

  t1.join();
  mylog::LoggerManager::GetInstance().Shutdown();
  mylog::AsyncWorker::SetTaskSubmitter({});
  thread_pool.reset();
  return 0;
}
