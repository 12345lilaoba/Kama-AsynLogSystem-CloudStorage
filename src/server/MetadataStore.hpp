#pragma once

#include "StorageInfo.hpp"
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <unordered_map>
#include <vector>

namespace storage
{
    class MetadataStore
    {
    public:
        virtual ~MetadataStore() = default;
        virtual bool Insert(const StorageInfo &info) = 0;
        virtual bool Update(const StorageInfo &info) = 0;
        virtual bool Delete(const std::string &key) = 0;
        virtual bool GetOneByURL(const std::string &key, StorageInfo *info) = 0;
        virtual bool GetAll(std::vector<StorageInfo> *arry) = 0;
        virtual bool SaveAll(const std::vector<StorageInfo> &arry) = 0;
    };

    class JsonMetadataStore : public MetadataStore
    {
    private:
        std::string storage_file_;
        std::unordered_map<std::string, StorageInfo> table_;
        bool loaded_;

        bool LoadFromFile()
        {
            storage::FileUtil f(storage_file_);
            if (!f.Exists())
            {
                mylog::GetLogger("asynclogger")->Info("there is no storage file info need to load");
                return true;
            }

            std::string body;
            if (!f.GetContent(&body))
                return false;

            Json::Value root;
            if (!storage::JsonUtil::UnSerialize(body, &root))
                return false;

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
                if (root[i].isMember("content_hash_"))
                    info.content_hash_ = root[i]["content_hash_"].asString();
                else
                    info.content_hash_.clear();
                if (root[i].isMember("hash_algo_"))
                    info.hash_algo_ = root[i]["hash_algo_"].asString();
                else
                    info.hash_algo_ = info.content_hash_.empty() ? "" : "fnv1a64";

                table_[info.url_] = info;
            }
            return true;
        }

        bool Persist()
        {
            mylog::GetLogger("asynclogger")->Info("message storage start");

            Json::Value root;
            for (const auto &e : table_)
            {
                const StorageInfo &info = e.second;
                Json::Value item;
                item["mtime_"] = (Json::Int64)info.mtime_;
                item["atime_"] = (Json::Int64)info.atime_;
                item["upload_time_"] = (Json::Int64)info.upload_time_;
                item["fsize_"] = (Json::Int64)info.fsize_;
                item["original_size_"] = (Json::UInt64)info.original_size_;
                item["stored_size_"] = (Json::UInt64)info.stored_size_;
                item["url_"] = info.url_.c_str();
                item["storage_path_"] = info.storage_path_.c_str();
                item["storage_type_"] = info.storage_type_.c_str();
                item["compressed_"] = info.compressed_;
                item["content_type_"] = info.content_type_.c_str();
                item["content_hash_"] = info.content_hash_.c_str();
                item["hash_algo_"] = info.hash_algo_.c_str();
                root.append(item);
            }

            std::string body;
            JsonUtil::Serialize(root, &body);
            mylog::GetLogger("asynclogger")->Info("new message for StorageInfo:%s", body.c_str());

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

    public:
        explicit JsonMetadataStore(const std::string &storage_file)
            : storage_file_(storage_file),
              loaded_(false)
        {
            loaded_ = LoadFromFile();
        }

        bool Insert(const StorageInfo &info) override
        {
            table_[info.url_] = info;
            return Persist();
        }

        bool Update(const StorageInfo &info) override
        {
            table_[info.url_] = info;
            return Persist();
        }

        bool Delete(const std::string &key) override
        {
            auto it = table_.find(key);
            if (it == table_.end())
                return false;
            table_.erase(it);
            return Persist();
        }

        bool GetOneByURL(const std::string &key, StorageInfo *info) override
        {
            auto it = table_.find(key);
            if (it == table_.end())
                return false;
            *info = it->second;
            return true;
        }

        bool GetAll(std::vector<StorageInfo> *arry) override
        {
            if (!loaded_)
                return false;
            for (const auto &e : table_)
                arry->emplace_back(e.second);
            return true;
        }

        bool SaveAll(const std::vector<StorageInfo> &arry) override
        {
            table_.clear();
            for (const auto &info : arry)
                table_[info.url_] = info;
            return Persist();
        }
    };
}
