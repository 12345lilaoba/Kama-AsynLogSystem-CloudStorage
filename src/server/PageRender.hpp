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

        static std::string GenerateModernFileList(const std::vector<StorageInfo> &files)
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
                std::string escaped_filename = HttpUtil::HtmlEscape(filename);
                std::string encoded_filename = HttpUtil::UrlEncodePathSegment(filename);
                std::string escaped_url = HttpUtil::HtmlEscape(Config::GetInstance()->GetDownloadPrefix() + encoded_filename);
                std::string escaped_delete_url = HttpUtil::HtmlEscape("/delete/" + encoded_filename);
                std::string escaped_upload_time = HttpUtil::HtmlEscape(TimeToStr(file.upload_time_));

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
                   << "<span class='file-size'>" << FormatSize(display_size) << "</span>"
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
    }
}
