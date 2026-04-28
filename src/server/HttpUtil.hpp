#pragma once

#include "Config.hpp"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <iomanip>
#include <sstream>
#include <string>
#include <unordered_set>
#include <unistd.h>

namespace storage
{
    namespace HttpUtil
    {
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

        static std::string CalculateContentHash(const std::string &content)
        {
            uint64_t hash = 1469598103934665603ULL;
            for (unsigned char ch : content)
            {
                hash ^= ch;
                hash *= 1099511628211ULL;
            }

            std::stringstream ss;
            ss << std::hex << std::setfill('0') << std::setw(16) << hash;
            return ss.str();
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
    }
}
