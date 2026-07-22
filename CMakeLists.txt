cmake_minimum_required(VERSION 3.20)
project(foo_openlyrics_core LANGUAGES CXX C OBJC OBJCXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)
FetchContent_Declare(
  googletest
  URL https://github.com/google/googletest/archive/refs/tags/v1.15.2.tar.gz
)
FetchContent_MakeAvailable(googletest)

add_library(openlyrics_core STATIC
  extensions/foo_openlyrics_mac/core/parser/LrcParser.cpp
  extensions/foo_openlyrics_mac/core/parser/LrcSerializer.cpp
  extensions/foo_openlyrics_mac/core/sync/SyncEngine.cpp
  extensions/foo_openlyrics_mac/core/sources/TagSource.cpp
  extensions/foo_openlyrics_mac/core/sources/LocalFileSource.cpp
  extensions/foo_openlyrics_mac/core/pipeline/SearchPipeline.cpp
  extensions/foo_openlyrics_mac/core/net/UrlEncode.cpp
  extensions/foo_openlyrics_mac/core/net/JsonField.cpp
  extensions/foo_openlyrics_mac/core/sources/LrcLibProvider.cpp
  extensions/foo_openlyrics_mac/core/store/LyricStore.cpp
  extensions/foo_openlyrics_mac/core/net/Base64.cpp
  extensions/foo_openlyrics_mac/core/sources/NetEaseProvider.cpp
  extensions/foo_openlyrics_mac/core/sources/QQMusicProvider.cpp
)
target_include_directories(openlyrics_core PUBLIC extensions/foo_openlyrics_mac/core)

# Plan 2 Task 1：foobar2000 macOS SDK 构建探针（fb2k_sdk 静态库），与上面的 Plan 1 核心目标相互独立。
if(EXISTS ${CMAKE_SOURCE_DIR}/SDK-2025-03-07)
  add_subdirectory(extensions/foo_openlyrics_mac/platform)

  # Plan 2 Task 2：最小可加载 ui_element_mac 面板，打包为 foo_openlyrics.component。
  # MODULE 库在 Apple 平台默认以 -bundle 方式链接（产出 MH_BUNDLE 类型 Mach-O），
  # 契合 foobar2000 组件加载器所需的可加载 bundle 二进制；PREFIX/SUFFIX 清空后
  # 产物文件名恰为 foo_openlyrics，直接放进 Contents/MacOS/ 即可，不走 CMake 自带的
  # MACOSX_BUNDLE/BUNDLE 自动打包（其 Info.plist 变量替换约定与本项目固定字面量键值
  # 不完全对应），改为手工组装 .component 目录结构，行为更可控。
  add_library(foo_openlyrics MODULE
    extensions/foo_openlyrics_mac/platform/component_entry.mm
    # Plan 2 Task 3：play_callback 实现 + PlaybackHub 单例 broker，打通 SDK→面板数据流。
    extensions/foo_openlyrics_mac/platform/PlaybackBridge.mm
    # Plan 2 Task 5：TagIO/FileSystem 端口的平台实现 + 歌词渲染 View + 面板接线，
    # 完成"曲目切换→检索歌词→随播放位置高亮滚动"的展示闭环。
    extensions/foo_openlyrics_mac/platform/TagIOAdapter.mm
    extensions/foo_openlyrics_mac/platform/FileSystemAdapter.mm
    extensions/foo_openlyrics_mac/ui/LyricView.mm
    extensions/foo_openlyrics_mac/ui/LyricPanelController.mm
    # Plan 3 Task 6：HttpClient 端口的 NSURLSession 实现，在线（LrcLib）检索+自动
    # 落盘接线用它替 SearchPipeline 之外的补充在线段（不进 SearchPipeline 本身，
    # 见 LyricPanelController.mm 顶部注释）。
    extensions/foo_openlyrics_mac/platform/HttpAdapter.mm
    # Plan 4 Task 4：CryptoPort 的 CommonCrypto/Security.framework 实现。
    extensions/foo_openlyrics_mac/platform/CryptoAdapter.mm
  )
  set_target_properties(foo_openlyrics PROPERTIES
    PREFIX ""
    SUFFIX ""
    OUTPUT_NAME "foo_openlyrics"
  )
  set_source_files_properties(
    extensions/foo_openlyrics_mac/platform/component_entry.mm
    extensions/foo_openlyrics_mac/platform/PlaybackBridge.mm
    extensions/foo_openlyrics_mac/platform/TagIOAdapter.mm
    extensions/foo_openlyrics_mac/platform/FileSystemAdapter.mm
    extensions/foo_openlyrics_mac/ui/LyricView.mm
    extensions/foo_openlyrics_mac/ui/LyricPanelController.mm
    extensions/foo_openlyrics_mac/platform/HttpAdapter.mm
    extensions/foo_openlyrics_mac/platform/CryptoAdapter.mm
    PROPERTIES COMPILE_OPTIONS "-fobjc-arc"
  )
  target_include_directories(foo_openlyrics PRIVATE
    # class-suffix.h、stdafx.h
    extensions/foo_openlyrics_mac/platform
    # LyricPanelController.h（component_entry.mm 以 #import "LyricPanelController.h" 引用）
    extensions/foo_openlyrics_mac/ui
    # foobar2000-mac-helpers.h（FB2K_OBJC_CLASS 宏），fb2k_sdk 未把该目录设为 PUBLIC include 根
    ${CMAKE_SOURCE_DIR}/SDK-2025-03-07/foobar2000/helpers-mac
  )
  target_link_libraries(foo_openlyrics PRIVATE fb2k_sdk openlyrics_core "-framework Security")

  set(FOO_OPENLYRICS_BUNDLE "${CMAKE_CURRENT_BINARY_DIR}/foo_openlyrics.component")
  configure_file(
    extensions/foo_openlyrics_mac/platform/Info.plist.in
    "${FOO_OPENLYRICS_BUNDLE}/Contents/Info.plist"
    @ONLY
  )
  add_custom_command(TARGET foo_openlyrics POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${FOO_OPENLYRICS_BUNDLE}/Contents/MacOS"
    COMMAND ${CMAKE_COMMAND} -E copy
            "$<TARGET_FILE:foo_openlyrics>"
            "${FOO_OPENLYRICS_BUNDLE}/Contents/MacOS/foo_openlyrics"
    COMMAND codesign --force --deep --sign - "${FOO_OPENLYRICS_BUNDLE}"
    COMMENT "组装 foo_openlyrics.component 并 ad-hoc 签名"
    VERBATIM
  )
endif()

enable_testing()
add_executable(core_tests
  tests/test_smoke.cpp
  tests/test_lrc_parser.cpp
  tests/test_lrc_serializer.cpp
  tests/test_sync_engine.cpp
  tests/test_ports.cpp
  tests/test_tag_source.cpp
  tests/test_local_file_source.cpp
  tests/test_search_pipeline.cpp
  tests/test_url_encode.cpp
  tests/test_json_field.cpp
  tests/test_lrclib_provider.cpp
  tests/test_lyric_store.cpp
  tests/test_netease_provider.cpp
  tests/test_qqmusic_provider.cpp
)
target_link_libraries(core_tests PRIVATE openlyrics_core GTest::gtest_main)

include(GoogleTest)
gtest_discover_tests(core_tests)
