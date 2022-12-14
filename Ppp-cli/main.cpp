#include <string>
#include <cstring>
#include <iostream>
#include <metahost.h>
#include <Windows.h>

#include "rc4.h"
#include "resource.h"

// --confusion=C:\Ppp\publish\PPP.A

#pragma comment(lib, "MSCorEE.lib")

#include "mscorlib.tlh"
/*
// Import mscorlib.tlb (Microsoft Common Language Runtime Class Library).
#import "mscorlib.tlb" raw_interfaces_only				\
    high_property_prefixes("_get","_put","_putref")		\
    rename("ReportEvent", "InteropServices_ReportEvent")
*/
    using namespace mscorlib;
#pragma endregion

class FunctionHook
{
public:
    unsigned char           a[5];
    unsigned char           b[5];
    void*                   c;
    CRITICAL_SECTION        cs;

public:
    inline FunctionHook() : c(0), cs{}
    {
        InitializeCriticalSection(&cs);
    }
    inline ~FunctionHook()
    {
        DeleteCriticalSection(&cs);
    }

public:
    inline void             Enter()
    {
        EnterCriticalSection(&cs);
    }
    inline void             Exit()
    {
        LeaveCriticalSection(&cs);
    }

public:
    inline bool             Protect(void* p)
    {
        DWORD dw;
        return VirtualProtect(p, 5, PAGE_EXECUTE_READWRITE, &dw) ? 1 : 0;
    }
    inline void             Install(void* sources, void* destination)
    {
        Protect(sources);
        memcpy(a, sources, 5);
        b[0] = 0xE9;
        *(char**)&b[1] = (char*)((char*)destination - ((char*)sources + 5));
        c = sources;
        Resume();
    }
    inline void             Suspend()
    {
        memcpy(c, a, 5);
    }
    inline void             Resume()
    {
        memcpy(c, b, 5);
    }
};

static std::string g_szAppDomainPath;
static FunctionHook g_fSetConsoleTitleW;

static std::string 
W2A(DWORD codepage, const WCHAR* s, int len)
{
    if (s == NULL || (len != -1 && len < 1))
    {
        return NULL;
    }
    if (len == -1)
    {
        len = WideCharToMultiByte(codepage, 0, s, -1, NULL, 0, 0, 0);
    }
    if (len < 1)
    {
        return NULL;
    }
    std::string result = "";
    CHAR* contents = (CHAR*)malloc(len);
    if (contents)
    {
        WideCharToMultiByte(codepage, 0, s, -1, contents, len, 0, 0);
        result = contents;
        free(contents);
    }
    return result;
}

static std::string 
carg(const char* name, int argc, const char* argv[])
{
    if (argc <= 1)
        return "";
    for (int i = 1; i < argc; i++) {
        char* p = (char*)strstr(argv[i], name);
        if (!p)
            continue;
        p = strchr(p, '=');
        if (!p)
            continue;
        return 1 + p;
    }
    return "";
}

static HGLOBAL
GetResourceHandle(DWORD dwResourceId, DWORD& rdwResourceSize)
{
    rdwResourceSize = 0;

    HINSTANCE hModule = GetModuleHandleA(NULL);
    // "ZIP" 是自定义资源类型，可以自己决定
    HRSRC hSrc = FindResourceA(hModule, MAKEINTRESOURCEA(dwResourceId), "PPP");
    if (hSrc == NULL)
    {
        return NULL;
    }

    HGLOBAL hGlobalResource = LoadResource(hModule, hSrc);
    if (hGlobalResource == NULL)
    {
        return NULL;
    }

    rdwResourceSize = SizeofResource(hModule, hSrc);
    return hGlobalResource;
}

static void 
ConfusionAssemblyStream(const void* resourceData, int resourceSize)
{
    unsigned char key[8];
    key[0] = 'y';
    key[1] = 'y';
    key[2] = '5';
    key[3] = '2';
    key[4] = '3';
    key[5] = 'o';
    key[6] = '!';
    key[7] = '!';
    rc4_crypt(key, 8, (unsigned char*)resourceData, resourceSize, 0, 0);
}

static int
DecodeResourceAssembly(const void* pResourceData, DWORD dwResourceSize)
{
    ConfusionAssemblyStream(pResourceData, (int)dwResourceSize);
    return 0;
}

static int 
ConfusionAssemblyFile(const char* pResourceFile)
{
    int error = 0;
    HANDLE hFile = CreateFileA(
        pResourceFile,   //文件名
        GENERIC_READ | GENERIC_WRITE, //对文件进行读写操作
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        NULL,
        OPEN_EXISTING,  //打开已存在文件
        FILE_ATTRIBUTE_NORMAL,
        0);
    if (NULL == hFile || INVALID_HANDLE_VALUE == hFile)
    {
        error = -1;
        return error;
    }

    HANDLE hFileMapping = NULL;
    void* pFileView = NULL;
    do
    {
        DWORD dwLow;
        DWORD dwHigh;
        dwLow = GetFileSize(hFile, &dwHigh);
        if (0 == dwLow)
        {
            error = -2;
            break;
        }

        hFileMapping = CreateFileMapping(
            hFile,
            NULL,
            PAGE_READWRITE,  //对映射文件进行读写
            dwHigh,
            dwLow,   //这两个参数共64位，所以支持的最大文件长度为16EB
            NULL);
        if (NULL == hFile || INVALID_HANDLE_VALUE == hFile)
        {
            hFileMapping = NULL;
            error = -3;
            break;
        }

        pFileView = MapViewOfFile(
            hFileMapping,
            FILE_MAP_READ | FILE_MAP_WRITE,
            0,
            0,
            0);
        if (NULL == pFileView)
        {
            error = -4;
            break;
        }

        ConfusionAssemblyStream(pFileView, (int)dwLow);
    } while (0, 0);
    if (pFileView)
    {
        UnmapViewOfFile(pFileView);
    }
    if (hFileMapping)
    {
        CloseHandle(hFileMapping);
    }
    if (hFile)
    {
        CloseHandle(hFile);
    }
    return error;
}

static std::wstring
ExtractResource(DWORD dwResourceId)
{
    DWORD RAW_ASSEMBLY_LENGTH = 0;
    HGLOBAL hGlobalResource = GetResourceHandle(dwResourceId, RAW_ASSEMBLY_LENGTH);

    const void* pResourceData = ::LockResource(hGlobalResource);
    if (!pResourceData)
    {
        FreeResource(hGlobalResource);
        return L"";
    }

    WCHAR wzSystemTempPath[MAX_PATH];
    DWORD dwPathSize = GetTempPathW(MAX_PATH, wzSystemTempPath);
    if (dwPathSize > MAX_PATH || (dwPathSize == 0))
    {
        UnlockResource(pResourceData);
        FreeResource(hGlobalResource);
        return L"";
    }

    WCHAR wzRandomTempFileName[MAX_PATH];
    GetTempFileNameW(wzSystemTempPath, NULL, 0, wzRandomTempFileName);

    HANDLE hFile = CreateFileW(wzRandomTempFileName,
        GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE, 
        NULL, CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        NULL);
    if (hFile == INVALID_HANDLE_VALUE)
    {
        wzRandomTempFileName[0] = L'\x0';
    }
    else
    {
        void* pszLib = malloc(RAW_ASSEMBLY_LENGTH);
        memcpy(pszLib, pResourceData, RAW_ASSEMBLY_LENGTH);
        DecodeResourceAssembly(pszLib, RAW_ASSEMBLY_LENGTH);

        DWORD dwNumberOfBytesWritten;
        if (!WriteFile(hFile, pszLib, RAW_ASSEMBLY_LENGTH, &dwNumberOfBytesWritten, NULL) ||
            dwNumberOfBytesWritten != RAW_ASSEMBLY_LENGTH)
        {
            wzRandomTempFileName[0] = L'\x0';
        }

        free(pszLib);
        CloseHandle(hFile);
    }

    UnlockResource(pResourceData);
    FreeResource(hGlobalResource);

    return wzRandomTempFileName;
}

static int
ExecuteEntryPoint(LPCWSTR pwzVersion, DWORD dwResourceId)
{
    ICLRMetaHost* pMetaHost = NULL;
    HRESULT hr;

    /* Get ICLRMetaHost instance */

    hr = CLRCreateInstance(CLSID_CLRMetaHost, IID_ICLRMetaHost, (VOID**)&pMetaHost);
    if (FAILED(hr))
    {
        OutputDebugStringA("[!] CLRCreateInstance(...) failed\n");
        return -1;
    }

    OutputDebugStringA("[+] CLRCreateInstance(...) succeeded\n");
    ICLRRuntimeInfo* pRuntimeInfo = NULL;

    /* Get ICLRRuntimeInfo instance */

    hr = pMetaHost->GetRuntime(pwzVersion, IID_ICLRRuntimeInfo, (VOID**)&pRuntimeInfo);
    if (FAILED(hr))
    {
        pMetaHost->Release();
        OutputDebugStringA("[!] pMetaHost->GetRuntime(...) failed\n");
        return -1;
    }

    OutputDebugStringA("[+] pMetaHost->GetRuntime(...) succeeded\n");
    BOOL bLoadable;

    /* Check if the specified runtime can be loaded */

    hr = pRuntimeInfo->IsLoadable(&bLoadable);
    if (FAILED(hr) || !bLoadable)
    {
        pRuntimeInfo->Release();
        pMetaHost->Release();
        OutputDebugStringA("[!] pRuntimeInfo->IsLoadable(...) failed\n");
        return -1;
    }

    OutputDebugStringA("[+] pRuntimeInfo->IsLoadable(...) succeeded\n");
    ICorRuntimeHost* pRuntimeHost = NULL;

    /* Get ICorRuntimeHost instance */

    hr = pRuntimeInfo->GetInterface(CLSID_CorRuntimeHost, IID_ICorRuntimeHost, (VOID**)&pRuntimeHost);
    if (FAILED(hr))
    {
        pRuntimeInfo->Release();
        pMetaHost->Release();
        OutputDebugStringA("[!] pRuntimeInfo->GetInterface(...) failed\n");
        return -1;
    }

    OutputDebugStringA("[+] pRuntimeInfo->GetInterface(...) succeeded\n");

    /* Start the CLR */
    hr = pRuntimeHost->Start();
    if (FAILED(hr))
    {
        pRuntimeHost->Release();
        pRuntimeInfo->Release();
        pMetaHost->Release();
        OutputDebugStringA("[!] pRuntimeHost->Start() failed\n");
        return -1;
    }

    OutputDebugStringA("[+] pRuntimeHost->Start() succeeded\n");

    IUnknownPtr pAppDomainThunk = NULL;
    hr = pRuntimeHost->GetDefaultDomain(&pAppDomainThunk);
    if (FAILED(hr))
    {
        pRuntimeHost->Release();
        pRuntimeInfo->Release();
        pMetaHost->Release();
        OutputDebugStringA("[!] pRuntimeHost->GetDefaultDomain(...) failed\n");
        return -1;
    }

    OutputDebugStringA("[+] pRuntimeHost->GetDefaultDomain(...) succeeded\n");

    _AppDomainPtr pDefaultAppDomain = NULL;
    /* Equivalent of System.AppDomain.CurrentDomain in C# */

    hr = pAppDomainThunk->QueryInterface(__uuidof(_AppDomain), (VOID**)&pDefaultAppDomain);
    if (FAILED(hr))
    {
        pAppDomainThunk->Release();
        pRuntimeHost->Release();
        pRuntimeInfo->Release();
        pMetaHost->Release();
        OutputDebugStringA("[!] pAppDomainThunk->QueryInterface(...) failed\n");
        return -1;
    }

    OutputDebugStringA("[+] pAppDomainThunk->QueryInterface(...) succeeded\n");

    std::wstring wzAppDomainPathW = ExtractResource(dwResourceId);
    if (!wzAppDomainPathW.empty())
    {
        g_szAppDomainPath = W2A(CP_ACP, wzAppDomainPathW.data(), -1);
    }
    BSTR wzAssemblyFileName = SysAllocString(wzAppDomainPathW.data());

    long lRetVal = 0;
    hr = pDefaultAppDomain->ExecuteAssembly_2(wzAssemblyFileName, &lRetVal);

    pDefaultAppDomain->Release();
    pAppDomainThunk->Release();
    pRuntimeHost->Release();
    pRuntimeInfo->Release();
    pMetaHost->Release();
    if (wzAssemblyFileName)
    {
        SysFreeString(wzAssemblyFileName);
    }
    return 0;
}

static std::string 
ReplaceAllText(const std::string& s, const std::string& oldValue, const std::string& newValue)
{
    std::string r = s;
    std::string::size_type pos = 0;
    while ((pos = r.find(oldValue)) != std::string::npos)   //替换所有指定子串
    {
        r.replace(pos, oldValue.length(), newValue);
    }
    return r;
}

static BOOL WINAPI
__SetConsoleTitleW(LPCWSTR lpConsoleTitle)
{
    if (!lpConsoleTitle)
    {
        lpConsoleTitle = L"";
    }
    std::string szConsoleTitle = W2A(CP_ACP, lpConsoleTitle, -1);
    do
    {
        std::string szConsoleTemp = ReplaceAllText(szConsoleTitle, "/", "\\");
        if (0 != _strnicmp(szConsoleTemp.data(), "file:\\\\\\", 9))
        {
            if (0 == _stricmp(szConsoleTemp.data() + 8, g_szAppDomainPath.data()))
            {
                break;
            }
        }
        SetConsoleTitleA(szConsoleTitle.data());
    } while (0, 0);
    return TRUE;
}

static void 
Initialize(void)
{
    g_fSetConsoleTitleW.Install(SetConsoleTitleW, __SetConsoleTitleW);
}

static bool
ProcessInsideCommands(int argc, const char* argv[])
{
    std::string confusion = carg("--confusion", argc, argv);
    if (confusion.empty())
    {
        return false;
    }
    printf("confusion=%d\n", ConfusionAssemblyFile(confusion.data()));
    getchar();
    return true;
}

int main(int argc, const char* argv[])
{
    Initialize();
    if (ProcessInsideCommands(argc, argv))
    {
        return 0;
    }
    return ExecuteEntryPoint(L"v4.0.30319", IDR_PPP1);
}
