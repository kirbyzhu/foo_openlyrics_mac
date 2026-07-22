// fb2k_sdk 预编译头（精简版，参照 SDK-2025-03-07/foobar2000/foo_sample/stdafx.h）
// 仅引入 SDK 伞头与 mac 平台头，不含 Windows ATL 变体（foobar2000+atl.h 依赖 ATL，Windows-only）。
#pragma once

#ifdef __cplusplus
#include <SDK/foobar2000.h>
#endif

#ifdef __OBJC__
#include <Cocoa/Cocoa.h>
#endif
