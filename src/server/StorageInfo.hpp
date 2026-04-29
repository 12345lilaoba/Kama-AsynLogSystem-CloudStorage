#pragma once

#include "Config.hpp"

namespace storage
{
    // 用作初始化存储文件的属性信息
    typedef struct StorageInfo {
        time_t mtime_;
        time_t atime_;
        time_t upload_time_;
        size_t fsize_;             // 兼容旧字段，表示当前磁盘上的文件大小
        size_t original_size_;     // 上传时的原始文件大小
        size_t stored_size_;       // 实际落盘后的文件大小
        std::string storage_path_; // 文件存储路径
        std::string url_;          // 请求URL中的资源路径
        std::string storage_type_; // low / deep
        bool compressed_;          // 是否经过 bundle 压缩
        std::string content_type_; // 上传时的 Content-Type
        std::string content_hash_; // 上传内容的 hash 指纹
        std::string hash_algo_;    // hash 算法名称

        bool NewStorageInfo(const std::string &storage_path,
                            const std::string &storage_type = "low",
                            bool compressed = false,
                            size_t original_size = 0,
                            const std::string &content_type = "application/octet-stream",
                            const std::string &content_hash = "",
                            const std::string &hash_algo = "fnv1a64")
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
            content_hash_ = content_hash;
            hash_algo_ = hash_algo.empty() ? "fnv1a64" : hash_algo;
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
}
