#include "ime_channel.h"
#include <stdexcept>
#include <string>

namespace netroom::ipc {

namespace {
constexpr DWORD kMaxOpenRetry = 50;
constexpr DWORD kOpenRetryGapMs = 20;

std::wstring mapNameFor(const std::wstring& n) { return L"Local\\IME_5E_" + n + L"_map"; }
std::wstring evtNameFor(const std::wstring& n) { return L"Local\\IME_5E_" + n + L"_evt"; }
}  // namespace

ImeChannel::ImeChannel(const std::wstring& name, const std::wstring& evt,
                       RingBuffer::Role role, bool isCreator)
    : mapHandle_(nullptr), eventHandle_(nullptr) {
    const std::size_t regionBytes = RingBuffer::RequiredRegionBytes();
    const DWORD hi = static_cast<DWORD>((regionBytes >> 32) & 0xFFFFFFFFu);
    const DWORD lo = static_cast<DWORD>(regionBytes & 0xFFFFFFFFu);

    if (isCreator) {
        SECURITY_ATTRIBUTES sa{};
        sa.nLength = sizeof(SECURITY_ATTRIBUTES);
        sa.bInheritHandle = FALSE;
        // 空 SECURITY_DESCRIPTOR -> 默认 ACL（可控于单用户本地会话，足够自用）
        mapHandle_ = CreateFileMappingW(INVALID_HANDLE_VALUE, &sa, PAGE_READWRITE,
                                        hi, lo, mapNameFor(name).c_str());
        if (!mapHandle_ && GetLastError() == ERROR_ALREADY_EXISTS)
            throw std::runtime_error("channel map already exists while creating");
        if (!mapHandle_) throw std::runtime_error("CreateFileMappingW failed");
        eventHandle_ = CreateEventW(&sa, FALSE, FALSE, evtNameFor(evt).c_str());
        if (!eventHandle_) throw std::runtime_error("CreateEventW failed");
    } else {
        for (DWORD i = 0; i < kMaxOpenRetry; ++i) {
            mapHandle_ = OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, mapNameFor(name).c_str());
            if (mapHandle_) break;
            Sleep(kOpenRetryGapMs);
        }
        if (!mapHandle_) throw std::runtime_error("OpenFileMappingW timed out");
        // 事件同理重试
        for (DWORD i = 0; i < kMaxOpenRetry && !eventHandle_; ++i) {
            eventHandle_ = OpenEventW(EVENT_ALL_ACCESS, FALSE, evtNameFor(evt).c_str());
            if (!eventHandle_) Sleep(kOpenRetryGapMs);
        }
        if (!eventHandle_) throw std::runtime_error("OpenEventW timed out");
    }

    base_ = MapViewOfFile(mapHandle_, FILE_MAP_ALL_ACCESS, 0, 0, regionBytes);
    if (!base_) throw std::runtime_error("MapViewOfFile failed");
    ring_ = std::make_unique<RingBuffer>(base_, regionBytes, role, isCreator);
}

ImeChannel::~ImeChannel() {
    ring_.reset();
    if (base_) UnmapViewOfFile(base_);
    if (eventHandle_) CloseHandle(eventHandle_);
    if (mapHandle_) CloseHandle(mapHandle_);
}

void ImeChannel::Notify() { if (eventHandle_) SetEvent(eventHandle_); }

DWORD ImeChannel::WaitForData(DWORD timeoutMs) {
    if (!eventHandle_) return WAIT_FAILED;
    return WaitForSingleObject(eventHandle_, timeoutMs);
}

}  // namespace netroom::ipc