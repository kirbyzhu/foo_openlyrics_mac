#import "PlaylistSearchBridge.h"
#import "stdafx.h"
#import "PinyinBuilder.h"

#include <string>
#include <vector>

#include "search/PinyinCellBuilder.h"

using openlyrics::SearchRecord;

@implementation PlaylistSnapshot {
    std::vector<SearchRecord> _records;
    std::vector<metadb_handle_ptr> _handles;
    NSMutableArray<NSString *> *_displays;
}

- (instancetype)initWithRecords:(std::vector<SearchRecord> &&)records
                        handles:(std::vector<metadb_handle_ptr> &&)handles
                       displays:(NSMutableArray<NSString *> *)displays {
    if ((self = [super init])) {
        _records = std::move(records);
        _handles = std::move(handles);
        _displays = displays;
    }
    return self;
}

- (const std::vector<SearchRecord> &)records { return _records; }
- (NSInteger)count { return (NSInteger)_records.size(); }

- (NSString *)displayAt:(NSInteger)index {
    if (index < 0 || index >= (NSInteger)_displays.count) return @"";
    return _displays[index];
}

- (BOOL)locateIndex:(NSInteger)index {
    if (index < 0 || index >= (NSInteger)_handles.size()) return NO;
    auto pm = playlist_manager::get();
    if (pm.is_empty()) return NO;
    metadb_handle_ptr h = _handles[index];
    if (h.is_empty()) return NO;
    t_size pos = pm->activeplaylist_set_focus_by_handle(h);
    if (pos == pfc_infinite) return NO;
    // 清除其它选中，仅选中 pos：affected=全部，status 仅 pos 为真。
    bit_array_true affected;
    bit_array_one status(pos);
    pm->activeplaylist_set_selection(affected, status);
    pm->activeplaylist_ensure_visible(pos);
    return YES;
}

@end

@implementation PlaylistSearchBridge

+ (PlaylistSnapshot *)snapshotActivePlaylist {
    std::vector<SearchRecord> records;
    std::vector<metadb_handle_ptr> handles;
    NSMutableArray<NSString *> *displays = [NSMutableArray array];

    auto pm = playlist_manager::get();
    if (pm.is_empty()) {
        return [[PlaylistSnapshot alloc] initWithRecords:std::move(records)
                                                 handles:std::move(handles)
                                                displays:displays];
    }
    t_size count = pm->activeplaylist_get_item_count();
    auto lookup = openlyrics_platform::makeReadingLookup();

    for (t_size i = 0; i < count; ++i) {
        metadb_handle_ptr h;
        if (!pm->activeplaylist_get_item_handle(h, i) || h.is_empty()) continue;

        std::string title, artist, album;
        metadb_info_container::ptr infoRef;
        if (h->get_info_ref(infoRef)) {
            const file_info &info = infoRef->info();
            const char *t = info.meta_get_title(nullptr);
            if (t) title = t;
            if (info.meta_get_count_by_name("artist") > 0) {
                const char *a = info.meta_get("artist", 0);
                if (a) artist = a;
            }
            if (info.meta_get_count_by_name("album") > 0) {
                const char *a = info.meta_get("album", 0);
                if (a) album = a;
            }
        }
        if (title.empty()) {
            // 用文件名兜底标题
            std::string path = h->get_path();
            size_t slash = path.find_last_of('/');
            title = (slash == std::string::npos) ? path : path.substr(slash + 1);
        }

        SearchRecord rec;
        rec.title = openlyrics::buildSearchField(title, lookup);
        rec.artist = openlyrics::buildSearchField(artist, lookup);
        rec.album = openlyrics::buildSearchField(album, lookup);
        records.push_back(std::move(rec));
        handles.push_back(h);

        NSString *tt = [NSString stringWithUTF8String:title.c_str()] ?: @"";
        NSString *aa = [NSString stringWithUTF8String:artist.c_str()] ?: @"";
        NSString *disp = aa.length ? [NSString stringWithFormat:@"%@ — %@", tt, aa] : tt;
        [displays addObject:disp];
    }

    return [[PlaylistSnapshot alloc] initWithRecords:std::move(records)
                                             handles:std::move(handles)
                                            displays:displays];
}

@end
