#pragma once

#include "Config.hpp"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <event2/http.h>
#include <iomanip>
#include <sstream>
#include <string>
#include <unistd.h>

namespace storage
{
    namespace HttpUtil
    {
        struct HttpResult
        {
            int status;
            std::string reason;
            size_t bytes;

            HttpResult(int code = HTTP_OK, const std::string &message = "ok", size_t byte_count = 0)
                : status(code),
                  reason(message),
                  bytes(byte_count)
            {
            }
        };

        class UniqueFd
        {
        private:
            int fd_;

        public:
            explicit UniqueFd(int fd = -1) : fd_(fd) {}
            ~UniqueFd()
            {
                if (fd_ != -1)
                    close(fd_);
            }

            UniqueFd(const UniqueFd &) = delete;
            UniqueFd &operator=(const UniqueFd &) = delete;

            UniqueFd(UniqueFd &&other) noexcept : fd_(other.fd_)
            {
                other.fd_ = -1;
            }

            UniqueFd &operator=(UniqueFd &&other) noexcept
            {
                if (this != &other)
                {
                    if (fd_ != -1)
                        close(fd_);
                    fd_ = other.fd_;
                    other.fd_ = -1;
                }
                return *this;
            }

            int Get() const
            {
                return fd_;
            }

            int Release()
            {
                int fd = fd_;
                fd_ = -1;
                return fd;
            }

            bool Valid() const
            {
                return fd_ != -1;
            }
        };

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

        static bool WriteFileAtomically(const std::string &storage_path, const std::string &content)
        {
            std::string tmp_path = storage_path + ".uploading";
            std::remove(tmp_path.c_str());

            FileUtil tmp(tmp_path);
            if (!tmp.SetContent(content.c_str(), content.size()))
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

    }
}
