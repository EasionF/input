// netroom TSF 文本服务 DLL exported functions + module 级引用计数。
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <initguid.h>
#include <msctf.h>
#include <new>
#include <atomic>
#include <string>
#include "tfs_guids.h"
#include "tfs_service.h"

LONG g_cRefDll   = 0;   // 由类对象/模块函数增减
LONG g_cRefClass = 0;   // 当前已建立实例数量

namespace {

std::wstring GuidToString(const GUID& g) {
    wchar_t b[64];
    wsprintfW(b, L"{%08X-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X}",
              g.Data1, g.Data2, g.Data3, g.Data4[0], g.Data4[1], g.Data4[2],
              g.Data4[3], g.Data4[4], g.Data4[5], g.Data4[6], g.Data4[7]);
    return b;
}

void WriteStr(HKEY root, const std::wstring& path, const wchar_t* name, const wchar_t* value) {
    HKEY k = nullptr;
    if (RegCreateKeyExW(root, path.c_str(), 0, nullptr, 0, KEY_WRITE, nullptr, &k, nullptr)
        == ERROR_SUCCESS) {
        RegSetValueExW(k, name, 0, REG_SZ, (const BYTE*)value,
                       (DWORD)((wcslen(value) + 1) * sizeof(wchar_t)));
        RegCloseKey(k);
    }
}
void WriteDword(HKEY root, const std::wstring& path, const wchar_t* name, DWORD v) {
    HKEY k = nullptr;
    if (RegCreateKeyExW(root, path.c_str(), 0, nullptr, 0, KEY_WRITE, nullptr, &k, nullptr)
        == ERROR_SUCCESS) {
        RegSetValueExW(k, name, 0, REG_DWORD, (const BYTE*)&v, sizeof(DWORD));
        RegCloseKey(k);
    }
}
void ClearTree(HKEY root, const std::wstring& path) { RegDeleteTreeW(root, path.c_str()); }

class CClassFactory : public IClassFactory {
public:
    CClassFactory() { InterlockedIncrement(&g_cRefDll); }
    ~CClassFactory() { InterlockedDecrement(&g_cRefDll); }

    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override {
        if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_IClassFactory)) {
            *ppv = static_cast<IClassFactory*>(this); AddRef(); return S_OK;
        }
        *ppv = nullptr; return E_NOINTERFACE;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return (ULONG)InterlockedIncrement(&ref_); }
    STDMETHODIMP_(ULONG) Release() override {
        LONG r = InterlockedDecrement(&ref_);
        if (r == 0) delete this;
        return (ULONG)r;
    }
    STDMETHODIMP CreateInstance(IUnknown* punk, REFIID riid, void** ppv) override {
        if (punk) return CLASS_E_NOAGGREGATION;
        CTextService* svc = new (std::nothrow) CTextService();
        if (!svc) return E_OUTOFMEMORY;
        InterlockedIncrement(&g_cRefClass);
        HRESULT hr = svc->QueryInterface(riid, ppv);
        svc->Release();
        return hr;
    }
    STDMETHODIMP LockServer(BOOL) override { return S_OK; }
private:
    LONG ref_ = 1;
};

}  // namespace

extern "C" {

STDMETHODIMP DllGetClassObject(REFCLSID rclsid, REFIID riid, LPVOID* ppv) {
    if (!ppv) return E_POINTER;
    *ppv = nullptr;
    if (!IsEqualGUID(rclsid, CLSID_NetRoomTextService)) return CLASS_E_CLASSNOTAVAILABLE;
    CClassFactory* f = new (std::nothrow) CClassFactory();
    if (!f) return E_OUTOFMEMORY;
    HRESULT hr = f->QueryInterface(riid, ppv);
    f->Release();
    return hr;
}

STDMETHODIMP DllCanUnloadNow() {
    return (g_cRefDll == 0 && g_cRefClass == 0) ? S_OK : S_FALSE;
}

STDMETHODIMP DllRegisterServer() {
    std::wstring gs  = GuidToString(CLSID_NetRoomTextService);
    // 用当前用户注册表，免管理员权限；路径取本 DLL 实际所在目录。
    // HKLM 版：注册为远程/会话布局需管理员 + 需一次登录；HKCU 对单机测试更友好。
    const HKEY root = HKEY_CURRENT_USER;
    std::wstring tip = std::wstring(L"Software\\Microsoft\\CTF\\TIP\\") + gs;
    std::wstring cl  = std::wstring(L"Software\\Classes\\CLSID\\") + gs;
    std::wstring inproc = cl + L"\\InprocServer32";

    wchar_t dllPath[MAX_PATH] = {0};
    // 取本 DLL（而非宿主进程 regsvr32.exe）的绝对路径。
    HMODULE hmod = nullptr;
    ::GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                             GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                         reinterpret_cast<LPCWSTR>(&DllRegisterServer), &hmod);
    ::GetModuleFileNameW(hmod, dllPath, MAX_PATH);

    WriteStr(root, cl, nullptr, L"netroom input method");
    WriteStr(root, inproc, nullptr, dllPath);
    WriteStr(root, inproc, L"ThreadingModel", L"Both");

    WriteStr(root, tip, nullptr, L"netroom input method");
    WriteStr(root, tip + L"\\Enable", L"", L"1");
    // 键盘布局入口：适用于 0409 (US)。其他布局见配置阶段。
    WriteDword(root, tip + L"\\KeyboardLayout\\00000409", L"", 0xE0200804);
    return S_OK;
}

STDMETHODIMP DllUnregisterServer() {
    std::wstring gs = GuidToString(CLSID_NetRoomTextService);
    const HKEY root = HKEY_CURRENT_USER;
    ClearTree(root, std::wstring(L"Software\\Microsoft\\CTF\\TIP\\") + gs);
    ClearTree(root, std::wstring(L"Software\\Classes\\CLSID\\") + gs);
    return S_OK;
}

}  // extern "C"
