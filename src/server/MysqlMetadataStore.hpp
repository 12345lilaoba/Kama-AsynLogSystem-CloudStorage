#pragma once

#include "MetadataStore.hpp"
#include <cstdlib>
#include <sstream>

#if __has_include(<mysql/mysql.h>)
#define STORAGE_HAS_MYSQL_CLIENT 1
#include <mysql/mysql.h>
#else
#define STORAGE_HAS_MYSQL_CLIENT 0
#endif

namespace storage
{
#if STORAGE_HAS_MYSQL_CLIENT
    class MysqlMetadataStore : public MetadataStore
    {
    private:
        Config *config_;
        MYSQL *conn_;
        bool ready_;

        bool Execute(const std::string &sql)
        {
            if (!ready_)
                return false;
            if (mysql_query(conn_, sql.c_str()) != 0)
            {
                mylog::GetLogger("asynclogger")->Error("mysql execute failed:%s, sql:%s", mysql_error(conn_), sql.c_str());
                return false;
            }
            return true;
        }

        std::string Escape(const std::string &input)
        {
            std::string escaped;
            escaped.resize(input.size() * 2 + 1);
            unsigned long len = mysql_real_escape_string(conn_, &escaped[0], input.c_str(), input.size());
            escaped.resize(len);
            return escaped;
        }

        bool Connect()
        {
            conn_ = mysql_init(nullptr);
            if (conn_ == nullptr)
            {
                mylog::GetLogger("asynclogger")->Error("mysql_init failed");
                return false;
            }

            const char *password = std::getenv(config_->GetMysqlPasswordEnv().c_str());
            if (password == nullptr)
            {
                mylog::GetLogger("asynclogger")->Error("mysql password env not set:%s", config_->GetMysqlPasswordEnv().c_str());
                return false;
            }

            if (mysql_real_connect(conn_,
                                   config_->GetMysqlHost().c_str(),
                                   config_->GetMysqlUser().c_str(),
                                   password,
                                   config_->GetMysqlDatabase().c_str(),
                                   config_->GetMysqlPort(),
                                   nullptr,
                                   0) == nullptr)
            {
                mylog::GetLogger("asynclogger")->Error("mysql connect failed:%s", mysql_error(conn_));
                return false;
            }

            if (mysql_set_character_set(conn_, "utf8mb4") != 0)
            {
                mylog::GetLogger("asynclogger")->Error("mysql set charset failed:%s", mysql_error(conn_));
                return false;
            }
            ready_ = true;
            if (!EnsureTable())
            {
                ready_ = false;
                return false;
            }
            return true;
        }

        bool EnsureTable()
        {
            return Execute(
                "CREATE TABLE IF NOT EXISTS file_metadata ("
                "url VARCHAR(768) NOT NULL PRIMARY KEY,"
                "storage_path VARCHAR(2048) NOT NULL,"
                "storage_type VARCHAR(16) NOT NULL,"
                "compressed TINYINT(1) NOT NULL DEFAULT 0,"
                "fsize BIGINT UNSIGNED NOT NULL DEFAULT 0,"
                "original_size BIGINT UNSIGNED NOT NULL DEFAULT 0,"
                "stored_size BIGINT UNSIGNED NOT NULL DEFAULT 0,"
                "mtime BIGINT NOT NULL DEFAULT 0,"
                "atime BIGINT NOT NULL DEFAULT 0,"
                "upload_time BIGINT NOT NULL DEFAULT 0,"
                "content_type VARCHAR(255) NOT NULL DEFAULT 'application/octet-stream',"
                "content_hash VARCHAR(128) NOT NULL DEFAULT '',"
                "hash_algo VARCHAR(32) NOT NULL DEFAULT '',"
                "INDEX idx_content_hash(content_hash),"
                "INDEX idx_upload_time(upload_time)"
                ") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4");
        }

        std::string UpsertSql(const StorageInfo &info)
        {
            std::ostringstream oss;
            oss << "INSERT INTO file_metadata "
                << "(url,storage_path,storage_type,compressed,fsize,original_size,stored_size,mtime,atime,upload_time,content_type,content_hash,hash_algo) VALUES ("
                << "'" << Escape(info.url_) << "',"
                << "'" << Escape(info.storage_path_) << "',"
                << "'" << Escape(info.storage_type_) << "',"
                << (info.compressed_ ? 1 : 0) << ","
                << info.fsize_ << ","
                << info.original_size_ << ","
                << info.stored_size_ << ","
                << (long long)info.mtime_ << ","
                << (long long)info.atime_ << ","
                << (long long)info.upload_time_ << ","
                << "'" << Escape(info.content_type_) << "',"
                << "'" << Escape(info.content_hash_) << "',"
                << "'" << Escape(info.hash_algo_) << "') "
                << "ON DUPLICATE KEY UPDATE "
                << "storage_path=VALUES(storage_path),"
                << "storage_type=VALUES(storage_type),"
                << "compressed=VALUES(compressed),"
                << "fsize=VALUES(fsize),"
                << "original_size=VALUES(original_size),"
                << "stored_size=VALUES(stored_size),"
                << "mtime=VALUES(mtime),"
                << "atime=VALUES(atime),"
                << "upload_time=VALUES(upload_time),"
                << "content_type=VALUES(content_type),"
                << "content_hash=VALUES(content_hash),"
                << "hash_algo=VALUES(hash_algo)";
            return oss.str();
        }

        static unsigned long long ToUInt64(const char *s)
        {
            return s == nullptr ? 0 : std::strtoull(s, nullptr, 10);
        }

        static long long ToInt64(const char *s)
        {
            return s == nullptr ? 0 : std::strtoll(s, nullptr, 10);
        }

        static std::string ToString(const char *s)
        {
            return s == nullptr ? "" : s;
        }

        static bool FillInfo(MYSQL_ROW row, StorageInfo *info)
        {
            if (row == nullptr || info == nullptr)
                return false;
            info->url_ = ToString(row[0]);
            info->storage_path_ = ToString(row[1]);
            info->storage_type_ = ToString(row[2]);
            info->compressed_ = ToInt64(row[3]) != 0;
            info->fsize_ = ToUInt64(row[4]);
            info->original_size_ = ToUInt64(row[5]);
            info->stored_size_ = ToUInt64(row[6]);
            info->mtime_ = (time_t)ToInt64(row[7]);
            info->atime_ = (time_t)ToInt64(row[8]);
            info->upload_time_ = (time_t)ToInt64(row[9]);
            info->content_type_ = ToString(row[10]);
            info->content_hash_ = ToString(row[11]);
            info->hash_algo_ = ToString(row[12]);
            return true;
        }

        std::string SelectColumns()
        {
            return "url,storage_path,storage_type,compressed,fsize,original_size,stored_size,mtime,atime,upload_time,content_type,content_hash,hash_algo";
        }

    public:
        explicit MysqlMetadataStore(Config *config)
            : config_(config),
              conn_(nullptr),
              ready_(false)
        {
            ready_ = Connect();
        }

        ~MysqlMetadataStore()
        {
            if (conn_ != nullptr)
                mysql_close(conn_);
        }

        bool Insert(const StorageInfo &info) override
        {
            return Execute(UpsertSql(info));
        }

        bool Update(const StorageInfo &info) override
        {
            return Execute(UpsertSql(info));
        }

        bool Delete(const std::string &key) override
        {
            std::string sql = "DELETE FROM file_metadata WHERE url='" + Escape(key) + "'";
            return Execute(sql);
        }

        bool GetOneByURL(const std::string &key, StorageInfo *info) override
        {
            if (!ready_)
                return false;
            std::string sql = "SELECT " + SelectColumns() + " FROM file_metadata WHERE url='" + Escape(key) + "' LIMIT 1";
            if (mysql_query(conn_, sql.c_str()) != 0)
            {
                mylog::GetLogger("asynclogger")->Error("mysql query failed:%s", mysql_error(conn_));
                return false;
            }
            MYSQL_RES *result = mysql_store_result(conn_);
            if (result == nullptr)
                return false;
            MYSQL_ROW row = mysql_fetch_row(result);
            bool ok = FillInfo(row, info);
            mysql_free_result(result);
            return ok;
        }

        bool GetAll(std::vector<StorageInfo> *arry) override
        {
            if (!ready_)
                return false;
            std::string sql = "SELECT " + SelectColumns() + " FROM file_metadata";
            if (mysql_query(conn_, sql.c_str()) != 0)
            {
                mylog::GetLogger("asynclogger")->Error("mysql query failed:%s", mysql_error(conn_));
                return false;
            }
            MYSQL_RES *result = mysql_store_result(conn_);
            if (result == nullptr)
                return false;
            MYSQL_ROW row;
            while ((row = mysql_fetch_row(result)) != nullptr)
            {
                StorageInfo info;
                if (FillInfo(row, &info))
                    arry->emplace_back(info);
            }
            mysql_free_result(result);
            return true;
        }

        bool SaveAll(const std::vector<StorageInfo> &arry) override
        {
            if (!Execute("START TRANSACTION"))
                return false;
            if (!Execute("DELETE FROM file_metadata"))
            {
                Execute("ROLLBACK");
                return false;
            }
            for (const auto &info : arry)
            {
                if (!Execute(UpsertSql(info)))
                {
                    Execute("ROLLBACK");
                    return false;
                }
            }
            return Execute("COMMIT");
        }
    };
#else
    class MysqlMetadataStore : public MetadataStore
    {
    public:
        explicit MysqlMetadataStore(Config *)
        {
            mylog::GetLogger("asynclogger")->Error("MysqlMetadataStore unavailable: install libmysqlclient-dev and rebuild");
        }

        bool Insert(const StorageInfo &) override { return false; }
        bool Update(const StorageInfo &) override { return false; }
        bool Delete(const std::string &) override { return false; }
        bool GetOneByURL(const std::string &, StorageInfo *) override { return false; }
        bool GetAll(std::vector<StorageInfo> *) override { return false; }
        bool SaveAll(const std::vector<StorageInfo> &) override { return false; }
    };
#endif
}
