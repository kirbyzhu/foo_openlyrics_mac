// TagIOAdapter.h
// foo_openlyrics_mac —— Plan 2 Task 5：TagIO 端口的 foobar2000 metadb 实现。
//
// 头文件只暴露纯 C++ 接口（openlyrics::TagIO 的实现），不引入任何 SDK/Cocoa 类型，
// 保持与 core/ports/TagIO.h 相同的可移植性；真正触碰 metadb_handle/file_info 的代码
// 全部关在 TagIOAdapter.mm 里（需要 stdafx.h 拉入 SDK 头）。
#pragma once
#include "ports/TagIO.h"

namespace openlyrics {

// 实现要点见 TagIOAdapter.mm 顶部注释：
// - 读取字段：LYRICS / UNSYNCEDLYRICS / SYNCEDLYRICS，foobar2000 file_info::meta_find_ex
//   内部按 stricmp_ascii_ex 做大小写不敏感比较（SDK/file_info.cpp:19），故不必再枚举大小写变体。
// - 线程：readLyricTag 可能被 SearchPipeline::resolve 从后台队列调用；而 metadb 相关 API
//   在 SDK 注释里明确"仅主线程"（playback_control，SDK/playback_control.h:4）或未注明保证，
//   保守起见，实际的 metadb::handle_create()/metadb_handle::get_info() 统一 dispatch 回主线程执行。
// - writeLyricTag 暂为桩实现（返回 false），写入是后续计划的任务。
class TagIOAdapter : public TagIO {
public:
    bool readLyricTag(const TrackMeta& track, std::string& out) override;
    bool writeLyricTag(const TrackMeta& track, const std::string& lrc) override;
};

}  // namespace openlyrics
