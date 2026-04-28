#pragma once
#include "Config.hpp"
#include <cstdio>
#include <unordered_map>
#include <pthread.h>
namespace storage
{
    // 用作初始化存储文件的属性信息
    typedef struct StorageInfo{                   
        time_t mtime_;
        time_t atime_;
        time_t upload_time_;
        size_t fsize_;          // 兼容旧字段，表示当前磁盘上的文件大小
        size_t original_size_;  // 上传时的原始文件大小
        size_t stored_size_;    // 实际落盘后的文件大小
        std::string storage_path_; // 文件存储路径
        std::string url_;          // 请求URL中的资源路径
        std::string storage_type_; // low / deep
        bool compressed_;          // 是否经过 bundle 压缩
        std::string content_type_; // 上传时的 Content-Type

        bool NewStorageInfo(const std::string &storage_path,
                            const std::string &storage_type = "low",
                            bool compressed = false,
                            size_t original_size = 0,
                            const std::string &content_type = "application/octet-stream")
        {
            // 初始化备份文件的信息
            mylog::GetLogger("asynclogger")->Info("NewStorageInfo start");
            FileUtil f(storage_path);
            if (!f.Exists())
            {
                mylog::GetLogger("asynclogger")->Info("file not exists");
                return false;
            }
            atime_ = f.LastAccessTime();
            mtime_ = f.LastModifyTime();
            upload_time_ = time(nullptr);
            fsize_ = f.FileSize();
            stored_size_ = fsize_;
            original_size_ = original_size == 0 ? stored_size_ : original_size;
            storage_path_ = storage_path;
            storage_type_ = storage_type;
            compressed_ = compressed;
            content_type_ = content_type.empty() ? "application/octet-stream" : content_type;
            // URL实际就是用户下载文件请求的路径
            // 下载路径前缀+文件名
            storage::Config *config = storage::Config::GetInstance();
            url_ = config->GetDownloadPrefix() + f.FileName();
            mylog::GetLogger("asynclogger")->Info("download_url:%s,mtime_:%s,atime_:%s,original_size_:%zu,stored_size_:%zu",
                                                  url_.c_str(), ctime(&mtime_), ctime(&atime_), original_size_, stored_size_);
            mylog::GetLogger("asynclogger")->Info("NewStorageInfo end");
            return true;
        }
    } StorageInfo; // namespace StorageInfo

    class DataManager
    {
    private:
        std::string storage_file_;
        pthread_rwlock_t rwlock_;
        std::unordered_map<std::string, StorageInfo> table_;
        bool need_persist_;

    public:
        DataManager()
        {
            mylog::GetLogger("asynclogger")->Info("DataManager construct start");
            storage_file_ = storage::Config::GetInstance()->GetStorageInfoFile();
            pthread_rwlock_init(&rwlock_, NULL);
            need_persist_ = false;
            InitLoad();
            need_persist_ = true;
            mylog::GetLogger("asynclogger")->Info("DataManager construct end");
        }
        ~DataManager()
        {
            pthread_rwlock_destroy(&rwlock_);
        }

        bool InitLoad() // 初始化程序运行时从文件读取数据
        {
            mylog::GetLogger("asynclogger")->Info("init datamanager");
            storage::FileUtil f(storage_file_);
            if (!f.Exists()){
                mylog::GetLogger("asynclogger")->Info("there is no storage file info need to load");
                return true;
            }

            std::string body;
            if (!f.GetContent(&body))
                return false;

            // 反序列化
            Json::Value root;
            storage::JsonUtil::UnSerialize(body, &root);
            // 3，将反序列化得到的Json::Value中的数据添加到table中
            for (int i = 0; i < root.size(); i++)
            {
                StorageInfo info;
                info.fsize_ = root[i]["fsize_"].asUInt64();
                info.atime_ = root[i]["atime_"].asInt64();
                info.mtime_ = root[i]["mtime_"].asInt64();
                if (root[i].isMember("upload_time_"))
                    info.upload_time_ = root[i]["upload_time_"].asInt64();
                else
                    info.upload_time_ = info.mtime_;
                if (root[i].isMember("original_size_"))
                    info.original_size_ = root[i]["original_size_"].asUInt64();
                else
                    info.original_size_ = info.fsize_;
                if (root[i].isMember("stored_size_"))
                    info.stored_size_ = root[i]["stored_size_"].asUInt64();
                else
                    info.stored_size_ = info.fsize_;
                info.storage_path_ = root[i]["storage_path_"].asString();
                info.url_ = root[i]["url_"].asString();
                if (root[i].isMember("storage_type_"))
                    info.storage_type_ = root[i]["storage_type_"].asString();
                else if (info.storage_path_.find(storage::Config::GetInstance()->GetDeepStorageDir()) != std::string::npos)
                    info.storage_type_ = "deep";
                else
                    info.storage_type_ = "low";

                if (root[i].isMember("compressed_"))
                    info.compressed_ = root[i]["compressed_"].asBool();
                else
                    info.compressed_ = (info.storage_type_ == "deep");
                if (root[i].isMember("content_type_"))
                    info.content_type_ = root[i]["content_type_"].asString();
                else
                    info.content_type_ = "application/octet-stream";
                Insert(info);
            }
            return true;
        }

        bool Storage()
        {
            // 每次有信息改变则需要持久化存储一次
            // 把table_中的数据转成json格式存入文件
            mylog::GetLogger("asynclogger")->Info("message storage start");
            std::vector<StorageInfo> arr;
            if (!GetAll(&arr))
            {
                mylog::GetLogger("asynclogger")->Warn("GetAll fail,can't get StorageInfo");
                return false;
            }

            Json::Value root; // root中存着json::value对象
            for (auto e : arr)
            {
                Json::Value item;
                item["mtime_"] = (Json::Int64)e.mtime_;
                item["atime_"] = (Json::Int64)e.atime_;
                item["upload_time_"] = (Json::Int64)e.upload_time_;
                item["fsize_"] = (Json::Int64)e.fsize_;
                item["original_size_"] = (Json::UInt64)e.original_size_;
                item["stored_size_"] = (Json::UInt64)e.stored_size_;
                item["url_"] = e.url_.c_str();
                item["storage_path_"] = e.storage_path_.c_str();
                item["storage_type_"] = e.storage_type_.c_str();
                item["compressed_"] = e.compressed_;
                item["content_type_"] = e.content_type_.c_str();
                root.append(item); // 作为数组
            }

            // 序列化
            std::string body;
            JsonUtil::Serialize(root, &body);
            mylog::GetLogger("asynclogger")->Info("new message for StorageInfo:%s", body.c_str());    //打印序列化后的文件信息

            // 先写临时文件，再原子替换正式元数据文件，避免写一半崩溃损坏 storage.data。
            std::string tmp_file = storage_file_ + ".tmp";
            FileUtil tmp(tmp_file);
            if (tmp.SetContent(body.c_str(), body.size()) == false)
            {
                mylog::GetLogger("asynclogger")->Error("SetContent for StorageInfo tmp Error");
                return false;
            }
            if (std::rename(tmp_file.c_str(), storage_file_.c_str()) != 0)
            {
                mylog::GetLogger("asynclogger")->Error("rename StorageInfo tmp Error:%s", strerror(errno));
                std::remove(tmp_file.c_str());
                return false;
            }

            mylog::GetLogger("asynclogger")->Info("message storage end");
            return true;
        }

        bool Insert(const StorageInfo &info)
        {
            mylog::GetLogger("asynclogger")->Info("data_message Insert start");
            pthread_rwlock_wrlock(&rwlock_); // 加写锁
            table_[info.url_] = info;
            pthread_rwlock_unlock(&rwlock_);
            if (need_persist_ == true && Storage() == false)
            {
                mylog::GetLogger("asynclogger")->Error("data_message Insert:Storage Error");
                return false;
            }
            mylog::GetLogger("asynclogger")->Info("data_message Insert end");
            return true;
        }

        bool Update(const StorageInfo &info)
        {
            mylog::GetLogger("asynclogger")->Info("data_message Update start");
            pthread_rwlock_wrlock(&rwlock_);
            table_[info.url_] = info;
            pthread_rwlock_unlock(&rwlock_);
            if (Storage() == false)
            {
                mylog::GetLogger("asynclogger")->Error("data_message Update:Storage Error");
                return false;
            }
            mylog::GetLogger("asynclogger")->Info("data_message Update end");
            return true;
        }
        bool Delete(const std::string &key)
        {
            mylog::GetLogger("asynclogger")->Info("data_message Delete start");
            pthread_rwlock_wrlock(&rwlock_);
            auto it = table_.find(key);
            if (it == table_.end())
            {
                pthread_rwlock_unlock(&rwlock_);
                mylog::GetLogger("asynclogger")->Info("data_message Delete:not found");
                return false;
            }
            table_.erase(it);
            pthread_rwlock_unlock(&rwlock_);
            if (Storage() == false)
            {
                mylog::GetLogger("asynclogger")->Error("data_message Delete:Storage Error");
                return false;
            }
            mylog::GetLogger("asynclogger")->Info("data_message Delete end");
            return true;
        }
        bool GetOneByURL(const std::string &key, StorageInfo *info)
        {
            pthread_rwlock_rdlock(&rwlock_);
            // URL是key，所以直接find()找
            if (table_.find(key) == table_.end())
            {
                pthread_rwlock_unlock(&rwlock_);
                return false;
            }
            *info = table_[key]; // 获取url对应的文件存储信息
            pthread_rwlock_unlock(&rwlock_);
            return true;
        }
        bool GetOneByStoragePath(const std::string &storage_path, StorageInfo *info)
        {
            pthread_rwlock_rdlock(&rwlock_);
            // 遍历 通过realpath字段找到对应存储信息
            for (auto e : table_)
            {
                if (e.second.storage_path_ == storage_path)
                {
                    *info = e.second;
                    pthread_rwlock_unlock(&rwlock_);
                    return true;
                }
            }
            pthread_rwlock_unlock(&rwlock_);
            return false;
        }
        bool GetAll(std::vector<StorageInfo> *arry)
        {
            pthread_rwlock_rdlock(&rwlock_);
            for (auto e : table_)
                arry->emplace_back(e.second);
            pthread_rwlock_unlock(&rwlock_);
            return true;
        }
    }; // namespace DataManager
}
