#pragma once
#include "Util.hpp"
#include <memory>
#include <mutex>
// 该类用于读取配置文件信息
namespace storage
{
    // 懒汉模式
    class Config
    {
    private:
        int server_port_;
        std::string server_ip;
        std::string download_prefix_; // URL路径前缀
        std::string deep_storage_dir_;     // 深度存储文件的存储路径
        std::string low_storage_dir_;     // 浅度存储文件的存储路径
        std::string storage_info_;     // 已存储文件的信息
        std::string metadata_store_;   // 元数据存储后端：json / mysql
        std::string mysql_host_;
        int mysql_port_;
        std::string mysql_user_;
        std::string mysql_password_env_;
        std::string mysql_database_;
        int bundle_format_;//深度存储的文件后缀，由选择的压缩格式确定
        size_t max_upload_size_; // 单次上传允许的最大文件大小
    private:
        static std::mutex _mutex;
        static Config *_instance;
        static std::string config_file_;
        Config()
        {
            if (ReadConfig() == false)
            {
                mylog::GetLogger("asynclogger")->Fatal("ReadConfig failed");
                return;
            }
            mylog::GetLogger("asynclogger")->Info("ReadConfig complicate");
        }

    public:
        // 读取配置文件信息
        bool ReadConfig()
        {
            mylog::GetLogger("asynclogger")->Info("ReadConfig start");

            storage::FileUtil fu(config_file_);
            std::string content;
            if (!fu.GetContent(&content))
            {
                return false;
            }

            Json::Value root;
            storage::JsonUtil::UnSerialize(content, &root); // 反序列化，把内容转成json value格式

            // 要记得转换的时候用上asint，asstring这种函数，json的数据类型是Value。
            server_port_ = root["server_port"].asInt();
            server_ip = root["server_ip"].asString();
            download_prefix_ = root["download_prefix"].asString();
            storage_info_ = root["storage_info"].asString();
            deep_storage_dir_ = root["deep_storage_dir"].asString();
            low_storage_dir_ = root["low_storage_dir"].asString();
            bundle_format_ = root["bundle_format"].asInt();
            metadata_store_ = root.get("metadata_store", "json").asString();
            mysql_host_ = root.get("mysql_host", "127.0.0.1").asString();
            mysql_port_ = root.get("mysql_port", 3306).asInt();
            mysql_user_ = root.get("mysql_user", "cloud_user").asString();
            mysql_password_env_ = root.get("mysql_password_env", "CLOUD_STORAGE_MYSQL_PASSWORD").asString();
            mysql_database_ = root.get("mysql_database", "cloud_storage").asString();
            max_upload_size_ = root.get("max_upload_size", 100 * 1024 * 1024).asUInt64();
            
            return true;
        }
        int GetServerPort()
        {
            return server_port_;
        }
        std::string GetServerIp()
        {
            return server_ip;
        }
        std::string GetDownloadPrefix()
        {
            return download_prefix_;
        }
        int GetBundleFormat()
        {
            return bundle_format_;
        }
        std::string GetDeepStorageDir()
        {
            return deep_storage_dir_;
        }
        std::string GetLowStorageDir()
        {
            return low_storage_dir_;
        }
        std::string GetStorageInfoFile()
        {
            return storage_info_;
        }
        std::string GetMetadataStoreType()
        {
            return metadata_store_;
        }
        std::string GetMysqlHost()
        {
            return mysql_host_;
        }
        int GetMysqlPort()
        {
            return mysql_port_;
        }
        std::string GetMysqlUser()
        {
            return mysql_user_;
        }
        std::string GetMysqlPasswordEnv()
        {
            return mysql_password_env_;
        }
        std::string GetMysqlDatabase()
        {
            return mysql_database_;
        }
        size_t GetMaxUploadSize()
        {
            return max_upload_size_;
        }

    public:
        static bool SetConfigFile(const std::string &config_file)
        {
            std::lock_guard<std::mutex> lock(_mutex);
            if (_instance != nullptr)
                return false;
            config_file_ = config_file;
            return true;
        }

        // 获取单例类对象
        static Config *GetInstance()
        {
            if (_instance == nullptr)
            {
                _mutex.lock();
                if (_instance == nullptr)
                {
                    _instance = new Config();
                }
                _mutex.unlock();
            }
            return _instance;
        }
    };
    // 静态成员初始化，先写类型再写类域
    std::mutex Config::_mutex;
    Config *Config::_instance = nullptr;
    std::string Config::config_file_ = "Storage.conf";
}
