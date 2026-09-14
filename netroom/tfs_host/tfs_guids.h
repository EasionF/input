#pragma once
#define WIN32_LEAN_AND_MEAN
#include <guiddef.h>
#include <ComDef.h>

// ---- netroom 输入法 TSF 文本服务标识 ----
// 一旦发布即不再改动。注册表/CMake 与代码共用同一 GUID。
// {79DC7A13-5E1A-4C0B-BB1C-361B4413E0D5}
DEFINE_GUID(CLSID_NetRoomTextService,
    0x79dc7a13, 0x5e1a, 0x4c0b, 0xbb, 0x1c, 0x36, 0x1b, 0x44, 0x13, 0xe0, 0xd5);
// ---- 输入法配置文件 GUID（必须与文本服务 CLSID 不同；供 RegisterLanguageProfile 使用）----
DEFINE_GUID(GUID_NetRoomProfile,
    0x23a6c530, 0x9b21, 0x4c4e, 0x9a, 0xc5, 0x6b, 0x2f, 0x81, 0x40, 0x7e, 0xd2);