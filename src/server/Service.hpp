#pragma once
#include "DataManager.hpp"

#include <sys/queue.h>
#include <event.h>
// for http
#include <evhttp.h>
#include <event2/http.h>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <regex>
#include <unordered_set>

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
            event_base *base = event_base_new();
            if (base == NULL)
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
            evhttp *httpd = evhttp_new(base);
            // 绑定端口和ip
            if (evhttp_bind_socket(httpd, "0.0.0.0", server_port_) != 0)
            {
                mylog::GetLogger("asynclogger")->Fatal("evhttp_bind_socket failed!");
                return false;
            }
            // 设定回调函数
            // 指定generic callback，也可以为特定的URI指定callback
            evhttp_set_gencb(httpd, GenHandler, NULL);

            if (base)
            {
#ifdef DEBUG_LOG
                mylog::GetLogger("asynclogger")->Debug("event_base_dispatch");
#endif
                if (-1 == event_base_dispatch(base))
                {
                    mylog::GetLogger("asynclogger")->Debug("event_base_dispatch err");
                }
            }
            if (base)
                event_base_free(base);
            if (httpd)
                evhttp_free(httpd);
            return true;
        }

    private:
        uint16_t server_port_;
        std::string server_ip_;
        std::string download_prefix_;

    private:
        static bool IsSafeFileName(const std::string &filename)
        {
            if (filename.empty() || filename.size() > 255)
                return false;
            if (filename == "." || filename == "..")
                return false;
            if (filename.find('/') != std::string::npos ||
                filename.find('\\') != std::string::npos ||
                filename.find("..") != std::string::npos)
                return false;

            for (unsigned char ch : filename)
            {
                if (ch < 32 || ch == 127)
                    return false;
            }
            return true;
        }

        static std::string HtmlEscape(const std::string &input)
        {
            std::string output;
            for (char ch : input)
            {
                switch (ch)
                {
                case '&':
                    output += "&amp;";
                    break;
                case '<':
                    output += "&lt;";
                    break;
                case '>':
                    output += "&gt;";
                    break;
                case '"':
                    output += "&quot;";
                    break;
                case '\'':
                    output += "&#39;";
                    break;
                default:
                    output += ch;
                    break;
                }
            }
            return output;
        }

        static std::string UrlEncodePathSegment(const std::string &input)
        {
            static const char *hex = "0123456789ABCDEF";
            std::string output;
            for (unsigned char ch : input)
            {
                if (std::isalnum(ch) || ch == '-' || ch == '_' || ch == '.')
                {
                    output += ch;
                }
                else
                {
                    output += '%';
                    output += hex[ch >> 4];
                    output += hex[ch & 0x0F];
                }
            }
            return output;
        }

        static std::string Lowercase(std::string input)
        {
            for (char &ch : input)
                ch = std::tolower(static_cast<unsigned char>(ch));
            return input;
        }

        static bool IsAlreadyCompressedFile(const std::string &filename)
        {
            static const std::unordered_set<std::string> compressed_exts = {
                ".zip", ".gz", ".bz2", ".xz", ".7z", ".rar", ".tgz",
                ".jpg", ".jpeg", ".png", ".gif", ".webp",
                ".mp4", ".mkv", ".avi", ".mov", ".mp3", ".aac", ".flac",
                ".pdf"};

            std::string lower_filename = Lowercase(filename);
            size_t dot_pos = lower_filename.find_last_of('.');
            if (dot_pos == std::string::npos)
                return false;
            return compressed_exts.find(lower_filename.substr(dot_pos)) != compressed_exts.end();
        }

        static std::string NormalizeContentType(const char *content_type)
        {
            if (content_type == nullptr || content_type[0] == '\0')
                return "application/octet-stream";

            std::string normalized = content_type;
            if (normalized.size() > 128)
                normalized.resize(128);

            for (char &ch : normalized)
            {
                unsigned char uch = static_cast<unsigned char>(ch);
                if (uch < 32 || uch == 127)
                    ch = ' ';
            }
            return normalized;
        }

        static bool WriteFileAtomically(const std::string &storage_path, const std::string &content, bool compress_content)
        {
            std::string tmp_path = storage_path + ".uploading";
            std::remove(tmp_path.c_str());

            FileUtil tmp(tmp_path);
            bool ok = false;
            if (compress_content)
                ok = tmp.Compress(content, Config::GetInstance()->GetBundleFormat());
            else
                ok = tmp.SetContent(content.c_str(), content.size());

            if (!ok)
            {
                std::remove(tmp_path.c_str());
                return false;
            }

            if (std::rename(tmp_path.c_str(), storage_path.c_str()) != 0)
            {
                mylog::GetLogger("asynclogger")->Error("rename upload tmp error:%s", strerror(errno));
                std::remove(tmp_path.c_str());
                return false;
            }
            return true;
        }

        static bool ParseRangeHeader(const std::string &range_header, int64_t file_size, int64_t *start, int64_t *length)
        {
            const std::string prefix = "bytes=";
            if (file_size <= 0 || range_header.find(prefix) != 0)
                return false;

            std::string range = range_header.substr(prefix.size());
            if (range.find(',') != std::string::npos)
                return false;

            size_t dash_pos = range.find('-');
            if (dash_pos == std::string::npos)
                return false;

            std::string start_str = range.substr(0, dash_pos);
            std::string end_str = range.substr(dash_pos + 1);
            if (start_str.empty() && end_str.empty())
                return false;

            char *endptr = nullptr;
            if (start_str.empty())
            {
                errno = 0;
                long long suffix_len = std::strtoll(end_str.c_str(), &endptr, 10);
                if (errno != 0 || endptr == end_str.c_str() || *endptr != '\0' || suffix_len <= 0)
                    return false;
                if (suffix_len > file_size)
                    suffix_len = file_size;
                *start = file_size - suffix_len;
                *length = suffix_len;
                return true;
            }

            errno = 0;
            long long parsed_start = std::strtoll(start_str.c_str(), &endptr, 10);
            if (errno != 0 || endptr == start_str.c_str() || *endptr != '\0' || parsed_start < 0 || parsed_start >= file_size)
                return false;

            long long parsed_end = file_size - 1;
            if (!end_str.empty())
            {
                errno = 0;
                parsed_end = std::strtoll(end_str.c_str(), &endptr, 10);
                if (errno != 0 || endptr == end_str.c_str() || *endptr != '\0' || parsed_end < parsed_start)
                    return false;
                if (parsed_end >= file_size)
                    parsed_end = file_size - 1;
            }

            *start = parsed_start;
            *length = parsed_end - parsed_start + 1;
            return true;
        }

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

            if (!IsSafeFileName(filename))
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

            bool compressed = (storage_type == "deep" && !IsAlreadyCompressedFile(filename));
            if (!WriteFileAtomically(storage_path, content, compressed))
            {
                mylog::GetLogger("asynclogger")->Error("%s storage fail, evhttp_send_reply: HTTP_INTERNAL", storage_type.c_str());
                evhttp_send_reply(req, HTTP_INTERNAL, "server error", NULL);
                return;
            }
            mylog::GetLogger("asynclogger")->Info("%s_storage success, compressed:%d", storage_type.c_str(), compressed);

            // 添加存储文件信息，交由数据管理类进行管理
            StorageInfo info;
            std::string content_type = NormalizeContentType(evhttp_find_header(req->input_headers, "Content-Type"));
            if (!info.NewStorageInfo(storage_path, storage_type, compressed, content.size(), content_type) || !data_->Insert(info))
            {
                std::remove(storage_path.c_str());
                evhttp_send_reply(req, HTTP_INTERNAL, "save metadata failed", NULL);
                return;
            }

            evhttp_send_reply(req, HTTP_OK, "Success", NULL);
            mylog::GetLogger("asynclogger")->Info("upload finish:success");
        }

        static std::string TimetoStr(time_t t)
        {
            std::string tmp = std::ctime(&t);
            if (!tmp.empty() && tmp.back() == '\n')
                tmp.pop_back();
            return tmp;
        }

        // 前端代码处理函数
        // 在渲染函数中直接处理StorageInfo
        static std::string generateModernFileList(const std::vector<StorageInfo> &files)
        {
            std::stringstream ss;
            ss << "<section class='file-list' data-total='" << files.size() << "'>"
               << "<div class='file-list-header'>"
               << "<h2>已上传文件</h2>"
               << "<div class='file-tools'>"
               << "<input id='fileSearch' type='search' placeholder='搜索文件名' oninput='filterFiles()'>"
               << "<select id='fileSort' onchange='sortFiles()'>"
               << "<option value='upload-desc'>最近上传</option>"
               << "<option value='name-asc'>文件名 A-Z</option>"
               << "<option value='size-desc'>文件大小</option>"
               << "</select>"
               << "</div>"
               << "</div>"
               << "<div id='fileCount' class='file-count'></div>"
               << "<div id='fileItems'>";

            for (const auto &file : files)
            {
                std::string filename = FileUtil(file.storage_path_).FileName();
                std::string escaped_filename = HtmlEscape(filename);
                std::string encoded_filename = UrlEncodePathSegment(filename);
                std::string escaped_url = HtmlEscape(Config::GetInstance()->GetDownloadPrefix() + encoded_filename);
                std::string escaped_delete_url = HtmlEscape("/delete/" + encoded_filename);
                std::string escaped_upload_time = HtmlEscape(TimetoStr(file.upload_time_));

                std::string storage_type = file.storage_type_.empty() ? "low" : file.storage_type_;
                std::string storage_label = "普通存储";
                if (storage_type == "deep")
                    storage_label = file.compressed_ ? "深度存储-已压缩" : "深度存储-原样保存";
                size_t display_size = file.original_size_ == 0 ? file.fsize_ : file.original_size_;

                ss << "<div class='file-item' data-name='" << escaped_filename
                   << "' data-size='" << display_size
                   << "' data-upload-time='" << file.upload_time_ << "'>"
                   << "<div class='file-info'>"
                   << "<span class='file-name'>📄" << escaped_filename << "</span>"
                   << "<span class='file-type'>"
                   << storage_label
                   << "</span>"
                   << "<span class='file-size'>" << formatSize(display_size) << "</span>"
                   << "<span class='file-time'>上传 " << escaped_upload_time << "</span>"
                   << "</div>"
                   << "<div class='file-actions'>"
                   << "<button data-url='" << escaped_url << "' onclick=\"window.location=this.dataset.url\">⬇️ 下载</button>"
                   << "<button class='delete-button' data-url='" << escaped_delete_url << "' onclick=\"deleteFile(this.dataset.url)\">删除</button>"
                   << "</div>"
                   << "</div>";
            }

            ss << "</div>"
               << "<div id='emptyState' class='empty-state'>暂无匹配文件</div>"
               << "</section>";
            return ss.str();
        }

        // 文件大小格式化函数
        static std::string formatSize(uint64_t bytes)
        {
            const char *units[] = {"B", "KB", "MB", "GB"};
            int unit_index = 0;
            double size = bytes;

            while (size >= 1024 && unit_index < 3)
            {
                size /= 1024;
                unit_index++;
            }

            std::stringstream ss;
            ss << std::fixed << std::setprecision(2) << size << " " << units[unit_index];
            return ss.str();
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
                                                 generateModernFileList(arry));
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
            if (!IsSafeFileName(filename))
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
                if (!ParseRangeHeader(range_header, file_size, &range_start, &range_length))
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
            int fd = open(download_path.c_str(), O_RDONLY);
            if (fd == -1)
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
            if (-1 == evbuffer_add_file(outbuf, fd, range_start, range_length))
            {
                mylog::GetLogger("asynclogger")->Error("evbuffer_add_file: %d -- %s -- %s", fd, download_path.c_str(), strerror(errno));
                close(fd);
                evhttp_send_reply(req, HTTP_INTERNAL, "add file failed", NULL);
                return;
            }
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
