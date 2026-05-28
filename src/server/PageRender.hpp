#pragma once

#include "DataManager.hpp"
#include "HttpUtil.hpp"

#include <ctime>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

namespace storage
{
    namespace PageRender
    {
        static std::string TimeToStr(time_t t)
        {
            std::string tmp = std::ctime(&t);
            if (!tmp.empty() && tmp.back() == '\n')
                tmp.pop_back();
            return tmp;
        }

        static std::string FormatSize(uint64_t bytes)
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

        static std::string SelectedAttr(const std::string &left, const std::string &right)
        {
            return left == right ? " selected" : "";
        }

        static std::string BuildPageLink(size_t page,
                                         size_t page_size,
                                         const std::string &keyword,
                                         const std::string &sort)
        {
            std::stringstream ss;
            ss << "/?page=" << page << "&page_size=" << page_size;
            if (!keyword.empty())
                ss << "&q=" << HttpUtil::UrlEncodePathSegment(keyword);
            if (sort != "upload-desc")
                ss << "&sort=" << HttpUtil::UrlEncodePathSegment(sort);
            return ss.str();
        }

        static std::string GenerateModernFileList(const std::vector<StorageInfo> &files,
                                                  size_t total_files,
                                                  size_t current_page,
                                                  size_t page_size,
                                                  const std::string &keyword,
                                                  const std::string &sort)
        {
            size_t page_count = total_files == 0 ? 1 : (total_files + page_size - 1) / page_size;
            if (current_page > page_count)
                current_page = page_count;

            std::string escaped_keyword = HttpUtil::HtmlEscape(keyword);
            std::stringstream ss;
            ss << "<section class='file-list' data-total='" << total_files << "' data-page-count='" << files.size() << "'>"
               << "<div class='file-list-header'>"
               << "<h2>已上传文件</h2>"
               << "<form class='file-tools' method='get' action='/'>"
               << "<input type='hidden' name='page' value='1'>"
               << "<input id='fileSearch' name='q' type='search' placeholder='搜索文件名' value='" << escaped_keyword << "'>"
               << "<select name='sort' onchange='this.form.submit()'>"
               << "<option value='upload-desc'" << SelectedAttr(sort, "upload-desc") << ">最近上传</option>"
               << "<option value='upload-asc'" << SelectedAttr(sort, "upload-asc") << ">最早上传</option>"
               << "<option value='name-asc'" << SelectedAttr(sort, "name-asc") << ">文件名 A-Z</option>"
               << "<option value='name-desc'" << SelectedAttr(sort, "name-desc") << ">文件名 Z-A</option>"
               << "<option value='size-desc'" << SelectedAttr(sort, "size-desc") << ">文件从大到小</option>"
               << "<option value='size-asc'" << SelectedAttr(sort, "size-asc") << ">文件从小到大</option>"
               << "</select>"
               << "<select name='page_size' onchange='this.form.submit()'>"
               << "<option value='10'" << (page_size == 10 ? " selected" : "") << ">每页 10 条</option>"
               << "<option value='20'" << (page_size == 20 ? " selected" : "") << ">每页 20 条</option>"
               << "<option value='50'" << (page_size == 50 ? " selected" : "") << ">每页 50 条</option>"
               << "<option value='100'" << (page_size == 100 ? " selected" : "") << ">每页 100 条</option>"
               << "</select>"
               << "<button type='submit'>查询</button>"
               << "<a class='reset-link' href='/'>重置</a>"
               << "</form>"
               << "</div>"
               << "<div id='fileCount' class='file-count'></div>"
               << "<div id='fileItems'>";

            for (const auto &file : files)
            {
                std::string filename = FileUtil(file.storage_path_).FileName();
                std::string escaped_filename = HttpUtil::HtmlEscape(filename);
                std::string encoded_filename = HttpUtil::UrlEncodePathSegment(filename);
                std::string escaped_url = HttpUtil::HtmlEscape(Config::GetInstance()->GetDownloadPrefix() + encoded_filename);
                std::string escaped_download_url = HttpUtil::HtmlEscape(Config::GetInstance()->GetDownloadPrefix() + encoded_filename + "?download=1");
                std::string escaped_delete_url = HttpUtil::HtmlEscape("/delete/" + encoded_filename);
                std::string escaped_upload_time = HttpUtil::HtmlEscape(TimeToStr(file.upload_time_));

                size_t display_size = file.original_size_ == 0 ? file.fsize_ : file.original_size_;

                ss << "<div class='file-item' data-name='" << escaped_filename
                   << "' data-size='" << display_size
                   << "' data-upload-time='" << file.upload_time_ << "'>"
                   << "<div class='file-info'>"
                   << "<span class='file-name'>📄" << escaped_filename << "</span>"
                   << "<span class='file-size'>" << FormatSize(display_size) << "</span>"
                   << "<span class='file-time'>上传 " << escaped_upload_time << "</span>"
                   << "</div>"
                   << "<div class='file-actions'>"
                   << "<button data-url='" << escaped_url << "' onclick=\"window.open(this.dataset.url, '_blank')\">预览</button>"
                   << "<button data-url='" << escaped_download_url << "' onclick=\"window.location=this.dataset.url\">下载</button>"
                   << "<button class='delete-button' data-url='" << escaped_delete_url << "' onclick=\"deleteFile(this.dataset.url)\">删除</button>"
                   << "</div>"
                   << "</div>";
            }

            ss << "</div>"
               << "<div id='emptyState' class='empty-state'>暂无匹配文件</div>"
               << "<div class='pagination'>";
            if (current_page > 1)
                ss << "<a href='" << BuildPageLink(current_page - 1, page_size, keyword, sort) << "'>上一页</a>";
            else
                ss << "<span class='page-disabled'>上一页</span>";

            ss << "<span>第 " << current_page << " / " << page_count << " 页</span>";

            if (current_page < page_count)
                ss << "<a href='" << BuildPageLink(current_page + 1, page_size, keyword, sort) << "'>下一页</a>";
            else
                ss << "<span class='page-disabled'>下一页</span>";

            ss << "</div>"
               << "</section>";
            return ss.str();
        }
    }
}
