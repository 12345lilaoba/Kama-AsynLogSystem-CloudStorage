#pragma once
#include "MetadataStore.hpp"
#include "MysqlMetadataStore.hpp"
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
            return std::unique_ptr<MetadataStore>(new MysqlMetadataStore(storage::Config::GetInstance()));
        }

        void UpsertOneInMemory(const StorageInfo &info)
        {
            WriteLockGuard lock(&rwlock_);
            table_[info.url_] = info;
        }

        void RemoveOneFromMemory(const std::string &key)
        {
            WriteLockGuard lock(&rwlock_);
            table_.erase(key);
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

        bool InitLoad()
        {
            mylog::GetLogger("asynclogger")->Info("init datamanager");
            std::vector<StorageInfo> arry;
            if (!metadata_store_->GetAll(&arry))
                return false;

            {
                WriteLockGuard lock(&rwlock_);
                for (const auto &info : arry)
                {
                    table_[info.url_] = info;
                }
            }
            mylog::GetLogger("asynclogger")->Info("init datamanager loaded metadata count:%zu", arry.size());
            return true;
        }


        bool Insert(const StorageInfo &info)
        {
            mylog::GetLogger("asynclogger")->Info("data_message Insert start");
            if (need_persist_ == false)
            {
                WriteLockGuard lock(&rwlock_);
                table_[info.url_] = info;
                mylog::GetLogger("asynclogger")->Info("data_message Insert end");
                return true;
            }
            if (need_persist_ == true && metadata_store_->Insert(info) == false)
            {
                mylog::GetLogger("asynclogger")->Error("data_message Insert:Storage Error");
                return false;
            }
            UpsertOneInMemory(info);
            mylog::GetLogger("asynclogger")->Info("data_message Insert end");
            return true;
        }

        bool Update(const StorageInfo &info)
        {
            mylog::GetLogger("asynclogger")->Info("data_message Update start");
            if (metadata_store_->Update(info) == false)
            {
                mylog::GetLogger("asynclogger")->Error("data_message Update:Storage Error");
                return false;
            }
            UpsertOneInMemory(info);
            mylog::GetLogger("asynclogger")->Info("data_message Update end");
            return true;
        }
        bool Delete(const std::string &key)
        {
            mylog::GetLogger("asynclogger")->Info("data_message Delete start");
            if (metadata_store_->Delete(key) == false)
            {
                mylog::GetLogger("asynclogger")->Error("data_message Delete:Storage Error");
                return false;
            }
            RemoveOneFromMemory(key);
            mylog::GetLogger("asynclogger")->Info("data_message Delete end");
            return true;
        }
        bool GetOneByURL(const std::string &key, StorageInfo *info)
        {
            {
                ReadLockGuard lock(&rwlock_);
                // URL是key，所以直接find()找
                auto it = table_.find(key);
                if (it != table_.end())
                {
                    *info = it->second; // 获取url对应的文件存储信息
                    return true;
                }
            }

            StorageInfo stored_info;
            if (!metadata_store_->GetOneByURL(key, &stored_info))
                return false;

            {
                WriteLockGuard lock(&rwlock_);
                table_[stored_info.url_] = stored_info;
            }
            *info = stored_info;
            return true;
        }
        bool QueryList(const std::string &keyword, const std::string &sort, size_t *page, size_t page_size, std::vector<StorageInfo> *files, size_t *total)
        {
            return metadata_store_->QueryList(keyword, sort, page, page_size, files, total);
        }
    }; // namespace DataManager
}
