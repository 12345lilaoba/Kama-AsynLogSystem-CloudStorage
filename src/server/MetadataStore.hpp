#pragma once

#include "StorageInfo.hpp"
#include <string>
#include <vector>

namespace storage
{
    class MetadataStore
    {
    public:
        virtual ~MetadataStore() = default;
        virtual bool Insert(const StorageInfo &info) = 0;
        virtual bool Update(const StorageInfo &info) = 0;
        virtual bool Delete(const std::string &key) = 0;
        virtual bool GetOneByURL(const std::string &key, StorageInfo *info) = 0;
        virtual bool GetAll(std::vector<StorageInfo> *arry) = 0;
        virtual bool QueryList(const std::string &keyword, const std::string &sort, size_t *page, size_t page_size, std::vector<StorageInfo> *files, size_t *total) = 0;
    };
}
