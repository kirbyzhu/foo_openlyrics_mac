// TagIOAdapter.mm
// foo_openlyrics_mac —— Plan 2 Task 5：TagIOAdapter 实现，读 metadb 内嵌歌词标签。
//
// 字段候选：LYRICS（最常见，多数打标工具写纯文本或整段 LRC 到此字段）、UNSYNCEDLYRICS
// （id3 UFID/USLT 常见写法迁移过来的命名）、SYNCEDLYRICS（少数工具用此名存 LRC 格式文本）。
// file_info::meta_find_ex 内部用 pfc::stricmp_ascii_ex 做大小写不敏感比较
// （SDK-2025-03-07/foobar2000/SDK/file_info.cpp:19），所以候选列表不需要再枚举大小写变体，
// "LYRICS" 与 "lyrics"/"Lyrics" 命中同一字段。
//
// 线程模型：本文件对照 SDK 注释核实——playback_control 的方法明确"仅主线程可调用，否则不生效
// 或抛异常"（SDK/playback_control.h:4）；metadb/metadb_handle 一族虽未见同样措辞的强约束，
// 但 metadb_handle::get_info() 的文档提到"会临时锁住 metadb，不可在禁止加锁的上下文调用"，
// 且"缓存状态只在主线程变化"（SDK/metadb_handle.h:63-64/67-68）。保守起见，本适配器把真正的
// metadb::handle_create() + metadb_handle::get_info() 调用统一收敛到主线程执行：
// readLyricTag() 若发现自己被非主线程调用（即 SearchPipeline::resolve 跑在
// LyricPanelController.mm 的后台队列时），用 dispatch_sync 切回主线程再做实际查询，
// 查询本身很轻（只读已缓存的 file_info，不做磁盘 I/O），dispatch_sync 的往返开销可忽略。
#import "TagIOAdapter.h"
#import "stdafx.h"

namespace openlyrics {

namespace {

// 实际的 metadb 查询逻辑，调用方必须保证在主线程执行。
bool ReadLyricTagOnMainThread(const std::string& path, std::string& out) {
    if (path.empty()) return false;

    auto db = metadb::get();
    if (db.is_empty()) return false;

    // handle_create 只是"取或建" metadb_handle，不做磁盘访问；真正的标签数据来自
    // 下面 get_info() 读取的缓存 file_info（通常已在库扫描/播放开始时加载好）。
    metadb_handle_ptr handle = db->handle_create(path.c_str(), 0);
    if (handle.is_empty()) return false;

    file_info_impl info;
    if (!handle->get_info(info)) return false;

    static const char* kCandidateFields[] = {"LYRICS", "UNSYNCEDLYRICS", "SYNCEDLYRICS"};
    for (const char* field : kCandidateFields) {
        if (info.meta_get_count_by_name(field) == 0) continue;
        const char* value = info.meta_get(field, 0);
        if (value != nullptr && value[0] != '\0') {
            out = value;
            return true;
        }
    }
    return false;
}

}  // namespace

bool TagIOAdapter::readLyricTag(const TrackMeta& track, std::string& out) {
    if (track.path.empty()) return false;

    __block bool found = false;
    __block std::string text;
    const std::string path = track.path;

    if ([NSThread isMainThread]) {
        found = ReadLyricTagOnMainThread(path, text);
    } else {
        dispatch_sync(dispatch_get_main_queue(), ^{
            found = ReadLyricTagOnMainThread(path, text);
        });
    }

    if (!found) return false;
    out = text;
    return true;
}

bool TagIOAdapter::writeLyricTag(const TrackMeta& /*track*/, const std::string& /*lrc*/) {
    // 写回内嵌标签留到后续计划：需要 metadb_io::update_info_multi/hint 家族并处理写权限、
    // 各格式标签写入差异，超出本任务（展示闭环）范围。
    return false;
}

}  // namespace openlyrics
