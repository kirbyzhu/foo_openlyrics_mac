#pragma once
#include <string>
#include <vector>

namespace openlyrics {

class FileSystem {
public:
    virtual ~FileSystem() = default;
    virtual bool readFile(const std::string& path, std::string& out) = 0;
    virtual bool writeFile(const std::string& path, const std::string& data) = 0;
    // 删除文件；成功或文件本不存在返回 true，出错返回 false。默认实现不删除、返回 false，
    // 仅需要删除能力的适配器（FileSystemAdapter）覆盖，测试 mock 无需实现。
    virtual bool removeFile(const std::string& path) { (void)path; return false; }
    // 列出 dir 目录下的条目文件名（basename，不含目录前缀）；dir 不存在时返回空 vector，
    // 不抛异常。供 LocalFileSource 做大小写不敏感的精确匹配 + 标准化标题模糊匹配用。
    virtual std::vector<std::string> listDirectory(const std::string& dir) = 0;
};

}  // namespace openlyrics
