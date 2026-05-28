#pragma once

#include "DataManager.hpp"
#include "HttpUtil.hpp"
#include "base64.h"

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <event2/buffer.h>
#include <event2/http.h>
#include <evhttp.h>
#include <fcntl.h>
#include <fstream>
#include <random>
#include <set>
#include <sstream>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

namespace storage
{
    class MultipartUploadHandler
    {
    private:
        struct UploadSession
        {
            std::string upload_id;
            std::string filename;
            std::string content_type;
            size_t total_size;
            size_t chunk_size;
            size_t total_chunks;
            time_t created_at;
            time_t updated_at;
        };

        static std::string UploadRoot()
        {
            return Config::GetInstance()->GetStorageDir() + ".multipart_uploads/";
        }

        static std::string SessionDir(const std::string &upload_id)
        {
            return UploadRoot() + upload_id + "/";
        }

        static std::string MetaPath(const std::string &upload_id)
        {
            return SessionDir(upload_id) + "meta.json";
        }

        static std::string ChunkPath(const std::string &upload_id, size_t chunk_index)
        {
            char name[64] = {0};
            snprintf(name, sizeof(name), "chunk_%06zu", chunk_index);
            return SessionDir(upload_id) + name;
        }

        static void SendText(struct evhttp_request *req, int status, const char *reason, const std::string &body, const char *content_type)
        {
            evbuffer *out = evbuffer_new();
            if (out != nullptr)
            {
                evbuffer_add(out, body.data(), body.size());
                evhttp_add_header(req->output_headers, "Content-Type", content_type);
                evhttp_send_reply(req, status, reason, out);
                evbuffer_free(out);
            }
            else
            {
                evhttp_send_reply(req, status, reason, NULL);
            }
        }

        static void SendJson(struct evhttp_request *req, int status, const std::string &body)
        {
            SendText(req, status, "OK", body, "application/json; charset=utf-8");
        }

        static bool HeaderSize(struct evhttp_request *req, const char *name, size_t *value)
        {
            const char *header = evhttp_find_header(req->input_headers, name);
            if (header == nullptr || header[0] == '\0')
                return false;
            char *end = nullptr;
            errno = 0;
            unsigned long long parsed = strtoull(header, &end, 10);
            if (errno != 0 || end == header || *end != '\0')
                return false;
            *value = (size_t)parsed;
            return true;
        }

        static std::string QueryParam(struct evhttp_request *req, const std::string &name)
        {
            const char *query = evhttp_uri_get_query(evhttp_request_get_evhttp_uri(req));
            if (query == nullptr)
                return "";

            std::string raw = query;
            size_t pos = 0;
            while (pos <= raw.size())
            {
                size_t amp = raw.find('&', pos);
                std::string part = raw.substr(pos, amp == std::string::npos ? std::string::npos : amp - pos);
                size_t eq = part.find('=');
                std::string key = eq == std::string::npos ? part : part.substr(0, eq);
                if (key == name)
                {
                    std::string value = eq == std::string::npos ? "" : part.substr(eq + 1);
                    std::string decoded;
                    return UrlDecode(value, &decoded) ? decoded : "";
                }
                if (amp == std::string::npos)
                    break;
                pos = amp + 1;
            }
            return "";
        }

        static std::string NewUploadId()
        {
            static std::random_device rd;
            static std::mt19937_64 gen(rd());
            std::uniform_int_distribution<unsigned long long> dist;
            std::ostringstream oss;
            oss << time(nullptr) << "-" << dist(gen);
            return oss.str();
        }

        static bool SaveSession(const UploadSession &session)
        {
            FileUtil dir(SessionDir(session.upload_id));
            if (!dir.CreateDirectory())
                return false;

            Json::Value root;
            root["upload_id"] = session.upload_id;
            root["filename"] = session.filename;
            root["content_type"] = session.content_type;
            root["total_size"] = (Json::UInt64)session.total_size;
            root["chunk_size"] = (Json::UInt64)session.chunk_size;
            root["total_chunks"] = (Json::UInt64)session.total_chunks;
            root["created_at"] = (Json::Int64)session.created_at;
            root["updated_at"] = (Json::Int64)session.updated_at;

            std::string content;
            if (!JsonUtil::Serialize(root, &content))
                return false;
            FileUtil meta(MetaPath(session.upload_id));
            return meta.SetContent(content.c_str(), content.size());
        }

        static bool LoadSession(const std::string &upload_id, UploadSession *session)
        {
            if (upload_id.empty() || upload_id.find('/') != std::string::npos || upload_id.find('\\') != std::string::npos)
                return false;
            FileUtil meta(MetaPath(upload_id));
            std::string content;
            if (!meta.Exists() || !meta.GetContent(&content))
                return false;
            Json::Value root;
            if (!JsonUtil::UnSerialize(content, &root))
                return false;
            session->upload_id = root["upload_id"].asString();
            session->filename = root["filename"].asString();
            session->content_type = root.get("content_type", "application/octet-stream").asString();
            session->total_size = root["total_size"].asUInt64();
            session->chunk_size = root["chunk_size"].asUInt64();
            session->total_chunks = root["total_chunks"].asUInt64();
            session->created_at = (time_t)root["created_at"].asInt64();
            session->updated_at = (time_t)root["updated_at"].asInt64();
            return session->upload_id == upload_id && HttpUtil::IsSafeFileName(session->filename);
        }

        static std::set<size_t> UploadedChunks(const UploadSession &session)
        {
            std::set<size_t> uploaded;
            for (size_t i = 0; i < session.total_chunks; ++i)
            {
                FileUtil chunk(ChunkPath(session.upload_id, i));
                if (chunk.Exists())
                    uploaded.insert(i);
            }
            return uploaded;
        }

        static std::string StatusJson(const UploadSession &session)
        {
            Json::Value root;
            root["upload_id"] = session.upload_id;
            root["filename"] = session.filename;
            root["total_size"] = (Json::UInt64)session.total_size;
            root["chunk_size"] = (Json::UInt64)session.chunk_size;
            root["total_chunks"] = (Json::UInt64)session.total_chunks;
            root["uploaded_chunks"] = Json::arrayValue;
            for (size_t index : UploadedChunks(session))
                root["uploaded_chunks"].append((Json::UInt64)index);

            std::string body;
            JsonUtil::Serialize(root, &body);
            return body;
        }

        static bool FindReusableSession(const std::string &filename,
                                        size_t total_size,
                                        size_t chunk_size,
                                        size_t total_chunks,
                                        UploadSession *matched)
        {
            FileUtil root(UploadRoot());
            if (!root.Exists())
                return false;

            for (const auto &entry : fs::directory_iterator(UploadRoot()))
            {
                if (!fs::is_directory(entry))
                    continue;

                std::string upload_id = fs::path(entry).filename().string();
                UploadSession session;
                if (!LoadSession(upload_id, &session))
                    continue;

                if (session.filename == filename &&
                    session.total_size == total_size &&
                    session.chunk_size == chunk_size &&
                    session.total_chunks == total_chunks)
                {
                    *matched = session;
                    return true;
                }
            }
            return false;
        }

        static bool ReadBody(struct evhttp_request *req, std::string *content)
        {
            struct evbuffer *buf = evhttp_request_get_input_buffer(req);
            if (buf == nullptr)
                return false;
            size_t len = evbuffer_get_length(buf);
            content->assign(len, 0);
            if (len == 0)
                return true;
            return evbuffer_copyout(buf, &(*content)[0], len) != -1;
        }

        static bool MergeChunks(const UploadSession &session, const std::string &merged_path)
        {
            HttpUtil::UniqueFd out(open(merged_path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644));
            if (!out.Valid())
                return false;

            char buffer[64 * 1024];
            for (size_t i = 0; i < session.total_chunks; ++i)
            {
                HttpUtil::UniqueFd in(open(ChunkPath(session.upload_id, i).c_str(), O_RDONLY));
                if (!in.Valid())
                    return false;
                ssize_t n = 0;
                while ((n = read(in.Get(), buffer, sizeof(buffer))) > 0)
                {
                    const char *p = buffer;
                    ssize_t left = n;
                    while (left > 0)
                    {
                        ssize_t written = write(out.Get(), p, left);
                        if (written <= 0)
                            return false;
                        p += written;
                        left -= written;
                    }
                }
                if (n < 0)
                    return false;
            }
            return true;
        }

        static bool FinalizeFile(const UploadSession &session, const std::string &merged_path, std::string *storage_path)
        {
            std::string dir = Config::GetInstance()->GetStorageDir();
            FileUtil target_dir(dir);
            target_dir.CreateDirectory();

            *storage_path = dir + session.filename;
            std::remove(storage_path->c_str());

            std::string tmp_path = *storage_path + ".uploading";
            std::remove(tmp_path.c_str());
            if (std::rename(merged_path.c_str(), tmp_path.c_str()) != 0)
                return false;
            if (std::rename(tmp_path.c_str(), storage_path->c_str()) != 0)
            {
                std::remove(tmp_path.c_str());
                return false;
            }
            return true;
        }

    public:
        static size_t CleanupSessionsByFilename(const std::string &filename)
        {
            if (!HttpUtil::IsSafeFileName(filename))
                return 0;

            FileUtil root(UploadRoot());
            if (!root.Exists())
                return 0;

            size_t removed = 0;
            for (const auto &entry : fs::directory_iterator(UploadRoot()))
            {
                if (!fs::is_directory(entry))
                    continue;

                std::string upload_id = fs::path(entry).filename().string();
                UploadSession session;
                if (!LoadSession(upload_id, &session))
                    continue;

                if (session.filename == filename)
                {
                    std::string session_dir = SessionDir(upload_id);
                    fs::remove_all(session_dir);
                    ++removed;
                    mylog::GetLogger("storage_logger")->Info("multipart cleanup result=removed upload_id=%s filename=%s reason=ordinary_upload_completed",
                                                             upload_id.c_str(), filename.c_str());
                }
            }
            return removed;
        }

        static HttpUtil::HttpResult Init(struct evhttp_request *req, DataManager &data_manager)
        {
            const char *filename_header = evhttp_find_header(req->input_headers, "FileName");
            size_t total_size = 0;
            size_t chunk_size = 0;
            size_t total_chunks = 0;

            if (filename_header == nullptr ||
                !HeaderSize(req, "X-File-Size", &total_size) ||
                !HeaderSize(req, "X-Chunk-Size", &chunk_size) ||
                !HeaderSize(req, "X-Total-Chunks", &total_chunks))
            {
                evhttp_send_reply(req, HTTP_BADREQUEST, "missing multipart headers", NULL);
                return HttpUtil::HttpResult(HTTP_BADREQUEST, "missing_multipart_headers");
            }

            std::string filename;
            try
            {
                filename = base64_decode(std::string(filename_header));
            }
            catch (const std::exception &e)
            {
                evhttp_send_reply(req, HTTP_BADREQUEST, "invalid base64 filename", NULL);
                return HttpUtil::HttpResult(HTTP_BADREQUEST, "invalid_base64_filename");
            }

            if (!HttpUtil::IsSafeFileName(filename) ||
                total_size == 0 || chunk_size == 0 || total_chunks == 0 ||
                total_chunks != (total_size + chunk_size - 1) / chunk_size ||
                total_size > Config::GetInstance()->GetMaxUploadSize())
            {
                evhttp_send_reply(req, HTTP_BADREQUEST, "invalid multipart init", NULL);
                return HttpUtil::HttpResult(HTTP_BADREQUEST, "invalid_multipart_init");
            }

            StorageInfo old_info;
            std::string download_url = Config::GetInstance()->GetDownloadPrefix() + filename;
            if (data_manager.GetOneByURL(download_url, &old_info))
            {
                evhttp_send_reply(req, 409, "file already exists", NULL);
                return HttpUtil::HttpResult(409, "file_already_exists");
            }

            FileUtil root(UploadRoot());
            root.CreateDirectory();

            UploadSession reusable;
            if (FindReusableSession(filename, total_size, chunk_size, total_chunks, &reusable))
            {
                reusable.updated_at = time(nullptr);
                SaveSession(reusable);
                mylog::GetLogger("storage_logger")->Info("multipart init result=reuse upload_id=%s filename=%s total_size=%zu chunk_size=%zu total_chunks=%zu",
                                                         reusable.upload_id.c_str(), reusable.filename.c_str(),
                                                         reusable.total_size, reusable.chunk_size, reusable.total_chunks);
                SendJson(req, HTTP_OK, StatusJson(reusable));
                return HttpUtil::HttpResult(HTTP_OK, "ok");
            }

            UploadSession session;
            session.upload_id = NewUploadId();
            session.filename = filename;
            session.content_type = HttpUtil::NormalizeContentType(evhttp_find_header(req->input_headers, "Content-Type"));
            session.total_size = total_size;
            session.chunk_size = chunk_size;
            session.total_chunks = total_chunks;
            session.created_at = time(nullptr);
            session.updated_at = session.created_at;

            if (!SaveSession(session))
            {
                evhttp_send_reply(req, HTTP_INTERNAL, "save upload session failed", NULL);
                return HttpUtil::HttpResult(HTTP_INTERNAL, "save_upload_session_failed");
            }

            mylog::GetLogger("storage_logger")->Info("multipart init result=ok upload_id=%s filename=%s total_size=%zu chunk_size=%zu total_chunks=%zu",
                                                     session.upload_id.c_str(), session.filename.c_str(),
                                                     session.total_size, session.chunk_size, session.total_chunks);
            SendJson(req, HTTP_OK, StatusJson(session));
            return HttpUtil::HttpResult(HTTP_OK, "ok");
        }

        static HttpUtil::HttpResult Status(struct evhttp_request *req)
        {
            std::string upload_id = QueryParam(req, "upload_id");
            UploadSession session;
            if (!LoadSession(upload_id, &session))
            {
                evhttp_send_reply(req, HTTP_NOTFOUND, "upload session not found", NULL);
                return HttpUtil::HttpResult(HTTP_NOTFOUND, "upload_session_not_found");
            }

            SendJson(req, HTTP_OK, StatusJson(session));
            return HttpUtil::HttpResult(HTTP_OK, "ok");
        }

        static HttpUtil::HttpResult Chunk(struct evhttp_request *req)
        {
            const char *upload_id_header = evhttp_find_header(req->input_headers, "UploadId");
            size_t chunk_index = 0;
            if (upload_id_header == nullptr || !HeaderSize(req, "ChunkIndex", &chunk_index))
            {
                evhttp_send_reply(req, HTTP_BADREQUEST, "missing chunk headers", NULL);
                return HttpUtil::HttpResult(HTTP_BADREQUEST, "missing_chunk_headers");
            }

            UploadSession session;
            if (!LoadSession(upload_id_header, &session))
            {
                evhttp_send_reply(req, HTTP_NOTFOUND, "upload session not found", NULL);
                return HttpUtil::HttpResult(HTTP_NOTFOUND, "upload_session_not_found");
            }
            if (chunk_index >= session.total_chunks)
            {
                evhttp_send_reply(req, HTTP_BADREQUEST, "invalid chunk index", NULL);
                return HttpUtil::HttpResult(HTTP_BADREQUEST, "invalid_chunk_index");
            }

            std::string content;
            if (!ReadBody(req, &content) || content.empty() || content.size() > session.chunk_size)
            {
                evhttp_send_reply(req, HTTP_BADREQUEST, "invalid chunk body", NULL);
                return HttpUtil::HttpResult(HTTP_BADREQUEST, "invalid_chunk_body");
            }

            size_t expected = (chunk_index == session.total_chunks - 1)
                                  ? session.total_size - session.chunk_size * chunk_index
                                  : session.chunk_size;
            if (content.size() != expected)
            {
                evhttp_send_reply(req, HTTP_BADREQUEST, "chunk size mismatch", NULL);
                return HttpUtil::HttpResult(HTTP_BADREQUEST, "chunk_size_mismatch", content.size());
            }

            FileUtil chunk(ChunkPath(session.upload_id, chunk_index));
            if (!chunk.SetContent(content.data(), content.size()))
            {
                evhttp_send_reply(req, HTTP_INTERNAL, "save chunk failed", NULL);
                return HttpUtil::HttpResult(HTTP_INTERNAL, "save_chunk_failed", content.size());
            }

            session.updated_at = time(nullptr);
            SaveSession(session);
            mylog::GetLogger("storage_logger")->Info("multipart chunk result=ok upload_id=%s filename=%s chunk_index=%zu bytes=%zu",
                                                     session.upload_id.c_str(), session.filename.c_str(), chunk_index, content.size());
            evhttp_send_reply(req, HTTP_OK, "chunk ok", NULL);
            return HttpUtil::HttpResult(HTTP_OK, "ok", content.size());
        }

        static HttpUtil::HttpResult Complete(struct evhttp_request *req, DataManager &data_manager)
        {
            const char *upload_id_header = evhttp_find_header(req->input_headers, "UploadId");
            if (upload_id_header == nullptr)
            {
                evhttp_send_reply(req, HTTP_BADREQUEST, "missing UploadId", NULL);
                return HttpUtil::HttpResult(HTTP_BADREQUEST, "missing_upload_id");
            }

            UploadSession session;
            if (!LoadSession(upload_id_header, &session))
            {
                evhttp_send_reply(req, HTTP_NOTFOUND, "upload session not found", NULL);
                return HttpUtil::HttpResult(HTTP_NOTFOUND, "upload_session_not_found");
            }
            if (UploadedChunks(session).size() != session.total_chunks)
            {
                SendJson(req, HTTP_BADREQUEST, StatusJson(session));
                return HttpUtil::HttpResult(HTTP_BADREQUEST, "chunks_incomplete");
            }

            StorageInfo old_info;
            std::string download_url = Config::GetInstance()->GetDownloadPrefix() + session.filename;
            if (data_manager.GetOneByURL(download_url, &old_info))
            {
                evhttp_send_reply(req, 409, "file already exists", NULL);
                return HttpUtil::HttpResult(409, "file_already_exists");
            }

            std::string merged_path = SessionDir(session.upload_id) + "merged.uploading";
            std::remove(merged_path.c_str());
            if (!MergeChunks(session, merged_path))
            {
                evhttp_send_reply(req, HTTP_INTERNAL, "merge chunks failed", NULL);
                return HttpUtil::HttpResult(HTTP_INTERNAL, "merge_chunks_failed");
            }

            FileUtil merged(merged_path);
            if (merged.FileSize() != (int64_t)session.total_size)
            {
                std::remove(merged_path.c_str());
                evhttp_send_reply(req, HTTP_INTERNAL, "merged file size mismatch", NULL);
                return HttpUtil::HttpResult(HTTP_INTERNAL, "merged_file_size_mismatch");
            }

            std::string storage_path;
            if (!FinalizeFile(session, merged_path, &storage_path))
            {
                std::remove(merged_path.c_str());
                evhttp_send_reply(req, HTTP_INTERNAL, "finalize file failed", NULL);
                return HttpUtil::HttpResult(HTTP_INTERNAL, "finalize_file_failed");
            }

            StorageInfo info;
            if (!info.NewStorageInfo(storage_path, session.total_size, session.content_type) ||
                !data_manager.Insert(info))
            {
                std::remove(storage_path.c_str());
                evhttp_send_reply(req, HTTP_INTERNAL, "save metadata failed", NULL);
                return HttpUtil::HttpResult(HTTP_INTERNAL, "save_metadata_failed");
            }

            fs::remove_all(SessionDir(session.upload_id));
            mylog::GetLogger("storage_logger")->Info("multipart complete result=ok upload_id=%s filename=%s url=%s original_size=%zu stored_size=%zu path=%s",
                                                     session.upload_id.c_str(), session.filename.c_str(), info.url_.c_str(),
                                                     info.original_size_, info.stored_size_, info.storage_path_.c_str());
            evhttp_send_reply(req, HTTP_OK, "Success", NULL);
            return HttpUtil::HttpResult(HTTP_OK, "ok", session.total_size);
        }

        static HttpUtil::HttpResult Abort(struct evhttp_request *req)
        {
            const char *upload_id_header = evhttp_find_header(req->input_headers, "UploadId");
            if (upload_id_header == nullptr)
            {
                evhttp_send_reply(req, HTTP_BADREQUEST, "missing UploadId", NULL);
                return HttpUtil::HttpResult(HTTP_BADREQUEST, "missing_upload_id");
            }
            fs::remove_all(SessionDir(upload_id_header));
            evhttp_send_reply(req, HTTP_OK, "aborted", NULL);
            return HttpUtil::HttpResult(HTTP_OK, "ok");
        }
    };
}
