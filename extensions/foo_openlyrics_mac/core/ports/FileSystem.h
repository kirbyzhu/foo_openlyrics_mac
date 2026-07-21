#pragma once
#include <string>

namespace openlyrics {

class FileSystem {
public:
    virtual ~FileSystem() = default;
    virtual bool exists(const std::string& path) = 0;
    virtual bool readFile(const std::string& path, std::string& out) = 0;
    virtual bool writeFile(const std::string& path, const std::string& data) = 0;
};

}  // namespace openlyrics
