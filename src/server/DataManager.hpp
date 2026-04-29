#pragma once
#include "MetadataStore.hpp"
#include <cstdio>
#include <memory>
#include <unordered_map>
#include <pthread.h>
namespace storage
{
    class DataManager
    {
    private:
        class ReadLockGuard
        {
        private:
            pthread_rwlock_t *lock_;

        public:
            explicit ReadLockGuard(pthread_rwlock_t *lock) : lock_(lock)
            {
                pthread_rwlock_rdlock(lock_);
            }
            ~ReadLockGuard()
            {
                pthread_rwlock_unlock(lock_);
            }
            ReadLockGuard(const ReadLockGuard &) = delete;
            ReadLockGuard &operator=(const ReadLockGuard &) = delete;
        };

        class WriteLockGuard
        {
        private:
            pthread_rwlock_t *lock_;

        public:
            explicit WriteLockGuard(pthread_rwlock_t *lock) : lock_(lock)
            {
                pthread_rwlock_wrlock(lock_);
            }
            ~WriteLockGuard()
            {
                pthread_rwlock_unlock(lock_);
            }
            WriteLockGuard(const WriteLockGuard &) = delete;
            WriteLockGuard &operator=(const WriteLockGuard &) = delete;
        };

        std::unique_ptr<MetadataStore> metadata_store_;
        pthread_rwlock_t rwlock_;
        std::unordered_map<std::string, StorageInfo> table_;
        bool need_persist_;

        std::unique_ptr<MetadataStore> CreateMetadataStore()
        {
            storage::Config *config = storage::Config::GetInstance();
            std::string store_type = config->GetMetadataStoreType();
            if (store_type != "json")
            {
                mylog::GetLogger("asynclogger")->Warn("unsupported metadata_store:%s, fallback to json", store_type.c_str());
            }
            return std::unique_ptr<MetadataStore>(new JsonMetadataStore(config->GetStorageInfoFile()));
        }

    public:
        DataManager()
        {
            mylog::GetLogger("asynclogger")->Info("DataManager construct start");
            metadata_store_ = CreateMetadataStore();
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
        DataManager(const DataManager &) = delete;
        DataManager &operator=(const DataManager &) = delete;

        bool InitLoad() // 初始化程序运行时从文件读取数据
        {
            mylog::GetLogger("asynclogger")->Info("init datamanager");
            std::vector<StorageInfo> arry;
            if (!metadata_store_->GetAll(&arry))
                return false;

            for (const auto &info : arry)
            {
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

            if (!metadata_store_->SaveAll(arr))
                return false;
            mylog::GetLogger("asynclogger")->Info("message storage end");
            return true;
        }

        bool Insert(const StorageInfo &info)
        {
            mylog::GetLogger("asynclogger")->Info("data_message Insert start");
            {
                WriteLockGuard lock(&rwlock_);
                table_[info.url_] = info;
            }
            if (need_persist_ == true && metadata_store_->Insert(info) == false)
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
            {
                WriteLockGuard lock(&rwlock_);
                table_[info.url_] = info;
            }
            if (metadata_store_->Update(info) == false)
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
            {
                WriteLockGuard lock(&rwlock_);
                auto it = table_.find(key);
                if (it == table_.end())
                {
                    mylog::GetLogger("asynclogger")->Info("data_message Delete:not found");
                    return false;
                }
                table_.erase(it);
            }
            if (metadata_store_->Delete(key) == false)
            {
                mylog::GetLogger("asynclogger")->Error("data_message Delete:Storage Error");
                return false;
            }
            mylog::GetLogger("asynclogger")->Info("data_message Delete end");
            return true;
        }
        bool GetOneByURL(const std::string &key, StorageInfo *info)
        {
            ReadLockGuard lock(&rwlock_);
            // URL是key，所以直接find()找
            auto it = table_.find(key);
            if (it == table_.end())
            {
                return false;
            }
            *info = it->second; // 获取url对应的文件存储信息
            return true;
        }
        bool GetOneByStoragePath(const std::string &storage_path, StorageInfo *info)
        {
            ReadLockGuard lock(&rwlock_);
            // 遍历 通过realpath字段找到对应存储信息
            for (const auto &e : table_)
            {
                if (e.second.storage_path_ == storage_path)
                {
                    *info = e.second;
                    return true;
                }
            }
            return false;
        }
        bool GetAll(std::vector<StorageInfo> *arry)
        {
            ReadLockGuard lock(&rwlock_);
            for (const auto &e : table_)
                arry->emplace_back(e.second);
            return true;
        }
    }; // namespace DataManager
}
