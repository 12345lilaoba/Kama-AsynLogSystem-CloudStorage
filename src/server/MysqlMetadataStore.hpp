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

        void LogConfigContext(const char *prefix)
        {
            mylog::GetLogger("asynclogger")->Error("%s host=%s, port=%d, user=%s, database=%s",
                                                   prefix,
                                                   config_->GetMysqlHost().c_str(),
                                                   config_->GetMysqlPort(),
                                                   config_->GetMysqlUser().c_str(),
                                                   config_->GetMysqlDatabase().c_str());
        }

        bool Execute(const std::string &sql, const char *operation = "execute", const std::string &url = "")
        {
            if (!ready_)
            {
                mylog::GetLogger("asynclogger")->Error("mysql %s failed: connection not ready, url=%s", operation, url.c_str());
                return false;
            }
            if (mysql_query(conn_, sql.c_str()) != 0)
            {
                mylog::GetLogger("asynclogger")->Error("mysql %s failed, url=%s, error=%s, sql=%s",
                                                       operation, url.c_str(), mysql_error(conn_), sql.c_str());
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
                LogConfigContext("mysql init context:");
                return false;
            }

            const char *password = std::getenv(config_->GetMysqlPasswordEnv().c_str());
            if (password == nullptr)
            {
                mylog::GetLogger("asynclogger")->Error("mysql password env not set:%s", config_->GetMysqlPasswordEnv().c_str());
                LogConfigContext("mysql password env missing context:");
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
                mylog::GetLogger("asynclogger")->Error("mysql connect failed, error=%s", mysql_error(conn_));
                LogConfigContext("mysql connect context:");
                return false;
            }

            if (mysql_set_character_set(conn_, "utf8mb4") != 0)
            {
                mylog::GetLogger("asynclogger")->Error("mysql set charset failed, error=%s", mysql_error(conn_));
                LogConfigContext("mysql charset context:");
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
            if (!Execute(
                "CREATE TABLE IF NOT EXISTS file_metadata ("
                "url VARCHAR(768) NOT NULL PRIMARY KEY,"
                "storage_path VARCHAR(2048) NOT NULL,"
                "fsize BIGINT UNSIGNED NOT NULL DEFAULT 0,"
                "original_size BIGINT UNSIGNED NOT NULL DEFAULT 0,"
                "stored_size BIGINT UNSIGNED NOT NULL DEFAULT 0,"
                "mtime BIGINT NOT NULL DEFAULT 0,"
                "atime BIGINT NOT NULL DEFAULT 0,"
                "upload_time BIGINT NOT NULL DEFAULT 0,"
                "content_type VARCHAR(255) NOT NULL DEFAULT 'application/octet-stream',"
                "INDEX idx_upload_time(upload_time)"
                ") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4",
                "create file_metadata table"))
            {
                return false;
            }
            return true;
        }

        std::string UpsertSql(const StorageInfo &info)
        {
            std::ostringstream oss;
            oss << "INSERT INTO file_metadata "
                << "(url,storage_path,fsize,original_size,stored_size,mtime,atime,upload_time,content_type) VALUES ("
                << "'" << Escape(info.url_) << "',"
                << "'" << Escape(info.storage_path_) << "',"
                << info.fsize_ << ","
                << info.original_size_ << ","
                << info.stored_size_ << ","
                << (long long)info.mtime_ << ","
                << (long long)info.atime_ << ","
                << (long long)info.upload_time_ << ","
                << "'" << Escape(info.content_type_) << "') "
                << "ON DUPLICATE KEY UPDATE "
                << "storage_path=VALUES(storage_path),"
                << "fsize=VALUES(fsize),"
                << "original_size=VALUES(original_size),"
                << "stored_size=VALUES(stored_size),"
                << "mtime=VALUES(mtime),"
                << "atime=VALUES(atime),"
                << "upload_time=VALUES(upload_time),"
                << "content_type=VALUES(content_type)";
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
            info->fsize_ = ToUInt64(row[2]);
            info->original_size_ = ToUInt64(row[3]);
            info->stored_size_ = ToUInt64(row[4]);
            info->mtime_ = (time_t)ToInt64(row[5]);
            info->atime_ = (time_t)ToInt64(row[6]);
            info->upload_time_ = (time_t)ToInt64(row[7]);
            info->content_type_ = ToString(row[8]);
            return true;
        }

        std::string SelectColumns()
        {
            return "url,storage_path,fsize,original_size,stored_size,mtime,atime,upload_time,content_type";
        }

        std::string ListWhereSql(const std::string &keyword)
        {
            if (keyword.empty())
                return "";
            return " WHERE storage_path LIKE '%" + Escape(keyword) + "%'";
        }

        std::string ListOrderBySql(const std::string &sort)
        {
            if (sort == "name-asc")
                return " ORDER BY storage_path ASC, url ASC";
            if (sort == "name-desc")
                return " ORDER BY storage_path DESC, url DESC";
            if (sort == "size-asc")
                return " ORDER BY CASE WHEN original_size=0 THEN fsize ELSE original_size END ASC, url ASC";
            if (sort == "size-desc")
                return " ORDER BY CASE WHEN original_size=0 THEN fsize ELSE original_size END DESC, url ASC";
            if (sort == "upload-asc")
                return " ORDER BY upload_time ASC, url ASC";
            return " ORDER BY upload_time DESC, url ASC";
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
            return Execute(UpsertSql(info), "insert metadata", info.url_);
        }

        bool Update(const StorageInfo &info) override
        {
            return Execute(UpsertSql(info), "update metadata", info.url_);
        }

        bool Delete(const std::string &key) override
        {
            std::string sql = "DELETE FROM file_metadata WHERE url='" + Escape(key) + "'";
            return Execute(sql, "delete metadata", key);
        }

        bool GetOneByURL(const std::string &key, StorageInfo *info) override
        {
            if (!ready_)
            {
                mylog::GetLogger("asynclogger")->Error("mysql get metadata by url failed: connection not ready, url=%s", key.c_str());
                return false;
            }
            std::string sql = "SELECT " + SelectColumns() + " FROM file_metadata WHERE url='" + Escape(key) + "' LIMIT 1";
            if (mysql_query(conn_, sql.c_str()) != 0)
            {
                mylog::GetLogger("asynclogger")->Error("mysql get metadata by url failed, url=%s, error=%s", key.c_str(), mysql_error(conn_));
                return false;
            }
            MYSQL_RES *result = mysql_store_result(conn_);
            if (result == nullptr)
            {
                mylog::GetLogger("asynclogger")->Error("mysql store result failed for get metadata by url, url=%s, error=%s", key.c_str(), mysql_error(conn_));
                return false;
            }
            MYSQL_ROW row = mysql_fetch_row(result);
            bool ok = FillInfo(row, info);
            mysql_free_result(result);
            return ok;
        }

        bool GetAll(std::vector<StorageInfo> *arry) override
        {
            if (!ready_)
            {
                mylog::GetLogger("asynclogger")->Error("mysql get all metadata failed: connection not ready");
                return false;
            }
            std::string sql = "SELECT " + SelectColumns() + " FROM file_metadata";
            if (mysql_query(conn_, sql.c_str()) != 0)
            {
                mylog::GetLogger("asynclogger")->Error("mysql get all metadata failed, error=%s", mysql_error(conn_));
                return false;
            }
            MYSQL_RES *result = mysql_store_result(conn_);
            if (result == nullptr)
            {
                mylog::GetLogger("asynclogger")->Error("mysql store result failed for get all metadata, error=%s", mysql_error(conn_));
                return false;
            }
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

        bool QueryList(const std::string &keyword, const std::string &sort, size_t *page, size_t page_size, std::vector<StorageInfo> *files, size_t *total) override
        {
            if (!ready_)
            {
                mylog::GetLogger("asynclogger")->Error("mysql query list failed: connection not ready");
                return false;
            }
            if (page == nullptr || files == nullptr || total == nullptr || page_size == 0)
                return false;

            std::string where = ListWhereSql(keyword);
            std::string count_sql = "SELECT COUNT(*) FROM file_metadata" + where;
            if (mysql_query(conn_, count_sql.c_str()) != 0)
            {
                mylog::GetLogger("asynclogger")->Error("mysql query list count failed, error=%s", mysql_error(conn_));
                return false;
            }

            MYSQL_RES *count_result = mysql_store_result(conn_);
            if (count_result == nullptr)
            {
                mylog::GetLogger("asynclogger")->Error("mysql store result failed for list count, error=%s", mysql_error(conn_));
                return false;
            }
            MYSQL_ROW count_row = mysql_fetch_row(count_result);
            *total = count_row == nullptr ? 0 : (size_t)ToUInt64(count_row[0]);
            mysql_free_result(count_result);

            size_t page_count = *total == 0 ? 1 : (*total + page_size - 1) / page_size;
            if (*page < 1)
                *page = 1;
            if (*page > page_count)
                *page = page_count;
            size_t offset = (*page - 1) * page_size;

            std::ostringstream sql;
            sql << "SELECT " << SelectColumns() << " FROM file_metadata"
                << where
                << ListOrderBySql(sort)
                << " LIMIT " << page_size
                << " OFFSET " << offset;

            if (mysql_query(conn_, sql.str().c_str()) != 0)
            {
                mylog::GetLogger("asynclogger")->Error("mysql query list failed, error=%s", mysql_error(conn_));
                return false;
            }
            MYSQL_RES *result = mysql_store_result(conn_);
            if (result == nullptr)
            {
                mylog::GetLogger("asynclogger")->Error("mysql store result failed for query list, error=%s", mysql_error(conn_));
                return false;
            }

            files->clear();
            MYSQL_ROW row;
            while ((row = mysql_fetch_row(result)) != nullptr)
            {
                StorageInfo info;
                if (FillInfo(row, &info))
                    files->emplace_back(info);
            }
            mysql_free_result(result);
            return true;
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
        bool QueryList(const std::string &, const std::string &, size_t *, size_t, std::vector<StorageInfo> *, size_t *) override { return false; }
    };
#endif
}
