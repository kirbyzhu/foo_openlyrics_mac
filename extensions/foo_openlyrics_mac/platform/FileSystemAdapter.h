// FileSystemAdapter.h
// foo_openlyrics_mac —— Plan 2 Task 5：FileSystem 端口的标准库实现。
//
// 纯粹基于 std::ifstream/ofstream + NSFileManager 判存在性，不依赖 metadb/playback_control，
// 因此没有线程限制——LocalFileSource 通过本适配器做的磁盘 I/O 可以放心放在后台队列跑，
// 这也是 LyricPanelController 把 SearchPipeline::resolve() 整体丢到后台队列的原因之一
// （另一原因见 TagIOAdapter.mm 顶部注释：TagIOAdapter 内部自行处理主线程切换）。
#pragma once
#include "ports/FileSystem.h"

namespace openlyrics {

class FileSystemAdapter : public FileSystem {
public:
    bool readFile(const std::string& path, std::string& out) override;
    bool writeFile(const std::string& path, const std::string& data) override;
    bool removeFile(const std::string& path) override;
    std::vector<std::string> listDirectory(const std::string& dir) override;
};

}  // namespace openlyrics
