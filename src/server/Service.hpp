#pragma once
#include "DataManager.hpp"
#include "HttpUtil.hpp"
#include "PageRender.hpp"

#include <sys/queue.h>
#include <event.h>
// for http
#include <evhttp.h>
#include <event2/http.h>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <memory>
#include <regex>

#include "base64.h" // 来自 cpp-base64 库

extern storage::DataManager *data_;
namespace storage
{
    class Service
    {
    public:
        Service()
        {
#ifdef DEBUG_LOG
            mylog::GetLogger("asynclogger")->Debug("Service start(Construct)");
#endif
            server_port_ = Config::GetInstance()->GetServerPort();
            server_ip_ = Config::GetInstance()->GetServerIp();
            download_prefix_ = Config::GetInstance()->GetDownloadPrefix();
#ifdef DEBUG_LOG
            mylog::GetLogger("asynclogger")->Debug("Service end(Construct)");
#endif
        }
        bool RunModule()
        {
            // 初始化环境
            std::unique_ptr<event_base, decltype(&event_base_free)> base(event_base_new(), event_base_free);
            if (base == nullptr)
            {
                mylog::GetLogger("asynclogger")->Fatal("event_base_new err!");
                return false;
            }
            // 设置监听的端口和地址
            sockaddr_in sin;
            memset(&sin, 0, sizeof(sin));
            sin.sin_family = AF_INET;
            sin.sin_port = htons(server_port_);
            // http 服务器,创建evhttp上下文
            std::unique_ptr<evhttp, decltype(&evhttp_free)> httpd(evhttp_new(base.get()), evhttp_free);
            if (httpd == nullptr)
            {
                mylog::GetLogger("asynclogger")->Fatal("evhttp_new err!");
                return false;
            }
            // 绑定端口和ip
            if (evhttp_bind_socket(httpd.get(), "0.0.0.0", server_port_) != 0)
            {
                mylog::GetLogger("asynclogger")->Fatal("evhttp_bind_socket failed!");
                return false;
            }
            // 设定回调函数
            // 指定generic callback，也可以为特定的URI指定callback
            evhttp_set_gencb(httpd.get(), GenHandler, NULL);

#ifdef DEBUG_LOG
            mylog::GetLogger("asynclogger")->Debug("event_base_dispatch");
#endif
            if (-1 == event_base_dispatch(base.get()))
            {
                mylog::GetLogger("asynclogger")->Debug("event_base_dispatch err");
            }
            return true;
        }

    private:
        uint16_t server_port_;
        std::string server_ip_;
        std::string download_prefix_;

    private:
        static void GenHandler(struct evhttp_request *req, void *arg)
        {
            std::string path = evhttp_uri_get_path(evhttp_request_get_evhttp_uri(req));
            std::string decoded_path;
            if (!UrlDecode(path, &decoded_path))
            {
                evhttp_send_reply(req, HTTP_BADREQUEST, "bad url", NULL);
                return;
            }
            path = decoded_path;
            mylog::GetLogger("asynclogger")->Info("get req, uri: %s", path.c_str());

            // 根据请求中的内容判断是什么请求
            // 这里是下载请求
            if (path.find("/download/") == 0)
            {
                Download(req, arg);
            }
            else if (path.find("/delete/") == 0)
            {
                Delete(req, arg);
            }
            // 这里是上传
            else if (path == "/upload")
            {
                Upload(req, arg);
            }
            // 这里就是显示已存储文件列表，返回一个html页面给浏览器
            else if (path == "/")
            {
                ListShow(req, arg);
            }
            else
            {
                evhttp_send_reply(req, HTTP_NOTFOUND, "Not Found", NULL);
            }
        }

        static void Upload(struct evhttp_request *req, void *arg)
        {
            mylog::GetLogger("asynclogger")->Info("Upload start");
            // 约定：请求中包含"low_storage"，说明请求中存在文件数据,并希望普通存储\
                包含"deep_storage"字段则压缩后存储
            // 获取请求体内容
            struct evbuffer *buf = evhttp_request_get_input_buffer(req);
            if (buf == nullptr)
            {
                mylog::GetLogger("asynclogger")->Info("evhttp_request_get_input_buffer is empty");
                return;
            }

            size_t len = evbuffer_get_length(buf); // 获取请求体的长度
            mylog::GetLogger("asynclogger")->Info("evbuffer_get_length is %zu", len);
            if (0 == len)
            {
                evhttp_send_reply(req, HTTP_BADREQUEST, "file empty", NULL);
                mylog::GetLogger("asynclogger")->Info("request body is empty");
                return;
            }
            if (len > Config::GetInstance()->GetMaxUploadSize())
            {
                evhttp_send_reply(req, HTTP_BADREQUEST, "file too large", NULL);
                mylog::GetLogger("asynclogger")->Info("file too large, size:%zu, limit:%zu", len, Config::GetInstance()->GetMaxUploadSize());
                return;
            }
            std::string content(len, 0);
            if (-1 == evbuffer_copyout(buf, (void *)content.c_str(), len))
            {
                mylog::GetLogger("asynclogger")->Error("evbuffer_copyout error");
                evhttp_send_reply(req, HTTP_INTERNAL, NULL, NULL);
                return;
            }

            // 获取文件名
            auto filename_header = evhttp_find_header(req->input_headers, "FileName");
            // 解码文件名
            if (filename_header == NULL)
            {
                evhttp_send_reply(req, HTTP_BADREQUEST, "missing FileName", NULL);
                return;
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
                return;
            }

            if (!HttpUtil::IsSafeFileName(filename))
            {
                evhttp_send_reply(req, HTTP_BADREQUEST, "illegal filename", NULL);
                return;
            }

            // 获取存储类型，客户端自定义请求头 StorageType
            auto storage_type_header = evhttp_find_header(req->input_headers, "StorageType");
            if (storage_type_header == NULL)
            {
                evhttp_send_reply(req, HTTP_BADREQUEST, "missing StorageType", NULL);
                return;
            }
            std::string storage_type = storage_type_header;
            // 组织存储路径
            std::string storage_path;
            if (storage_type == "low")
            {
                storage_path = Config::GetInstance()->GetLowStorageDir();
            }
            else if (storage_type == "deep")
            {
                storage_path = Config::GetInstance()->GetDeepStorageDir();
            }
            else
            {
                mylog::GetLogger("asynclogger")->Info("evhttp_send_reply: HTTP_BADREQUEST");
                evhttp_send_reply(req, HTTP_BADREQUEST, "Illegal storage type", NULL);
                return;
            }

            StorageInfo old_info;
            std::string download_url = Config::GetInstance()->GetDownloadPrefix() + filename;
            if (data_->GetOneByURL(download_url, &old_info))
            {
                mylog::GetLogger("asynclogger")->Info("file already exists:%s", filename.c_str());
                evhttp_send_reply(req, 409, "file already exists", NULL);
                return;
            }

            // 如果不存在就创建low或deep目录
            FileUtil dirCreate(storage_path);
            dirCreate.CreateDirectory();

            // 目录创建后加可以加上文件名，这个就是最终要写入的文件路径
            storage_path += filename;
#ifdef DEBUG_LOG
            mylog::GetLogger("asynclogger")->Debug("storage_path:%s", storage_path.c_str());
#endif

            bool compressed = (storage_type == "deep" && !HttpUtil::IsAlreadyCompressedFile(filename));
            if (!HttpUtil::WriteFileAtomically(storage_path, content, compressed))
            {
                mylog::GetLogger("asynclogger")->Error("%s storage fail, evhttp_send_reply: HTTP_INTERNAL", storage_type.c_str());
                evhttp_send_reply(req, HTTP_INTERNAL, "server error", NULL);
                return;
            }
            mylog::GetLogger("asynclogger")->Info("%s_storage success, compressed:%d", storage_type.c_str(), compressed);

            // 添加存储文件信息，交由数据管理类进行管理
            StorageInfo info;
            std::string content_type = HttpUtil::NormalizeContentType(evhttp_find_header(req->input_headers, "Content-Type"));
            std::string content_hash = HttpUtil::CalculateContentHash(content);
            if (!info.NewStorageInfo(storage_path, storage_type, compressed, content.size(), content_type, content_hash, "fnv1a64") || !data_->Insert(info))
            {
                std::remove(storage_path.c_str());
                evhttp_send_reply(req, HTTP_INTERNAL, "save metadata failed", NULL);
                return;
            }

            evhttp_send_reply(req, HTTP_OK, "Success", NULL);
            mylog::GetLogger("asynclogger")->Info("upload finish:success");
        }

        static void ListShow(struct evhttp_request *req, void *arg)
        {
            mylog::GetLogger("asynclogger")->Info("ListShow()");
            // 1. 获取所有的文件存储信息
            std::vector<StorageInfo> arry;
            data_->GetAll(&arry);

            // 读取模板文件
            std::ifstream templateFile("index.html");
            std::string templateContent(
                (std::istreambuf_iterator<char>(templateFile)),
                std::istreambuf_iterator<char>());

            // 替换html文件中的占位符
            // 替换文件列表进html
            templateContent = std::regex_replace(templateContent,
                                                 std::regex("\\{\\{FILE_LIST\\}\\}"),
                                                 PageRender::GenerateModernFileList(arry));
            // 替换服务器地址进hrml
            templateContent = std::regex_replace(templateContent,
                                                 std::regex("\\{\\{BACKEND_URL\\}\\}"),
                                                 "http://" + storage::Config::GetInstance()->GetServerIp() + ":" + std::to_string(storage::Config::GetInstance()->GetServerPort()));
            // 获取请求的输出evbuffer
            struct evbuffer *buf = evhttp_request_get_output_buffer(req);
            auto response_body = templateContent;
            // 把前面的html数据给到evbuffer，然后设置响应头部字段，最后返回给浏览器
            evbuffer_add(buf, (const void *)response_body.c_str(), response_body.size());
            evhttp_add_header(req->output_headers, "Content-Type", "text/html;charset=utf-8");
            evhttp_send_reply(req, HTTP_OK, NULL, NULL);
            mylog::GetLogger("asynclogger")->Info("ListShow() finish");
        }
        static std::string GetETag(const StorageInfo &info)
        {
            // 自定义etag :  filename-fsize-mtime
            FileUtil fu(info.storage_path_);
            std::string etag = fu.FileName();
            etag += "-";
            etag += std::to_string(info.fsize_);
            etag += "-";
            etag += std::to_string(info.mtime_);
            return etag;
        }
        static void Delete(struct evhttp_request *req, void *arg)
        {
            if (evhttp_request_get_command(req) != EVHTTP_REQ_DELETE)
            {
                evhttp_send_reply(req, HTTP_BADMETHOD, "method not allowed", NULL);
                return;
            }

            std::string resource_path = evhttp_uri_get_path(evhttp_request_get_evhttp_uri(req));
            std::string decoded_path;
            if (!UrlDecode(resource_path, &decoded_path))
            {
                evhttp_send_reply(req, HTTP_BADREQUEST, "bad url", NULL);
                return;
            }
            resource_path = decoded_path;
            const std::string delete_prefix = "/delete/";
            if (resource_path.find(delete_prefix) != 0)
            {
                evhttp_send_reply(req, HTTP_BADREQUEST, "bad delete url", NULL);
                return;
            }

            std::string filename = resource_path.substr(delete_prefix.size());
            if (!HttpUtil::IsSafeFileName(filename))
            {
                evhttp_send_reply(req, HTTP_BADREQUEST, "illegal filename", NULL);
                return;
            }

            std::string download_url = Config::GetInstance()->GetDownloadPrefix() + filename;
            StorageInfo info;
            if (!data_->GetOneByURL(download_url, &info))
            {
                evhttp_send_reply(req, HTTP_NOTFOUND, "file not found", NULL);
                return;
            }

            FileUtil fu(info.storage_path_);
            if (fu.Exists() && remove(info.storage_path_.c_str()) != 0)
            {
                mylog::GetLogger("asynclogger")->Error("remove file error: %s -- %s", info.storage_path_.c_str(), strerror(errno));
                evhttp_send_reply(req, HTTP_INTERNAL, "delete file failed", NULL);
                return;
            }

            if (!data_->Delete(download_url))
            {
                evhttp_send_reply(req, HTTP_INTERNAL, "delete metadata failed", NULL);
                return;
            }

            evhttp_send_reply(req, HTTP_OK, "delete success", NULL);
            mylog::GetLogger("asynclogger")->Info("delete file success:%s", info.storage_path_.c_str());
        }
        static void Download(struct evhttp_request *req, void *arg)
        {
            // 1. 获取客户端请求的资源路径path   req.path
            // 2. 根据资源路径，获取StorageInfo
            StorageInfo info;
            std::string resource_path = evhttp_uri_get_path(evhttp_request_get_evhttp_uri(req));
            std::string decoded_path;
            if (!UrlDecode(resource_path, &decoded_path))
            {
                evhttp_send_reply(req, HTTP_BADREQUEST, "bad url", NULL);
                return;
            }
            resource_path = decoded_path;

            if (!data_->GetOneByURL(resource_path, &info))
            {
                evhttp_send_reply(req, HTTP_NOTFOUND, "file not found", NULL);
                return;
            }

            mylog::GetLogger("asynclogger")->Info("request resource_path:%s", resource_path.c_str());

            std::string download_path = info.storage_path_;
            // 2.如果压缩过了就解压到新文件给用户下载
            if (info.compressed_)
            {
                mylog::GetLogger("asynclogger")->Info("uncompressing:%s", info.storage_path_.c_str());
                FileUtil fu(info.storage_path_);
                download_path = Config::GetInstance()->GetLowStorageDir() +
                                std::string(download_path.begin() + download_path.find_last_of('/') + 1, download_path.end());
                FileUtil dirCreate(Config::GetInstance()->GetLowStorageDir());
                dirCreate.CreateDirectory();
                if (!fu.UnCompress(download_path)) // 将文件解压到low_storage下去或者再创一个文件夹做中转
                {
                    mylog::GetLogger("asynclogger")->Info("evhttp_send_reply: 500 - UnCompress failed");
                    evhttp_send_reply(req, HTTP_INTERNAL, "uncompress failed", NULL);
                    return;
                }
            }
            mylog::GetLogger("asynclogger")->Info("request download_path:%s", download_path.c_str());
            FileUtil fu(download_path);
            if (fu.Exists() == false && info.compressed_)
            {
                // 如果是压缩文件，且解压失败，是服务端的错误
                mylog::GetLogger("asynclogger")->Info("evhttp_send_reply: 500 - UnCompress failed");
                evhttp_send_reply(req, HTTP_INTERNAL, NULL, NULL);
                return;
            }
            else if (fu.Exists() == false)
            {
                // 如果是普通文件，且文件不存在，是客户端的错误
                mylog::GetLogger("asynclogger")->Info("evhttp_send_reply: 400 - bad request,file not exists");
                evhttp_send_reply(req, HTTP_BADREQUEST, "file not exists", NULL);
                return;
            }

            // 3.确认文件是否需要断点续传
            bool retrans = false;
            int64_t range_start = 0;
            int64_t range_length = 0;
            int64_t file_size = fu.FileSize();
            std::string etag = GetETag(info);

            auto range_header = evhttp_find_header(req->input_headers, "Range");
            auto if_range = evhttp_find_header(req->input_headers, "If-Range");
            if (range_header != NULL && (if_range == NULL || etag == if_range))
            {
                if (!HttpUtil::ParseRangeHeader(range_header, file_size, &range_start, &range_length))
                {
                    std::string content_range = "bytes */" + std::to_string(file_size);
                    evhttp_add_header(req->output_headers, "Content-Range", content_range.c_str());
                    evhttp_send_reply(req, 416, "range not satisfiable", NULL);
                    return;
                }
                retrans = true;
                mylog::GetLogger("asynclogger")->Info("%s need breakpoint continuous transmission", download_path.c_str());
            }

            // 4. 读取文件数据，放入rsp.body中
            if (fu.Exists() == false)
            {
                mylog::GetLogger("asynclogger")->Info("%s not exists", download_path.c_str());
                download_path += "not exists";
                evhttp_send_reply(req, 404, download_path.c_str(), NULL);
                return;
            }
            evbuffer *outbuf = evhttp_request_get_output_buffer(req);
            HttpUtil::UniqueFd fd(open(download_path.c_str(), O_RDONLY));
            if (!fd.Valid())
            {
                mylog::GetLogger("asynclogger")->Error("open file error: %s -- %s", download_path.c_str(), strerror(errno));
                evhttp_send_reply(req, HTTP_INTERNAL, strerror(errno), NULL);
                return;
            }
            if (!retrans)
            {
                range_start = 0;
                range_length = file_size;
            }
            // 和前面用的evbuffer_add类似，但是效率更高，具体原因可以看函数声明
            if (-1 == evbuffer_add_file(outbuf, fd.Get(), range_start, range_length))
            {
                mylog::GetLogger("asynclogger")->Error("evbuffer_add_file: %d -- %s -- %s", fd.Get(), download_path.c_str(), strerror(errno));
                evhttp_send_reply(req, HTTP_INTERNAL, "add file failed", NULL);
                return;
            }
            fd.Release();
            // 5. 设置响应头部字段： ETag， Accept-Ranges: bytes
            evhttp_add_header(req->output_headers, "Accept-Ranges", "bytes");
            evhttp_add_header(req->output_headers, "ETag", etag.c_str());
            evhttp_add_header(req->output_headers, "Content-Type", info.content_type_.empty() ? "application/octet-stream" : info.content_type_.c_str());
            evhttp_add_header(req->output_headers, "X-Storage-Type", info.storage_type_.empty() ? "low" : info.storage_type_.c_str());
            evhttp_add_header(req->output_headers, "X-Storage-Compressed", info.compressed_ ? "true" : "false");
            std::string original_size = std::to_string(info.original_size_);
            std::string stored_size = std::to_string(info.stored_size_);
            evhttp_add_header(req->output_headers, "X-Original-Size", original_size.c_str());
            evhttp_add_header(req->output_headers, "X-Stored-Size", stored_size.c_str());
            if (!info.content_hash_.empty())
            {
                evhttp_add_header(req->output_headers, "X-Content-Hash", info.content_hash_.c_str());
                evhttp_add_header(req->output_headers, "X-Hash-Algorithm", info.hash_algo_.empty() ? "fnv1a64" : info.hash_algo_.c_str());
            }
            if (retrans)
            {
                std::string content_range = "bytes " + std::to_string(range_start) + "-" +
                                            std::to_string(range_start + range_length - 1) + "/" +
                                            std::to_string(file_size);
                evhttp_add_header(req->output_headers, "Content-Range", content_range.c_str());
                evhttp_send_reply(req, 206, "breakpoint continuous transmission", NULL);
                mylog::GetLogger("asynclogger")->Info("evhttp_send_reply: 206");
            }
            else
            {
                evhttp_send_reply(req, HTTP_OK, "Success", NULL);
                mylog::GetLogger("asynclogger")->Info("evhttp_send_reply: HTTP_OK");
            }

            if (download_path != info.storage_path_)
            {
                remove(download_path.c_str()); // 删除文件
            }
        }
    };
}
