#pragma once

#include "DataManager.hpp"
#include "HttpUtil.hpp"

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <event2/buffer.h>
#include <event2/http.h>
#include <evhttp.h>
#include <fcntl.h>
#include <string>

namespace storage
{
    class DownloadHandler
    {
    private:
        static std::string GetETag(const StorageInfo &info)
        {
            FileUtil fu(info.storage_path_);
            std::string etag = fu.FileName();
            etag += "-";
            etag += std::to_string(info.fsize_);
            etag += "-";
            etag += std::to_string(info.mtime_);
            return etag;
        }

        static bool IsAttachmentRequest(struct evhttp_request *req)
        {
            const char *query = evhttp_uri_get_query(evhttp_request_get_evhttp_uri(req));
            if (query == nullptr)
                return false;
            std::string query_str = query;
            return query_str == "download=1" ||
                   query_str.find("&download=1") != std::string::npos ||
                   query_str.find("download=1&") != std::string::npos;
        }

        static std::string ContentDisposition(const std::string &filename)
        {
            std::string fallback;
            fallback.reserve(filename.size());
            for (unsigned char ch : filename)
            {
                if (ch >= 32 && ch < 127 && ch != '"' && ch != '\\')
                    fallback.push_back((char)ch);
                else
                    fallback.push_back('_');
            }
            if (fallback.empty())
                fallback = "download";

            return "attachment; filename=\"" + fallback + "\"; filename*=UTF-8''" +
                   HttpUtil::UrlEncodePathSegment(filename);
        }

    public:
        static HttpUtil::HttpResult Handle(struct evhttp_request *req, DataManager &data_manager)
        {
            StorageInfo info;
            std::string resource_path = evhttp_uri_get_path(evhttp_request_get_evhttp_uri(req));
            std::string decoded_path;
            if (!UrlDecode(resource_path, &decoded_path))
            {
                evhttp_send_reply(req, HTTP_BADREQUEST, "bad url", NULL);
                return HttpUtil::HttpResult(HTTP_BADREQUEST, "bad_url");
            }
            resource_path = decoded_path;
            mylog::GetLogger("storage_logger")->Info("download start url=%s", resource_path.c_str());

            if (!data_manager.GetOneByURL(resource_path, &info))
            {
                evhttp_send_reply(req, HTTP_NOTFOUND, "file not found", NULL);
                mylog::GetLogger("storage_logger")->Info("download result=not_found url=%s", resource_path.c_str());
                return HttpUtil::HttpResult(HTTP_NOTFOUND, "file_not_found");
            }

            std::string download_path = info.storage_path_;
            FileUtil fu(download_path);
            if (!fu.Exists())
            {
                mylog::GetLogger("asynclogger")->Info("evhttp_send_reply: 400 - bad request,file not exists");
                evhttp_send_reply(req, HTTP_BADREQUEST, "file not exists", NULL);
                mylog::GetLogger("storage_logger")->Info("download result=file_missing url=%s path=%s",
                                                         resource_path.c_str(), download_path.c_str());
                return HttpUtil::HttpResult(HTTP_BADREQUEST, "file_missing");
            }

            int64_t file_size = fu.FileSize();
            std::string etag = GetETag(info);

            evbuffer *outbuf = evhttp_request_get_output_buffer(req);
            HttpUtil::UniqueFd fd(open(download_path.c_str(), O_RDONLY));
            if (!fd.Valid())
            {
                mylog::GetLogger("asynclogger")->Error("open file error: %s -- %s", download_path.c_str(), strerror(errno));
                mylog::GetLogger("storage_logger")->Info("download result=open_failed url=%s path=%s error=%s",
                                                         resource_path.c_str(), download_path.c_str(), strerror(errno));
                evhttp_send_reply(req, HTTP_INTERNAL, strerror(errno), NULL);
                return HttpUtil::HttpResult(HTTP_INTERNAL, "open_failed");
            }
            if (evbuffer_add_file(outbuf, fd.Get(), 0, file_size) == -1)
            {
                mylog::GetLogger("asynclogger")->Error("evbuffer_add_file: %d -- %s -- %s", fd.Get(), download_path.c_str(), strerror(errno));
                mylog::GetLogger("storage_logger")->Info("download result=add_file_failed url=%s path=%s error=%s",
                                                         resource_path.c_str(), download_path.c_str(), strerror(errno));
                evhttp_send_reply(req, HTTP_INTERNAL, "add file failed", NULL);
                return HttpUtil::HttpResult(HTTP_INTERNAL, "add_file_failed");
            }
            fd.Release();

            evhttp_add_header(req->output_headers, "ETag", etag.c_str());
            evhttp_add_header(req->output_headers, "Content-Type", info.content_type_.empty() ? "application/octet-stream" : info.content_type_.c_str());
            if (IsAttachmentRequest(req))
            {
                std::string disposition = ContentDisposition(FileUtil(info.storage_path_).FileName());
                evhttp_add_header(req->output_headers, "Content-Disposition", disposition.c_str());
            }
            std::string content_length = std::to_string(file_size);
            evhttp_add_header(req->output_headers, "Content-Length", content_length.c_str());
            std::string original_size = std::to_string(info.original_size_);
            std::string stored_size = std::to_string(info.stored_size_);
            evhttp_add_header(req->output_headers, "X-Original-Size", original_size.c_str());
            evhttp_add_header(req->output_headers, "X-Stored-Size", stored_size.c_str());
            evhttp_send_reply(req, HTTP_OK, "Success", NULL);
            mylog::GetLogger("asynclogger")->Info("evhttp_send_reply: HTTP_OK");

            mylog::GetLogger("storage_logger")->Info("download result=ok url=%s path=%s bytes=%lld content_type=%s",
                                                     resource_path.c_str(), info.storage_path_.c_str(),
                                                     (long long)file_size, info.content_type_.c_str());
            return HttpUtil::HttpResult(HTTP_OK, "ok", (size_t)file_size);
        }
    };
}
