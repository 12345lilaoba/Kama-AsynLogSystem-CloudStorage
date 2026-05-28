#pragma once

#include "DataManager.hpp"
#include "HttpUtil.hpp"
#include "PageRender.hpp"

#include <algorithm>
#include <cstdlib>
#include <event2/buffer.h>
#include <event2/http.h>
#include <evhttp.h>
#include <fstream>
#include <regex>
#include <string>
#include <unordered_map>
#include <vector>

namespace storage
{
    class ListHandler
    {
    private:
        struct ListQuery
        {
            size_t page = 1;
            size_t page_size = 10;
            std::string keyword;
            std::string sort = "upload-desc";
        };

        static bool ParseSizeValue(const std::string &raw, size_t *value)
        {
            if (raw.empty())
                return false;

            char *parse_end = nullptr;
            unsigned long parsed = std::strtoul(raw.c_str(), &parse_end, 10);
            if (parse_end == raw.c_str() || *parse_end != '\0' || parsed == 0)
                return false;

            *value = parsed;
            return true;
        }

        static bool DecodeQueryValue(const std::string &raw, std::string *decoded)
        {
            std::string normalized = raw;
            std::replace(normalized.begin(), normalized.end(), '+', ' ');
            return UrlDecode(normalized, decoded);
        }

        static bool ParseQueryParams(const char *query, std::unordered_map<std::string, std::string> *params)
        {
            if (query == nullptr)
                return true;

            std::string query_str = query;
            size_t pos = 0;
            while (pos <= query_str.size())
            {
                size_t end = query_str.find('&', pos);
                std::string pair = query_str.substr(pos, end == std::string::npos ? std::string::npos : end - pos);
                if (!pair.empty())
                {
                    size_t eq = pair.find('=');
                    std::string raw_key = pair.substr(0, eq);
                    std::string raw_value = eq == std::string::npos ? "" : pair.substr(eq + 1);
                    std::string key;
                    std::string value;
                    if (!DecodeQueryValue(raw_key, &key) || !DecodeQueryValue(raw_value, &value))
                        return false;
                    (*params)[key] = value;
                }
                if (end == std::string::npos)
                    break;
                pos = end + 1;
            }
            return true;
        }

        static bool ParseListQuery(const char *query, ListQuery *list_query)
        {
            std::unordered_map<std::string, std::string> params;
            if (!ParseQueryParams(query, &params))
                return false;

            auto page_it = params.find("page");
            if (page_it != params.end())
                ParseSizeValue(page_it->second, &list_query->page);

            auto page_size_it = params.find("page_size");
            if (page_size_it != params.end())
                ParseSizeValue(page_size_it->second, &list_query->page_size);

            if (list_query->page_size < 1)
                list_query->page_size = 10;
            if (list_query->page_size > 100)
                list_query->page_size = 100;

            auto keyword_it = params.find("q");
            if (keyword_it != params.end())
            {
                list_query->keyword = keyword_it->second;
                if (list_query->keyword.size() > 100)
                    list_query->keyword.resize(100);
            }

            auto sort_it = params.find("sort");
            if (sort_it != params.end() &&
                (sort_it->second == "upload-desc" || sort_it->second == "upload-asc" ||
                 sort_it->second == "name-asc" || sort_it->second == "name-desc" ||
                 sort_it->second == "size-desc" || sort_it->second == "size-asc"))
            {
                list_query->sort = sort_it->second;
            }
            return true;
        }

    public:
        static HttpUtil::HttpResult Handle(struct evhttp_request *req, DataManager &data_manager)
        {
            mylog::GetLogger("asynclogger")->Info("ListShow()");

            ListQuery list_query;
            const char *raw_query = evhttp_uri_get_query(evhttp_request_get_evhttp_uri(req));
            if (!ParseListQuery(raw_query, &list_query))
            {
                evhttp_send_reply(req, HTTP_BADREQUEST, "bad query", NULL);
                return HttpUtil::HttpResult(HTTP_BADREQUEST, "bad_query");
            }

            size_t total = 0;
            std::vector<StorageInfo> page_files;
            if (!data_manager.QueryList(list_query.keyword, list_query.sort, &list_query.page, list_query.page_size, &page_files, &total))
            {
                evhttp_send_reply(req, HTTP_INTERNAL, "query list failed", NULL);
                return HttpUtil::HttpResult(HTTP_INTERNAL, "query_list_failed");
            }

            std::ifstream templateFile("index.html");
            std::string templateContent(
                (std::istreambuf_iterator<char>(templateFile)),
                std::istreambuf_iterator<char>());

            templateContent = std::regex_replace(templateContent,
                                                 std::regex("\\{\\{FILE_LIST\\}\\}"),
                                                 PageRender::GenerateModernFileList(page_files, total, list_query.page, list_query.page_size,
                                                                                    list_query.keyword, list_query.sort));
            struct evbuffer *buf = evhttp_request_get_output_buffer(req);
            evbuffer_add(buf, (const void *)templateContent.c_str(), templateContent.size());
            evhttp_add_header(req->output_headers, "Content-Type", "text/html;charset=utf-8");
            evhttp_send_reply(req, HTTP_OK, NULL, NULL);
            mylog::GetLogger("asynclogger")->Info("ListShow() finish");
            return HttpUtil::HttpResult(HTTP_OK, "ok", templateContent.size());
        }
    };
}
