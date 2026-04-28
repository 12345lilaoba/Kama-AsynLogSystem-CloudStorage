#include <cassert>
#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <fstream>
#include <iomanip>
#include <memory>
#include <sstream>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>
#include "Util.hpp"

extern mylog::Util::JsonData* g_conf_data;
namespace mylog{
    class LogFlush
    {
    public:
        using ptr = std::shared_ptr<LogFlush>;
        virtual ~LogFlush() {}
        virtual void Flush(const char *data, size_t len) = 0;//不同的写文件方式Flush的实现不同
    };

    class StdoutFlush : public LogFlush
    {
    public:
        using ptr = std::shared_ptr<StdoutFlush>;
        void Flush(const char *data, size_t len) override{
            cout.write(data, len);
        }
    };
    class FileFlush : public LogFlush
    {
    public:
        using ptr = std::shared_ptr<FileFlush>;
        FileFlush(const std::string &filename) : filename_(filename)
        {
            // 创建所给目录
            Util::File::CreateDirectory(Util::File::Path(filename));
            // 打开文件
            fs_ = fopen(filename.c_str(), "ab");
            if(fs_==NULL){
                std::cout <<__FILE__<<__LINE__<<"open log file failed"<< std::endl;
                perror(NULL);
            }
        }
        ~FileFlush()
        {
            if (fs_ != NULL)
            {
                fclose(fs_);
                fs_ = NULL;
            }
        }
        void Flush(const char *data, size_t len) override{
            fwrite(data,1,len,fs_);
            if(ferror(fs_)){
                std::cout <<__FILE__<<__LINE__<<"write log file failed"<< std::endl;
                perror(NULL);
            }
            if(g_conf_data->flush_log == 1){
                if(fflush(fs_)==EOF){
                    std::cout <<__FILE__<<__LINE__<<"fflush file failed"<< std::endl;
                    perror(NULL);
                }
            }else if(g_conf_data->flush_log == 2){
                fflush(fs_);
                fsync(fileno(fs_));
            }
        }

    private:
        std::string filename_;
        FILE* fs_ = NULL; 
    };

    class RollFileFlush : public LogFlush
    {
    public:
        using ptr = std::shared_ptr<RollFileFlush>;
        RollFileFlush(const std::string &filename, size_t max_size = 0,
                      size_t max_count = 0, size_t max_age_days = 0)
            : max_size_(max_size == 0 ? g_conf_data->roll_file_max_size : max_size),
              max_count_(max_count == 0 ? g_conf_data->roll_file_max_count : max_count),
              max_age_days_(max_age_days == 0 ? g_conf_data->roll_file_max_age_days : max_age_days),
              basename_(filename),
              log_dir_(Util::File::Path(filename)),
              file_prefix_(Util::File::Name(filename))
        {
            Util::File::CreateDirectory(Util::File::Path(filename));
        }
        ~RollFileFlush()
        {
            if (fs_ != NULL)
            {
                fclose(fs_);
                fs_ = NULL;
            }
        }

        void Flush(const char *data, size_t len) override
        {
            InitLogFile(len);
            // 向文件写入内容
            fwrite(data, 1, len, fs_);
            if(ferror(fs_)){
                std::cout <<__FILE__<<__LINE__<<"write log file failed"<< std::endl;
                perror(NULL);
            }
            cur_size_ += len;
            if(g_conf_data->flush_log == 1){
                if(fflush(fs_)){
                    std::cout <<__FILE__<<__LINE__<<"fflush file failed"<< std::endl;
                    perror(NULL);
                }
            }else if(g_conf_data->flush_log == 2){
                fflush(fs_);
                fsync(fileno(fs_));
            }
        }

    private:
        void InitLogFile(size_t len)
        {
            std::string today = CurrentDay();
            if (fs_ == NULL || cur_size_ + len > max_size_ || today != cur_day_)
            {
                if(fs_!=NULL){
                    fclose(fs_);
                    fs_=NULL;
                }   
                std::string filename = CreateFilename();
                fs_=fopen(filename.c_str(), "ab");
                if(fs_==NULL){
                    std::cout <<__FILE__<<__LINE__<<"open file failed"<< std::endl;
                    perror(NULL);
                    return;
                }
                cur_size_ = 0;
                cur_day_ = today;
                CleanOldLogFiles();
            }
        }

        std::string CurrentDay()
        {
            time_t now = Util::Date::Now();
            struct tm t;
            localtime_r(&now, &t);
            std::ostringstream oss;
            oss << std::setfill('0')
                << std::setw(4) << t.tm_year + 1900
                << std::setw(2) << t.tm_mon + 1
                << std::setw(2) << t.tm_mday;
            return oss.str();
        }

        // 构建落地的滚动日志文件名称
        std::string CreateFilename()
        {
            time_t time_ = Util::Date::Now();
            struct tm t;
            localtime_r(&time_, &t);
            std::ostringstream oss;
            oss << basename_ << std::setfill('0')
                << std::setw(4) << t.tm_year + 1900
                << std::setw(2) << t.tm_mon + 1
                << std::setw(2) << t.tm_mday << '-'
                << std::setw(2) << t.tm_hour
                << std::setw(2) << t.tm_min
                << std::setw(2) << t.tm_sec << '-'
                << cnt_++ << ".log";
            return oss.str();
        }

        struct LogFileInfo
        {
            std::string path_;
            time_t mtime_;
        };

        bool IsRollLogFile(const std::string &filename)
        {
            if (filename.find(file_prefix_) != 0)
                return false;
            const std::string suffix = ".log";
            if (filename.size() < suffix.size())
                return false;
            return filename.compare(filename.size() - suffix.size(), suffix.size(), suffix) == 0;
        }

        void CleanOldLogFiles()
        {
            if (max_count_ == 0 && max_age_days_ == 0)
                return;

            std::string dir = log_dir_.empty() ? "." : log_dir_;
            DIR *dp = opendir(dir.c_str());
            if (dp == NULL)
            {
                std::cout << __FILE__ << __LINE__ << "open log dir failed:" << dir << std::endl;
                perror(NULL);
                return;
            }

            std::vector<LogFileInfo> files;
            struct dirent *entry = NULL;
            while ((entry = readdir(dp)) != NULL)
            {
                std::string name = entry->d_name;
                if (!IsRollLogFile(name))
                    continue;

                std::string path = dir;
                if (!path.empty() && path.back() != '/')
                    path += "/";
                path += name;

                struct stat st;
                if (stat(path.c_str(), &st) != 0)
                    continue;
                files.push_back({path, st.st_mtime});
            }
            closedir(dp);

            time_t now = Util::Date::Now();
            if (max_age_days_ > 0)
            {
                time_t max_age_seconds = static_cast<time_t>(max_age_days_ * 24 * 60 * 60);
                for (const auto &file : files)
                {
                    if (now - file.mtime_ > max_age_seconds)
                        std::remove(file.path_.c_str());
                }
            }

            if (max_count_ == 0 || files.size() <= max_count_)
                return;

            std::sort(files.begin(), files.end(), [](const LogFileInfo &left, const LogFileInfo &right) {
                return left.mtime_ < right.mtime_;
            });

            size_t remove_count = files.size() - max_count_;
            for (size_t i = 0; i < remove_count; ++i)
                std::remove(files[i].path_.c_str());
        }

    private:
        size_t cnt_ = 1;
        size_t cur_size_ = 0;
        size_t max_size_;
        size_t max_count_;
        size_t max_age_days_;
        std::string basename_;
        std::string log_dir_;
        std::string file_prefix_;
        std::string cur_day_;
        // std::ofstream ofs_;
        FILE* fs_ = NULL;
    };

    class LogFlushFactory
    {
    public:
        using ptr = std::shared_ptr<LogFlushFactory>;
        template <typename FlushType, typename... Args>
        static std::shared_ptr<LogFlush> CreateLog(Args &&...args)
        {
            return std::make_shared<FlushType>(std::forward<Args>(args)...);
        }
    };
} // namespace mylog
