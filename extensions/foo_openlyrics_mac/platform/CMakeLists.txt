# fb2k_sdk：把 foobar2000 macOS SDK（pfc + foobar2000/SDK + foobar2000/helpers +
# foobar2000/helpers-mac + component_client）编译为可链接静态库，供后续组件目标使用。
# 构建探针性质：源清单先用各库 .xcodeproj 的 "in Sources" 名单核对（见任务简报 Step 1），
# 个别未出现在 mac Sources 名单中的 Windows-only 源显式排除，理由见下方注释。

set(FB2K_SDK_ROOT ${CMAKE_SOURCE_DIR}/SDK-2025-03-07)

# --- pfc ---
file(GLOB FB2K_PFC_CPP CONFIGURE_DEPENDS ${FB2K_SDK_ROOT}/pfc/*.cpp)
file(GLOB FB2K_PFC_OBJC CONFIGURE_DEPENDS ${FB2K_SDK_ROOT}/pfc/*.mm ${FB2K_SDK_ROOT}/pfc/*.m)

# --- foobar2000/SDK ---
file(GLOB FB2K_SDK_CPP CONFIGURE_DEPENDS ${FB2K_SDK_ROOT}/foobar2000/SDK/*.cpp)
file(GLOB FB2K_SDK_OBJC CONFIGURE_DEPENDS ${FB2K_SDK_ROOT}/foobar2000/SDK/*.mm ${FB2K_SDK_ROOT}/foobar2000/SDK/*.m)

# --- foobar2000/helpers ---
file(GLOB FB2K_HELPERS_CPP CONFIGURE_DEPENDS ${FB2K_SDK_ROOT}/foobar2000/helpers/*.cpp)

# 排除：不在 foobar2000_SDK_helpers.xcodeproj 的 mac "in Sources" 名单内的 Windows/ATL/WTL
# 专属源（对话框、Dark Mode、原地编辑等 Win32 GUI 助手，非 _WIN32 守卫，mac 下无对应实现）。
list(REMOVE_ITEM FB2K_HELPERS_CPP
  ${FB2K_SDK_ROOT}/foobar2000/helpers/AutoComplete.cpp
  ${FB2K_SDK_ROOT}/foobar2000/helpers/CTableEditHelper-Legacy.cpp
  ${FB2K_SDK_ROOT}/foobar2000/helpers/DarkMode.cpp
  ${FB2K_SDK_ROOT}/foobar2000/helpers/image_load_save.cpp
  ${FB2K_SDK_ROOT}/foobar2000/helpers/inplace_edit.cpp
  ${FB2K_SDK_ROOT}/foobar2000/helpers/ui_element_helpers.cpp
  ${FB2K_SDK_ROOT}/foobar2000/helpers/win32_dialog.cpp
  ${FB2K_SDK_ROOT}/foobar2000/helpers/window_placement_helper.cpp
  ${FB2K_SDK_ROOT}/foobar2000/helpers/WindowPositionUtils.cpp
)

# --- foobar2000/helpers-mac（无独立 xcodeproj，.mm/.m 全量编入）---
file(GLOB FB2K_HELPERS_MAC_OBJC CONFIGURE_DEPENDS ${FB2K_SDK_ROOT}/foobar2000/helpers-mac/*.mm ${FB2K_SDK_ROOT}/foobar2000/helpers-mac/*.m)

# --- foobar2000_component_client（必编，供组件导出表使用）---
set(FB2K_COMPONENT_CLIENT_CPP ${FB2K_SDK_ROOT}/foobar2000/foobar2000_component_client/component_client.cpp)

# --- foobar2000/shared（Plan 2 Task 2 新增：uBugCheck/GetInfiniteWaitEvent/uPrintfV/
# fb2k::crashWithMessage 等符号的实现，链接真正的可加载 bundle 时才会触发这些未定义符号；
# Task 1 的探针范围未纳入。按 shared.xcodeproj 的 mac target "in Sources" 名单核对，
# 只编入以下 5 个文件，其余（filedialogs*/minidump/modal_dialog/systray/text_drawing/
# Utility/utf8api/win32_misc）均为 Windows 专属，不在该名单内。）---
set(FB2K_SHARED_ROOT ${FB2K_SDK_ROOT}/foobar2000/shared)
set(FB2K_SHARED_CPP
  ${FB2K_SHARED_ROOT}/stdafx.cpp
  ${FB2K_SHARED_ROOT}/utf8.cpp
  ${FB2K_SHARED_ROOT}/shared-nix.cpp
  ${FB2K_SHARED_ROOT}/audio_math.cpp
)
set(FB2K_SHARED_OBJC ${FB2K_SHARED_ROOT}/shared-apple.mm)

set(FB2K_SDK_OBJC_SRC ${FB2K_PFC_OBJC} ${FB2K_SDK_OBJC} ${FB2K_HELPERS_MAC_OBJC} ${FB2K_SHARED_OBJC})
set(FB2K_SDK_ALL_SRC
  ${FB2K_PFC_CPP}
  ${FB2K_SDK_CPP}
  ${FB2K_HELPERS_CPP}
  ${FB2K_SHARED_CPP}
  ${FB2K_SDK_OBJC_SRC}
  ${FB2K_COMPONENT_CLIENT_CPP}
)

add_library(fb2k_sdk STATIC ${FB2K_SDK_ALL_SRC})

target_include_directories(fb2k_sdk PUBLIC
  ${FB2K_SDK_ROOT}
  ${FB2K_SDK_ROOT}/foobar2000
)
# foobar2000-mac-class-suffix.h：组件自备头（防 Obj-C 类名冲突，定义 FOOBAR2000_MAC_CLASS_SUFFIX），
# 由 foobar2000-mac-helpers.h 以引号 #include 查找；本项目组件名为 foo_openlyrics_mac，见同目录下该文件。
target_include_directories(fb2k_sdk PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

target_compile_features(fb2k_sdk PUBLIC cxx_std_17)

# Obj-C++/Obj-C 源统一开 ARC
set_source_files_properties(${FB2K_SDK_OBJC_SRC} PROPERTIES COMPILE_OPTIONS "-fobjc-arc")

target_link_libraries(fb2k_sdk PUBLIC
  "-framework AppKit"
  "-framework Foundation"
  "-framework CoreFoundation"
  "-framework Cocoa"
)
