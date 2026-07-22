#pragma once
#include "config/AppConfig.h"

namespace openlyrics {

class ConfigAdapter {
public:
    // 从 NSUserDefaults 读取 JSON → AppConfig；无已存配置返回 defaults()。
    AppConfig load() const;
    // 序列化 AppConfig → JSON → 写入 NSUserDefaults。
    void save(const AppConfig& config) const;
};

}  // namespace openlyrics
