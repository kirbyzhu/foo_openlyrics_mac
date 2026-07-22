// FileSystemAdapter.mm
// foo_openlyrics_mac —— Plan 2 Task 5：FileSystemAdapter 实现，标准库文件 I/O。
//
// 全部用 <fstream>，不牵涉 Cocoa/Foundation：macOS 的 POSIX 层按 UTF-8 字节路径工作，
// std::ifstream/ofstream 直接吃 std::string 路径即可，没有必要为 exists() 单独引入
// NSFileManager。文件本身很小（歌词文本通常几 KB），一次性整读整写，不做流式处理。
#import "FileSystemAdapter.h"
#import "stdafx.h"

#include <fstream>
#include <sstream>

namespace openlyrics {

bool FileSystemAdapter::exists(const std::string& path) {
    if (path.empty()) return false;
    std::ifstream f(path);
    return f.good();
}

bool FileSystemAdapter::readFile(const std::string& path, std::string& out) {
    if (path.empty()) return false;
    std::ifstream f(path, std::ios::in | std::ios::binary);
    if (!f) return false;

    std::ostringstream buffer;
    buffer << f.rdbuf();
    if (f.bad()) return false;  // rdbuf 读取过程中出现底层 I/O 错误

    out = buffer.str();
    return true;
}

bool FileSystemAdapter::writeFile(const std::string& path, const std::string& data) {
    if (path.empty()) return false;
    std::ofstream f(path, std::ios::out | std::ios::binary | std::ios::trunc);
    if (!f) return false;

    f.write(data.data(), static_cast<std::streamsize>(data.size()));
    return f.good();
}

}  // namespace openlyrics
