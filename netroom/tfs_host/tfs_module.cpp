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

static void RegisterTipAt(HKEY root, const std::wstring& gs, const std::wstring& pg,
                          const wchar_t* dllPath) {
    std::wstring tip = std::wstring(L"Software\\Microsoft\\CTF\\TIP\\") + gs;
    std::wstring cl  = std::wstring(L"Software\\Classes\\CLSID\\") + gs;
    std::wstring inproc = cl + L"\\InprocServer32";
    WriteStr(root, cl, nullptr, L"netroom input method");
    WriteStr(root, inproc, nullptr, dllPath);
    WriteStr(root, inproc, L"ThreadingModel", L"Both");
    WriteStr(root, tip, nullptr, L"netroom input method");
    WriteStr(root, tip + L"\\InprocServer32", nullptr, dllPath);   // 兼容旧式 TIP
    WriteDword(root, tip + L"\\Enable", L"", 1);
    // TSF 类别：标记为文本输入处理器，OS 才会在“添加键盘”里列出 netroom
    WriteStr(root, tip + L"\\Category\\Category\\{6302DE22-A5CF-4B02-BFE8-4D72B2BED3C6}\\" + gs, nullptr, L"");
    WriteStr(root, tip + L"\\Category\\Item\\" + gs, nullptr, L"netroom input method");
    WriteStr(root, tip + L"\\Category\\Item\\" + gs +
                 L"\\{6302DE22-A5CF-4B02-BFE8-4D72B2BED3C6}", nullptr, L"");
    const LANGID kLangs[] = { 0x0409, 0x0804 };   // en-US / zh-CN
    for (LANGID lang : kLangs) {
        wchar_t langKey[64];
        wsprintfW(langKey, L"\\LanguageProfile\\0x%08X\\", static_cast<unsigned>(lang));
        std::wstring lp = tip + langKey + pg;
        WriteStr(root, lp, L"Description", L"netroom input method");
        WriteStr(root, lp, L"Display Description", L"netroom input method");
        WriteDword(root, lp, L"Enable", 1);
        WriteStr(root, lp, L"IconFile", dllPath);
        WriteDword(root, lp, L"IconIndex", 0);
    }
    WriteDword(root, tip + L"\\KeyboardLayout\\00000409", L"", 0xE0200804);
    WriteDword(root, tip + L"\\KeyboardLayout\\00000804", L"", 0xE0200804);
}

STDMETHODIMP DllRegisterServer() {
    wchar_t dllPath[MAX_PATH] = {0};
    HMODULE hmod = nullptr;
    ::GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                             GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                         reinterpret_cast<LPCWSTR>(&DllRegisterServer), &hmod);
    ::GetModuleFileNameW(hmod, dllPath, MAX_PATH);
    std::wstring gs = GuidToString(CLSID_NetRoomTextService);
    std::wstring pg = GuidToString(GUID_NetRoomProfile);
    // 全用户(HKLM) 与当前用户(HKCU) 都注册；HKLM 需管理员权限运行 regsvr32。
    RegisterTipAt(HKEY_LOCAL_MACHINE, gs, pg, dllPath);
    RegisterTipAt(HKEY_CURRENT_USER, gs, pg, dllPath);

    // 官方 API：让 OS 真正把该文本服务作为输入法启用
    ::CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    ITfInputProcessorProfiles* pProfiles = nullptr;
    if (SUCCEEDED(::CoCreateInstance(CLSID_TF_InputProcessorProfiles, nullptr, CLSCTX_INPROC_SERVER,
                                     IID_ITfInputProcessorProfiles, (void**)&pProfiles))) {
        const wchar_t kDesc[] = L"netroom input method";
        const LANGID kLangs[] = { 0x0409, 0x0804 };
        for (LANGID lang : kLangs) {
            pProfiles->AddLanguageProfile(CLSID_NetRoomTextService, lang,
                                          GUID_NetRoomProfile, kDesc,
                                          static_cast<ULONG>(wcslen(kDesc)),
                                          dllPath, static_cast<ULONG>(wcslen(dllPath)), 0);
            pProfiles->EnableLanguageProfile(CLSID_NetRoomTextService, lang,
                                             GUID_NetRoomProfile, TRUE);
        }
        pProfiles->Release();
    }
    ::CoUninitialize();
    return S_OK;
}

STDMETHODIMP DllUnregisterServer() {
    std::wstring gs = GuidToString(CLSID_NetRoomTextService);
    std::wstring pg = GuidToString(GUID_NetRoomProfile);
    const HKEY roots[] = { HKEY_LOCAL_MACHINE, HKEY_CURRENT_USER };
    for (HKEY root : roots) {
        ClearTree(root, std::wstring(L"Software\\Microsoft\\CTF\\TIP\\") + gs);
        ClearTree(root, std::wstring(L"Software\\Classes\\CLSID\\") + gs);
    }
    ::CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    ITfInputProcessorProfiles* pProfiles = nullptr;
    if (SUCCEEDED(::CoCreateInstance(CLSID_TF_InputProcessorProfiles, nullptr, CLSCTX_INPROC_SERVER,
                                     IID_ITfInputProcessorProfiles, (void**)&pProfiles))) {
        const LANGID kLangs[] = { 0x0409, 0x0804 };
        for (LANGID lang : kLangs) {
            pProfiles->RemoveLanguageProfile(CLSID_NetRoomTextService, lang, GUID_NetRoomProfile);
        }
        pProfiles->Release();
    }
    ::CoUninitialize();
    return S_OK;
}

}  // extern "C"