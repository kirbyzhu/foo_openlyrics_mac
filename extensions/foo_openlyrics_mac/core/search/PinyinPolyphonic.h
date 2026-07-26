#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace openlyrics {
// 命中常用多音字表返回其读音列表（小写、去声调），否则返回 nullptr。
const std::vector<std::string>* polyphonicReadings(char32_t cp);
}  // namespace openlyrics
