#pragma once
#include "search/PinyinCellBuilder.h"

namespace openlyrics_platform {
// 返回一个 ReadingLookup：先查多音字表，未命中用 CFStringTransform 生成单读音（带进程内缓存）。
openlyrics::ReadingLookup makeReadingLookup();
}  // namespace openlyrics_platform
