// component_entry.mm
// foo_openlyrics_mac —— 组件版本声明 + ui_element_mac 实现与注册；
// Plan 6 Task 2：注册偏好设置页服务。
#import "stdafx.h"
#import "LyricPanelController.h"
#import "PreferencesViewController.h"
#import <fooPreferencesCommon.h>

#include <cstring>

// 组件版本信息，每个组件 DLL/bundle 只应有一份。
DECLARE_COMPONENT_VERSION("OpenLyrics", "0.7.0", "macOS lyrics panel");

namespace {

class ui_element_openlyrics : public ui_element_mac {
public:
    service_ptr instantiate(service_ptr arg) override {
        (void)arg;
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
        // DE2A6FEE-0C69-4F92-A87D-4FFDF1EABEDE
        return GUID{ 0xde2a6fee, 0x0c69, 0x4f92,
                     { 0xa8, 0x7d, 0x4f, 0xfd, 0xf1, 0xea, 0xbe, 0xde } };
    }
};

FB2K_SERVICE_FACTORY(ui_element_openlyrics);

// Plan 6 Task 2：偏好设置页
class openlyrics_preferences_page : public preferences_mac_common<PreferencesViewController> {
public:
    const char *get_name() override { return "OpenLyrics"; }
    GUID get_guid() override {
        return g_guid_openlyrics_preferences;
    }
    GUID get_parent_guid() override {
        return preferences_page::guid_display;
    }
};

preferences_page_factory_t<openlyrics_preferences_page> g_preferences_factory;

} // namespace
