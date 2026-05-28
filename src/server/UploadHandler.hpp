#pragma once

#include "DataManager.hpp"
#include "HttpUtil.hpp"
#include "MultipartUploadHandler.hpp"
#include "base64.h"

#include <event2/buffer.h>
#include <event2/http.h>
#include <evhttp.h>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <exception>
#include <string>

namespace storage
{
    class UploadHandler
    {
    public:
        static HttpUtil::HttpResult Handle(struct evhttp_request *req, DataManager &data_manager)
        {
            // 普通上传的文件内容放在 HTTP request body 里。
            struct evbuffer *buf = evhttp_request_get_input_buffer(req);
            if (buf == nullptr)
            {
                mylog::GetLogger("asynclogger")->Info("evhttp_request_get_input_buffer is empty");
                return HttpUtil::HttpResult(HTTP_BADREQUEST, "empty_input_buffer");
            }

            size_t len = evbuffer_get_length(buf);
            mylog::GetLogger("asynclogger")->Info("evbuffer_get_length is %zu", len);
            if (len == 0)
            {
                evhttp_send_reply(req, HTTP_BADREQUEST, "file empty", NULL);
                mylog::GetLogger("asynclogger")->Info("request body is empty");
                return HttpUtil::HttpResult(HTTP_BADREQUEST, "file_empty", len);
            }
            if (len > Config::GetInstance()->GetMaxUploadSize())
            {
                evhttp_send_reply(req, HTTP_BADREQUEST, "file too large", NULL);
                mylog::GetLogger("asynclogger")->Info("file too large, size:%zu, limit:%zu", len, Config::GetInstance()->GetMaxUploadSize());
                mylog::GetLogger("storage_logger")->Info("upload result=rejected reason=file_too_large bytes=%zu limit=%zu",
                                                         len, Config::GetInstance()->GetMaxUploadSize());
                return HttpUtil::HttpResult(HTTP_BADREQUEST, "file_too_large", len);
            }

            // 将 libevent 的缓冲区复制到 string，后面统一用 content 写入磁盘。
            std::string content(len, 0);
            if (evbuffer_copyout(buf, (void *)content.c_str(), len) == -1)
            {
                mylog::GetLogger("asynclogger")->Error("evbuffer_copyout error");
                evhttp_send_reply(req, HTTP_INTERNAL, NULL, NULL);
                return HttpUtil::HttpResult(HTTP_INTERNAL, "copyout_failed", len);
            }

            // 文件名通过 FileName 请求头传入，并使用 base64 编码来兼容中文和特殊字符。
            auto filename_header = evhttp_find_header(req->input_headers, "FileName");
            if (filename_header == NULL)
            {
                evhttp_send_reply(req, HTTP_BADREQUEST, "missing FileName", NULL);
                return HttpUtil::HttpResult(HTTP_BADREQUEST, "missing_filename", len);
            }

            std::string filename = filename_header;
            try
            {
                filename = base64_decode(filename);
            }
            catch (const std::exception &e)
            {
                evhttp_send_reply(req, HTTP_BADREQUEST, "invalid base64 filename", NULL);
                mylog::GetLogger("asynclogger")->Info("invalid base64 filename:%s", e.what());
                return HttpUtil::HttpResult(HTTP_BADREQUEST, "invalid_base64_filename", len);
            }

            // 防止 ../、路径分隔符等危险文件名写出存储目录。
            if (!HttpUtil::IsSafeFileName(filename))
            {
                evhttp_send_reply(req, HTTP_BADREQUEST, "illegal filename", NULL);
                mylog::GetLogger("storage_logger")->Info("upload result=rejected reason=illegal_filename filename=%s bytes=%zu",
                                                         filename.c_str(), len);
                return HttpUtil::HttpResult(HTTP_BADREQUEST, "illegal_filename", len);
            }

            mylog::GetLogger("storage_logger")->Info("upload start filename=%s bytes=%zu",
                                                     filename.c_str(), len);
            std::string storage_path = Config::GetInstance()->GetStorageDir();

            StorageInfo old_info;
            std::string download_url = Config::GetInstance()->GetDownloadPrefix() + filename;
            // 先按下载 URL 查询元数据，避免覆盖已经存在的文件。
            if (data_manager.GetOneByURL(download_url, &old_info))
            {
                mylog::GetLogger("asynclogger")->Info("file already exists:%s", filename.c_str());
                evhttp_send_reply(req, 409, "file already exists", NULL);
                mylog::GetLogger("storage_logger")->Info("upload result=conflict filename=%s url=%s bytes=%zu existing_path=%s",
                                                         filename.c_str(), download_url.c_str(), len, old_info.storage_path_.c_str());
                return HttpUtil::HttpResult(409, "file_already_exists", len);
            }

            FileUtil dirCreate(storage_path);
            dirCreate.CreateDirectory();

            storage_path += filename;
#ifdef DEBUG_LOG
            mylog::GetLogger("asynclogger")->Debug("storage_path:%s", storage_path.c_str());
#endif

            // 先把文件实体写入磁盘；元数据保存失败时会删除该文件做回滚。
            if (!HttpUtil::WriteFileAtomically(storage_path, content))
            {
                mylog::GetLogger("asynclogger")->Error("storage fail, evhttp_send_reply: HTTP_INTERNAL");
                mylog::GetLogger("storage_logger")->Info("upload result=file_write_failed filename=%s url=%s bytes=%zu path=%s",
                                                         filename.c_str(), download_url.c_str(), len, storage_path.c_str());
                evhttp_send_reply(req, HTTP_INTERNAL, "server error", NULL);
                return HttpUtil::HttpResult(HTTP_INTERNAL, "file_write_failed", len);
            }

            StorageInfo info;
            std::string content_type = HttpUtil::NormalizeContentType(evhttp_find_header(req->input_headers, "Content-Type"));
            // DataManager::Insert 会先写 MySQL，成功后再同步内存缓存。
            if (!info.NewStorageInfo(storage_path, content.size(), content_type) || !data_manager.Insert(info))
            {
                bool rollback_ok = std::remove(storage_path.c_str()) == 0;
                if (!rollback_ok)
                    mylog::GetLogger("asynclogger")->Error("rollback upload file failed: %s -- %s", storage_path.c_str(), strerror(errno));
                mylog::GetLogger("storage_logger")->Info("upload result=metadata_failed filename=%s url=%s original_size=%zu path=%s rollback=%s",
                                                         filename.c_str(), download_url.c_str(), content.size(), storage_path.c_str(),
                                                         rollback_ok ? "ok" : "failed");
                evhttp_send_reply(req, HTTP_INTERNAL, "save metadata failed", NULL);
                return HttpUtil::HttpResult(HTTP_INTERNAL, "save_metadata_failed", len);
            }

            evhttp_send_reply(req, HTTP_OK, "Success", NULL);
            // 普通上传成功后，同名的未完成分片上传会话已经没有意义，清理临时 chunk 文件。
            size_t cleaned_sessions = MultipartUploadHandler::CleanupSessionsByFilename(filename);
            mylog::GetLogger("storage_logger")->Info("upload result=ok filename=%s url=%s original_size=%zu stored_size=%zu path=%s content_type=%s",
                                                     filename.c_str(), info.url_.c_str(),
                                                     info.original_size_, info.stored_size_, info.storage_path_.c_str(), info.content_type_.c_str());
            if (cleaned_sessions > 0)
            {
                mylog::GetLogger("storage_logger")->Info("upload cleanup multipart_sessions=%zu filename=%s",
                                                         cleaned_sessions, filename.c_str());
            }
            return HttpUtil::HttpResult(HTTP_OK, "ok", len);
        }
    };
}
