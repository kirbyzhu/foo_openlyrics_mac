// component_entry.mm
// foo_openlyrics_mac —— Plan 2 Task 2：组件版本声明 + ui_element_mac 最小实现与注册。
#import "stdafx.h"
#import "LyricPanelController.h"

#include <cstring>

// 组件版本信息，每个组件 DLL/bundle 只应有一份。
DECLARE_COMPONENT_VERSION("OpenLyrics", "0.1.0", "macOS lyrics panel");

namespace {

// 最小可加载占位面板：instantiate 返回包装后的 LyricPanelController，
// match_name 认 "openlyrics"，get_guid 用固定硬编码 GUID（下方注释来源）。
class ui_element_openlyrics : public ui_element_mac {
public:
    service_ptr instantiate(service_ptr arg) override {
        (void)arg; // Task 2 占位阶段不消费传入的 NSDictionary 参数
        LyricPanelController *controller = [[LyricPanelController alloc] init];
        return fb2k::wrapNSObject(controller);
    }

    bool match_name(const char *name) override {
        return name != nullptr && std::strcmp(name, "openlyrics") == 0;
    }

    fb2k::stringRef get_name() override {
        return fb2k::makeString("OpenLyrics");
    }

    GUID get_guid() override {
        // 固定 GUID：由 `uuidgen` 一次性生成后硬编码，之后不得再变动
        // （DE2A6FEE-0C69-4F92-A87D-4FFDF1EABEDE）。
        return GUID{ 0xde2a6fee, 0x0c69, 0x4f92,
                     { 0xa8, 0x7d, 0x4f, 0xfd, 0xf1, 0xea, 0xbe, 0xde } };
    }
};

FB2K_SERVICE_FACTORY(ui_element_openlyrics);

} // namespace
