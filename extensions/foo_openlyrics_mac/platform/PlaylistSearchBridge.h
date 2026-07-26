#import <Foundation/Foundation.h>

#include <vector>
#include "search/PlaylistSearchMatcher.h"

@interface PlaylistSnapshot : NSObject
- (const std::vector<openlyrics::SearchRecord> &)records;
- (NSInteger)count;
// 供结果列表显示：返回 "标题 — 艺术家"。
- (NSString *)displayAt:(NSInteger)index;
// 在活动播放列表中聚焦+单选+滚动可见；成功返回 YES。
- (BOOL)locateIndex:(NSInteger)index;
@end

@interface PlaylistSearchBridge : NSObject
+ (PlaylistSnapshot *)snapshotActivePlaylist;
@end
