#pragma once
#include <string>
#include <vector>

namespace openlyrics {

class FileSystem {
public:
    virtual ~FileSystem() = default;
    virtual bool readFile(const std::string& path, std::string& out) = 0;
    virtual bool writeFile(const std::string& path, const std::string& data) = 0;
    // 列出 dir 目录下的条目文件名（basename，不含目录前缀）；dir 不存在时返回空 vector，
    // 不抛异常。供 LocalFileSource 做大小写不敏感的精确匹配 + 标准化标题模糊匹配用。
    virtual std::vector<std::string> listDirectory(const std::string& dir) = 0;
};

}  // namespace openlyrics
