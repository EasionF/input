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
    std::wstring pg  = GuidToString(GUID_NetRoomProfile);
    const HKEY root = HKEY_CURRENT_USER;
    std::wstring tip = std::wstring(L"Software\\Microsoft\\CTF\\TIP\\") + gs;
    std::wstring cl  = std::wstring(L"Software\\Classes\\CLSID\\") + gs;
    std::wstring inproc = cl + L"\\InprocServer32";

    wchar_t dllPath[MAX_PATH] = {0};
    HMODULE hmod = nullptr;
    ::GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                             GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                         reinterpret_cast<LPCWSTR>(&DllRegisterServer), &hmod);
    ::GetModuleFileNameW(hmod, dllPath, MAX_PATH);

    WriteStr(root, cl, nullptr, L"netroom input method");
    WriteStr(root, inproc, nullptr, dllPath);
    WriteStr(root, inproc, L"ThreadingModel", L"Both");

    WriteStr(root, tip, nullptr, L"netroom input method");
    // TIP 级启用标记
    WriteDword(root, tip + L"\\Enable", L"", 1);
    // 语言配置文件：TSF 依据它把文本服务列为一种“输入法”。
    // 值 {0x00000000} 为显示名（可按 len-res 形式，这里直接用字符串）。
    std::wstring langProfile = tip + L"\\LanguageProfile\\00000409\\" + pg;
    WriteStr(root, langProfile, nullptr, L"");
    WriteStr(root, langProfile, L"{0x00000000}", L"netroom input method");
    std::wstring iconFile = std::wstring(dllPath) + L",0";
    WriteStr(root, langProfile, L"{0x00000001}", iconFile.c_str());
    // 键盘布局入口（US 0409）
    WriteDword(root, tip + L"\\KeyboardLayout\\00000409", L"", 0xE0200804);

    // 官方 API 注册 + 使能：最可靠地让 OS 枚举出该输入法
    ::CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    ITfInputProcessorProfiles* pProfiles = nullptr;
    if (SUCCEEDED(::CoCreateInstance(CLSID_TF_InputProcessorProfiles, nullptr, CLSCTX_INPROC_SERVER,
                                     IID_ITfInputProcessorProfiles, (void**)&pProfiles))) {
        const wchar_t kDesc[] = L"netroom input method";
        pProfiles->AddLanguageProfile(CLSID_NetRoomTextService, 0x0409,
                                      GUID_NetRoomProfile, kDesc,
                                      static_cast<ULONG>(wcslen(kDesc)),
                                      dllPath, static_cast<ULONG>(wcslen(dllPath)), 0);
        pProfiles->EnableLanguageProfile(CLSID_NetRoomTextService, 0x0409,
                                         GUID_NetRoomProfile, TRUE);
        pProfiles->Release();
    }
    ::CoUninitialize();
    return S_OK;
}

STDMETHODIMP DllUnregisterServer() {
    std::wstring gs  = GuidToString(CLSID_NetRoomTextService);
    std::wstring pg  = GuidToString(GUID_NetRoomProfile);
    const HKEY root = HKEY_CURRENT_USER;
    ClearTree(root, std::wstring(L"Software\\Microsoft\\CTF\\TIP\\") + gs);
    ClearTree(root, std::wstring(L"Software\\Classes\\CLSID\\") + gs);
    // 官方 API 注销
    ::CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    ITfInputProcessorProfiles* pProfiles = nullptr;
    if (SUCCEEDED(::CoCreateInstance(CLSID_TF_InputProcessorProfiles, nullptr, CLSCTX_INPROC_SERVER,
                                     IID_ITfInputProcessorProfiles, (void**)&pProfiles))) {
        pProfiles->RemoveLanguageProfile(CLSID_NetRoomTextService, 0x0409, GUID_NetRoomProfile);
        pProfiles->Release();
    }
    ::CoUninitialize();
    return S_OK;
}

}  // extern "C"
