#import "stdafx.h"
#import <Cocoa/Cocoa.h>
#import "PlaylistSearchController.h"

namespace {

// {C8B4A7E1-3F2D-4A6B-9C1E-7D5F0A2B8E44}
static const GUID g_search_locate_cmd_guid =
    { 0xc8b4a7e1, 0x3f2d, 0x4a6b, { 0x9c, 0x1e, 0x7d, 0x5f, 0x0a, 0x2b, 0x8e, 0x44 } };

class PlaylistSearchContextMenu : public contextmenu_item_simple {
public:
    unsigned get_num_items() override { return 1; }

    void get_item_name(unsigned, pfc::string_base &out) override {
        out = "搜索定位歌曲  (⌘F)";
    }

    void context_command(unsigned, metadb_handle_list_cref, const GUID &) override {
        dispatch_async(dispatch_get_main_queue(), ^{
            [[PlaylistSearchController shared] showOrFocus];
        });
    }

    GUID get_item_guid(unsigned) override { return g_search_locate_cmd_guid; }

    bool get_item_description(unsigned, pfc::string_base &out) override {
        out = "在当前播放列表中模糊搜索并定位歌曲";
        return true;
    }
};

FB2K_SERVICE_FACTORY(PlaylistSearchContextMenu);

}  // namespace
