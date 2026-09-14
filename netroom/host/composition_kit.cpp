#include "composition_kit.h"
#if defined(_WIN32)
#include <windows.h>
#endif

namespace netroom::host {

HostAction Apply(const netroom::ipc::CandidateFrame& f) {
    HostAction a;
    switch (f.status) {
        case 0:   // 组合
            a.kind = HostActionKind::UpdateComposition;
            a.utf8Text = f.composition;
            a.caret = f.caret;
            break;
        case 1:   // 提交（composition 字段承载提交文本）
            a.kind = HostActionKind::Commit;
            a.utf8Text = f.composition;
            break;
        case 2:   // 放弃
            a.kind = HostActionKind::ClearComposition;
            break;
        default:  // 3=放行，其余一律保守放行
            a.kind = HostActionKind::Passthrough;
            break;
    }
    return a;
}

bool Utf8ToWide(const std::string& utf8, std::wstring* wide) {
    if (!wide) return false;
    if (utf8.empty()) { wide->clear(); return true; }
    // MultiByteToWideChar 需要 windows.h；为无头可测，此处用标准转换失败即返回。
    // 该函数会被 TSF 侧调用；单测覆盖空与非 ASCII 少见路径。
    const char* data = utf8.data();
    const int len = static_cast<int>(utf8.size());
    // 使用 WideCharToMultiByte 的反函数（局限于 Win32；未包含 windows.h 时返回 false）
#if defined(_WIN32)
    const int n = ::MultiByteToWideChar(CP_UTF8, 0, data, len, nullptr, 0);
    if (n <= 0) return false;
    wide->resize(static_cast<std::size_t>(n));
    if (::MultiByteToWideChar(CP_UTF8, 0, data, len, &(*wide)[0], n) != n) return false;
    return true;
#else
    (void)data; (void)len;
    return false;
#endif
}

}  // namespace netroom::host