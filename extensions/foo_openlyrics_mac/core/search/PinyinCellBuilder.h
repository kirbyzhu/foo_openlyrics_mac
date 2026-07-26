#pragma once
#include <functional>
#include <string>
#include <vector>

#include "search/PlaylistSearchMatcher.h"

namespace openlyrics {

// 输入一个 Hanzi 码点，返回其全拼读音列表（小写）。返回空表示无法罗马化，构建时跳过该字。
using ReadingLookup = std::function<std::vector<std::string>(char32_t)>;

// 把 UTF-8 文本构建成 SearchField：
// - ASCII 字母/数字：单字符 cell（小写）。
// - 汉字（CJK 统一表意）：用 lookup 取读音建 cell。
// - 其余（标点/空白/符号）：跳过。
SearchField buildSearchField(const std::string& utf8, const ReadingLookup& lookup);

}  // namespace openlyrics
