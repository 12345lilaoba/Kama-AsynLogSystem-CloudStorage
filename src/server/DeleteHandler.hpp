#pragma once

#include "DataManager.hpp"
#include "HttpUtil.hpp"

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <event2/http.h>
#include <evhttp.h>
#include <string>

namespace storage
{
    class DeleteHandler
    {
    public:
        static HttpUtil::HttpResult Handle(struct evhttp_request *req, DataManager &data_manager)
        {
            if (evhttp_request_get_command(req) != EVHTTP_REQ_DELETE)
            {
                evhttp_send_reply(req, HTTP_BADMETHOD, "method not allowed", NULL);
                return HttpUtil::HttpResult(HTTP_BADMETHOD, "method_not_allowed");
            }

            std::string resource_path = evhttp_uri_get_path(evhttp_request_get_evhttp_uri(req));
            std::string decoded_path;
            if (!UrlDecode(resource_path, &decoded_path))
            {
                evhttp_send_reply(req, HTTP_BADREQUEST, "bad url", NULL);
                return HttpUtil::HttpResult(HTTP_BADREQUEST, "bad_url");
            }
            resource_path = decoded_path;
            const std::string delete_prefix = "/delete/";
            if (resource_path.find(delete_prefix) != 0)
            {
                evhttp_send_reply(req, HTTP_BADREQUEST, "bad delete url", NULL);
                return HttpUtil::HttpResult(HTTP_BADREQUEST, "bad_delete_url");
            }

            std::string filename = resource_path.substr(delete_prefix.size());
            if (!HttpUtil::IsSafeFileName(filename))
            {
                evhttp_send_reply(req, HTTP_BADREQUEST, "illegal filename", NULL);
                mylog::GetLogger("storage_logger")->Info("delete result=rejected reason=illegal_filename filename=%s", filename.c_str());
                return HttpUtil::HttpResult(HTTP_BADREQUEST, "illegal_filename");
            }

            std::string download_url = Config::GetInstance()->GetDownloadPrefix() + filename;
            mylog::GetLogger("storage_logger")->Info("delete start filename=%s url=%s", filename.c_str(), download_url.c_str());
            StorageInfo info;
            if (!data_manager.GetOneByURL(download_url, &info))
            {
                evhttp_send_reply(req, HTTP_NOTFOUND, "file not found", NULL);
                mylog::GetLogger("storage_logger")->Info("delete result=not_found filename=%s url=%s", filename.c_str(), download_url.c_str());
                return HttpUtil::HttpResult(HTTP_NOTFOUND, "file_not_found");
            }

            if (!data_manager.Delete(download_url))
            {
                evhttp_send_reply(req, HTTP_INTERNAL, "delete metadata failed", NULL);
                mylog::GetLogger("storage_logger")->Info("delete result=metadata_failed filename=%s url=%s path=%s",
                                                         filename.c_str(), download_url.c_str(), info.storage_path_.c_str());
                return HttpUtil::HttpResult(HTTP_INTERNAL, "delete_metadata_failed");
            }

            FileUtil fu(info.storage_path_);
            if (fu.Exists() && std::remove(info.storage_path_.c_str()) != 0)
            {
                int remove_errno = errno;
                bool rollback_ok = data_manager.Insert(info);
                mylog::GetLogger("asynclogger")->Error("remove file error after metadata delete: %s -- %s, rollback=%s",
                                                       info.storage_path_.c_str(), strerror(remove_errno), rollback_ok ? "ok" : "failed");
                mylog::GetLogger("storage_logger")->Info("delete result=file_remove_failed filename=%s url=%s path=%s error=%s metadata_rollback=%s",
                                                         filename.c_str(), download_url.c_str(), info.storage_path_.c_str(), strerror(remove_errno),
                                                         rollback_ok ? "ok" : "failed");
                evhttp_send_reply(req, HTTP_INTERNAL, rollback_ok ? "delete file failed, metadata restored" : "delete file failed, metadata rollback failed", NULL);
                return HttpUtil::HttpResult(HTTP_INTERNAL, rollback_ok ? "delete_file_failed_metadata_restored" : "delete_file_failed_metadata_rollback_failed");
            }

            evhttp_send_reply(req, HTTP_OK, "delete success", NULL);
            mylog::GetLogger("storage_logger")->Info("delete result=ok filename=%s url=%s path=%s original_size=%zu stored_size=%zu",
                                                     filename.c_str(), download_url.c_str(), info.storage_path_.c_str(),
                                                     info.original_size_, info.stored_size_);
            return HttpUtil::HttpResult(HTTP_OK, "ok");
        }
    };
}
