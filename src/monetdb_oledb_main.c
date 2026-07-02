#define INITGUID
#define DBINITCONSTANTS

#include "monetdb_oledb.h"

HMODULE g_monet_module = NULL;
static LONG g_object_count = 0;
static LONG g_lock_count = 0;

DEFINE_GUID(CLSID_MonetDBOleDbProvider, 0xa3f2d8e1, 0x7b4c, 0x4e9a, 0xb5, 0xd6, 0x1c, 0x8f, 0x3e, 0x2a, 0x9d, 0x07);
DEFINE_GUID(IID_IMonetRowsetInternal, 0x2d5e9338, 0x4ab1, 0x48f6, 0xb6, 0x48, 0x0c, 0x41, 0x81, 0xe1, 0x60, 0x10);

static void Monet_FormatGuidBootstrapA(REFGUID guid, CHAR* buffer, size_t cch_buffer)
{
    WCHAR wide[64];

    if (!buffer || cch_buffer == 0)
    {
        return;
    }

    buffer[0] = '\0';
    if (!guid)
    {
        Monet_StringCopyA(buffer, cch_buffer, "<null-guid>");
        return;
    }

    if (StringFromGUID2(guid, wide, (int)MONET_ARRAY_SIZE(wide)) > 0)
    {
        Monet_WideToAnsi(buffer, cch_buffer, wide);
        return;
    }

    _snprintf(buffer, cch_buffer, "{%08lX-%04X-%04X-...}", guid->Data1, guid->Data2, guid->Data3);
    buffer[cch_buffer - 1] = '\0';
}

LONG Monet_ObjectAddRef(void)
{
    LONG count = InterlockedIncrement(&g_object_count);
    if (Config_IsTraceEnabled())
    {
        Log_WriteA(MONET_LOG_TRACE, "Lifetime::ObjectAddRef", "object_count=%ld lock_count=%ld", count, g_lock_count);
    }
    return count;
}

LONG Monet_ObjectRelease(void)
{
    LONG count = InterlockedDecrement(&g_object_count);
    if (Config_IsTraceEnabled())
    {
        Log_WriteA(MONET_LOG_TRACE, "Lifetime::ObjectRelease", "object_count=%ld lock_count=%ld", count, g_lock_count);
    }
    return count;
}

LONG Monet_LockAddRef(void)
{
    LONG count = InterlockedIncrement(&g_lock_count);
    if (Config_IsTraceEnabled())
    {
        Log_WriteA(MONET_LOG_TRACE, "Lifetime::LockAddRef", "object_count=%ld lock_count=%ld", g_object_count, count);
    }
    return count;
}

LONG Monet_LockRelease(void)
{
    LONG count = InterlockedDecrement(&g_lock_count);
    if (Config_IsTraceEnabled())
    {
        Log_WriteA(MONET_LOG_TRACE, "Lifetime::LockRelease", "object_count=%ld lock_count=%ld", g_object_count, count);
    }
    return count;
}

typedef struct MonetClassFactory
{
    IClassFactory IClassFactory_iface;
    LONG ref_count;
} MonetClassFactory;

static HRESULT STDMETHODCALLTYPE CF_QueryInterface(IClassFactory* iface, REFIID riid, void** ppv)
{
    MonetClassFactory* self = CONTAINING_RECORD(iface, MonetClassFactory, IClassFactory_iface);

    if (!ppv)
    {
        return E_POINTER;
    }

    *ppv = NULL;
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IClassFactory))
    {
        *ppv = &self->IClassFactory_iface;
        IClassFactory_AddRef(&self->IClassFactory_iface);
        return S_OK;
    }

    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE CF_AddRef(IClassFactory* iface)
{
    MonetClassFactory* self = CONTAINING_RECORD(iface, MonetClassFactory, IClassFactory_iface);
    return (ULONG)InterlockedIncrement(&self->ref_count);
}

static ULONG STDMETHODCALLTYPE CF_Release(IClassFactory* iface)
{
    MonetClassFactory* self = CONTAINING_RECORD(iface, MonetClassFactory, IClassFactory_iface);
    ULONG ref = (ULONG)InterlockedDecrement(&self->ref_count);
    if (ref == 0)
    {
        CoTaskMemFree(self);
        Monet_ObjectRelease();
    }
    return ref;
}

static HRESULT STDMETHODCALLTYPE CF_CreateInstance(IClassFactory* iface, IUnknown* outer, REFIID riid, void** ppv)
{
    HRESULT hr;
    CHAR iid_text[64];
    MONET_UNUSED(iface);

    Monet_FormatGuidBootstrapA(riid, iid_text, MONET_ARRAY_SIZE(iid_text));
    Bootstrap_LogA("CF_CreateInstance outer=%p riid=%s", outer, iid_text);
    hr = DataSource_CreateInstance(outer, riid, ppv);
    Bootstrap_LogA("CF_CreateInstance hr=0x%08lX", (unsigned long)hr);
    return hr;
}

static HRESULT STDMETHODCALLTYPE CF_LockServer(IClassFactory* iface, BOOL lock)
{
    MONET_UNUSED(iface);
    if (lock)
    {
        Monet_LockAddRef();
    }
    else
    {
        Monet_LockRelease();
    }
    return S_OK;
}

static IClassFactoryVtbl g_class_factory_vtbl =
{
    CF_QueryInterface,
    CF_AddRef,
    CF_Release,
    CF_CreateInstance,
    CF_LockServer
};

static HRESULT CreateClassFactory(REFIID riid, void** ppv)
{
    MonetClassFactory* cf = NULL;
    HRESULT hr;

    if (!ppv)
    {
        return E_POINTER;
    }

    *ppv = NULL;
    cf = (MonetClassFactory*)CoTaskMemAlloc(sizeof(*cf));
    if (!cf)
    {
        return E_OUTOFMEMORY;
    }
    ZeroMemory(cf, sizeof(*cf));
    cf->IClassFactory_iface.lpVtbl = &g_class_factory_vtbl;
    cf->ref_count = 1;
    Monet_ObjectAddRef();

    hr = IClassFactory_QueryInterface(&cf->IClassFactory_iface, riid, ppv);
    IClassFactory_Release(&cf->IClassFactory_iface);
    return hr;
}

static HRESULT SetRegistryStringValue(HKEY root, const WCHAR* subkey, const WCHAR* value_name, const WCHAR* value)
{
    HKEY key = NULL;
    DWORD bytes = 0;
    LONG rc = 0;

    rc = RegCreateKeyExW(root, subkey, 0, NULL, REG_OPTION_NON_VOLATILE, KEY_WRITE, NULL, &key, NULL);
    if (rc != ERROR_SUCCESS)
    {
        return HRESULT_FROM_WIN32(rc);
    }

    bytes = (DWORD)((wcslen(value) + 1) * sizeof(WCHAR));
    rc = RegSetValueExW(key, value_name, 0, REG_SZ, (const BYTE*)value, bytes);
    RegCloseKey(key);
    return (rc == ERROR_SUCCESS) ? S_OK : HRESULT_FROM_WIN32(rc);
}

static HRESULT SetRegistryDwordValue(HKEY root, const WCHAR* subkey, const WCHAR* value_name, DWORD value)
{
    HKEY key = NULL;
    LONG rc = 0;

    rc = RegCreateKeyExW(root, subkey, 0, NULL, REG_OPTION_NON_VOLATILE, KEY_WRITE, NULL, &key, NULL);
    if (rc != ERROR_SUCCESS)
    {
        return HRESULT_FROM_WIN32(rc);
    }

    rc = RegSetValueExW(key, value_name, 0, REG_DWORD, (const BYTE*)&value, sizeof(value));
    RegCloseKey(key);
    return (rc == ERROR_SUCCESS) ? S_OK : HRESULT_FROM_WIN32(rc);
}

static void DeleteRegistryTreeIfExists(HKEY root, const WCHAR* subkey)
{
    RegDeleteTreeW(root, subkey);
}

static HRESULT RegisterComServer(void)
{
    WCHAR module_path[MAX_PATH];
    WCHAR clsid_key[256];
    WCHAR progid_key[256];
    WCHAR ver_progid_key[256];
    HRESULT hr;

    if (!GetModuleFileNameW(g_monet_module, module_path, MONET_ARRAY_SIZE(module_path)))
    {
        return HRESULT_FROM_WIN32(GetLastError());
    }

    StringFromGUID2(&CLSID_MonetDBOleDbProvider, clsid_key, MONET_ARRAY_SIZE(clsid_key));

    {
        WCHAR key[384];
        _snwprintf(key, MONET_ARRAY_SIZE(key), L"CLSID\\%s", clsid_key);
        key[MONET_ARRAY_SIZE(key) - 1] = L'\0';
        hr = SetRegistryStringValue(HKEY_CLASSES_ROOT, key, NULL, MONETDB_OLEDB_FRIENDLY_NAME_W);
        if (FAILED(hr)) return hr;
        hr = SetRegistryDwordValue(HKEY_CLASSES_ROOT, key, L"OLEDB_SERVICES", 0x00000000UL);
        if (FAILED(hr)) return hr;

        _snwprintf(key, MONET_ARRAY_SIZE(key), L"CLSID\\%s\\InprocServer32", clsid_key);
        key[MONET_ARRAY_SIZE(key) - 1] = L'\0';
        hr = SetRegistryStringValue(HKEY_CLASSES_ROOT, key, NULL, module_path);
        if (FAILED(hr)) return hr;
        hr = SetRegistryStringValue(HKEY_CLASSES_ROOT, key, L"ThreadingModel", L"Both");
        if (FAILED(hr)) return hr;

        _snwprintf(key, MONET_ARRAY_SIZE(key), L"CLSID\\%s\\OLE DB Provider", clsid_key);
        key[MONET_ARRAY_SIZE(key) - 1] = L'\0';
        hr = SetRegistryStringValue(HKEY_CLASSES_ROOT, key, NULL, MONETDB_OLEDB_FRIENDLY_NAME_W);
        if (FAILED(hr)) return hr;

        _snwprintf(key, MONET_ARRAY_SIZE(key), L"CLSID\\%s\\ProgID", clsid_key);
        key[MONET_ARRAY_SIZE(key) - 1] = L'\0';
        hr = SetRegistryStringValue(HKEY_CLASSES_ROOT, key, NULL, MONETDB_OLEDB_PROGID_VER_W);
        if (FAILED(hr)) return hr;

        _snwprintf(key, MONET_ARRAY_SIZE(key), L"CLSID\\%s\\VersionIndependentProgID", clsid_key);
        key[MONET_ARRAY_SIZE(key) - 1] = L'\0';
        hr = SetRegistryStringValue(HKEY_CLASSES_ROOT, key, NULL, MONETDB_OLEDB_PROGID_W);
        if (FAILED(hr)) return hr;
    }

    _snwprintf(progid_key, MONET_ARRAY_SIZE(progid_key), L"%s", MONETDB_OLEDB_PROGID_W);
    progid_key[MONET_ARRAY_SIZE(progid_key) - 1] = L'\0';
    hr = SetRegistryStringValue(HKEY_CLASSES_ROOT, progid_key, NULL, MONETDB_OLEDB_FRIENDLY_NAME_W);
    if (FAILED(hr)) return hr;
    {
        WCHAR key[384];
        _snwprintf(key, MONET_ARRAY_SIZE(key), L"%s\\CLSID", progid_key);
        key[MONET_ARRAY_SIZE(key) - 1] = L'\0';
        hr = SetRegistryStringValue(HKEY_CLASSES_ROOT, key, NULL, clsid_key);
        if (FAILED(hr)) return hr;

        _snwprintf(key, MONET_ARRAY_SIZE(key), L"%s\\CurVer", progid_key);
        key[MONET_ARRAY_SIZE(key) - 1] = L'\0';
        hr = SetRegistryStringValue(HKEY_CLASSES_ROOT, key, NULL, MONETDB_OLEDB_PROGID_VER_W);
        if (FAILED(hr)) return hr;
    }

    _snwprintf(ver_progid_key, MONET_ARRAY_SIZE(ver_progid_key), L"%s", MONETDB_OLEDB_PROGID_VER_W);
    ver_progid_key[MONET_ARRAY_SIZE(ver_progid_key) - 1] = L'\0';
    hr = SetRegistryStringValue(HKEY_CLASSES_ROOT, ver_progid_key, NULL, MONETDB_OLEDB_FRIENDLY_NAME_W);
    if (FAILED(hr)) return hr;
    {
        WCHAR key[384];
        _snwprintf(key, MONET_ARRAY_SIZE(key), L"%s\\CLSID", ver_progid_key);
        key[MONET_ARRAY_SIZE(key) - 1] = L'\0';
        hr = SetRegistryStringValue(HKEY_CLASSES_ROOT, key, NULL, clsid_key);
        if (FAILED(hr)) return hr;

        _snwprintf(key, MONET_ARRAY_SIZE(key), L"%s\\OLE DB Provider", ver_progid_key);
        key[MONET_ARRAY_SIZE(key) - 1] = L'\0';
        hr = SetRegistryStringValue(HKEY_CLASSES_ROOT, key, NULL, MONETDB_OLEDB_FRIENDLY_NAME_W);
        if (FAILED(hr)) return hr;
    }

    hr = SetRegistryStringValue(HKEY_CLASSES_ROOT, L"MonetDB.OleDb\\OLE DB Provider", NULL, MONETDB_OLEDB_FRIENDLY_NAME_W);
    if (FAILED(hr)) return hr;

    return S_OK;
}

static HRESULT UnregisterComServer(void)
{
    WCHAR clsid_string[64];
    WCHAR key[384];

    StringFromGUID2(&CLSID_MonetDBOleDbProvider, clsid_string, MONET_ARRAY_SIZE(clsid_string));
    _snwprintf(key, MONET_ARRAY_SIZE(key), L"CLSID\\%s", clsid_string);
    key[MONET_ARRAY_SIZE(key) - 1] = L'\0';
    DeleteRegistryTreeIfExists(HKEY_CLASSES_ROOT, key);
    DeleteRegistryTreeIfExists(HKEY_CLASSES_ROOT, MONETDB_OLEDB_PROGID_W);
    DeleteRegistryTreeIfExists(HKEY_CLASSES_ROOT, MONETDB_OLEDB_PROGID_VER_W);
    return S_OK;
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved)
{
    CHAR module_path[MAX_PATH];
    CHAR host_path[MAX_PATH];
    CHAR* host_name = NULL;
    MONET_UNUSED(reserved);

    if (reason == DLL_PROCESS_ATTACH)
    {
        DisableThreadLibraryCalls(instance);
        g_monet_module = instance;
        GetModuleFileNameA(instance, module_path, MONET_ARRAY_SIZE(module_path));
        GetModuleFileNameA(NULL, host_path, MONET_ARRAY_SIZE(host_path));
        host_name = strrchr(host_path, '\\');
        host_name = host_name ? (host_name + 1) : host_path;
        Bootstrap_LogA("DllMain ATTACH v%s host='%s' dll='%s' clsid='{A3F2D8E1-7B4C-4E9A-B5D6-1C8F3E2A9D07}'",
            MONETDB_OLEDB_VERSION_A,
            host_name,
            module_path);
    }
    else if (reason == DLL_PROCESS_DETACH)
    {
        Bootstrap_LogA("DllMain DETACH");
        Odbc_ShutdownEnvironment();
        Log_Shutdown();
    }

    return TRUE;
}

STDAPI DllCanUnloadNow(void)
{
    HRESULT hr = (g_object_count == 0 && g_lock_count == 0) ? S_OK : S_FALSE;
    if (Config_IsTraceEnabled())
    {
        Log_WriteA(MONET_LOG_TRACE, "DllCanUnloadNow", "object_count=%ld lock_count=%ld hr=0x%08lX", g_object_count, g_lock_count, (unsigned long)hr);
    }
    return hr;
}

STDAPI DllGetClassObject(REFCLSID rclsid, REFIID riid, LPVOID* ppv)
{
    CHAR clsid_text[64];
    CHAR iid_text[64];

    if (!ppv)
    {
        return E_POINTER;
    }

    *ppv = NULL;
    Monet_FormatGuidBootstrapA(rclsid, clsid_text, MONET_ARRAY_SIZE(clsid_text));
    Monet_FormatGuidBootstrapA(riid, iid_text, MONET_ARRAY_SIZE(iid_text));
    Bootstrap_LogA("DllGetClassObject clsid=%s iid=%s", clsid_text, iid_text);

    if (!IsEqualCLSID(rclsid, &CLSID_MonetDBOleDbProvider))
    {
        return CLASS_E_CLASSNOTAVAILABLE;
    }

    return CreateClassFactory(riid, ppv);
}

STDAPI DllRegisterServer(void)
{
    HRESULT hr = RegisterComServer();
    if (SUCCEEDED(hr))
    {
        Bootstrap_LogA("DllRegisterServer completato");
    }
    else
    {
        Bootstrap_LogA("DllRegisterServer fallito hr=0x%08lX", (unsigned long)hr);
    }
    return hr;
}

STDAPI DllUnregisterServer(void)
{
    HRESULT hr = UnregisterComServer();
    if (SUCCEEDED(hr))
    {
        Bootstrap_LogA("DllUnregisterServer completato");
    }
    return hr;
}
