#import <Cocoa/Cocoa.h>

@interface PlaylistSearchController : NSObject
+ (instancetype)shared;
// 弹出/聚焦搜索框。必须在主线程调用。
- (void)showOrFocus;
@end
