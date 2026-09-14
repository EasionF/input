#include <windows.h>
#include <msctf.h>
#include <initguid.h>
#include <wchar.h>
int wmain(){
    ::CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    CLSID cls; GUID prof;
    ::CLSIDFromString(L"{79DC7A13-5E1A-4C0B-BB1C-361B4413E0D5}",&cls);
    ::CLSIDFromString(L"{23A6C530-9B21-4C4E-9AC5-6B2F81407ED2}",&prof);
    ITfInputProcessorProfiles* pp=nullptr;
    HRESULT hr=::CoCreateInstance(CLSID_TF_InputProcessorProfiles,nullptr,CLSCTX_INPROC_SERVER,IID_ITfInputProcessorProfiles,(void**)&pp);
    if(FAILED(hr)){ wprintf(L"coCreate failed hr=%08x\n",hr); ::CoUninitialize(); return 1; }
    const wchar_t* desc=L"netroom input method";
    LANGID langs[]={0x0804,0x0409};
    for(auto lang:langs){
        pp->AddLanguageProfile(cls,lang,prof,desc,(ULONG)wcslen(desc),L"",0,0);
        pp->EnableLanguageProfile(cls,lang,prof,TRUE);
        pp->SetDefaultLanguageProfile(lang,cls,prof);
    }
    pp->ActivateLanguageProfile(cls,0x0804,prof);
    pp->Release();
    wprintf(L"netroom: registered+enabled+default+activated\n");
    ::CoUninitialize();
    return 0;
}