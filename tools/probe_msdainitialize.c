#include <stdio.h>

#include "monetdb_oledb.h"

static void print_hr(const char* label, HRESULT hr)
{
    printf("%s: hr=0x%08lX\n", label, (unsigned long)hr);
}

static void print_error_info(void)
{
    IErrorInfo* err = NULL;
    HRESULT hr = GetErrorInfo(0, &err);
    if (FAILED(hr) || !err)
    {
        printf("GetErrorInfo: none\n");
        return;
    }

    {
        BSTR desc = NULL;
        BSTR source = NULL;
        BSTR help = NULL;

        IErrorInfo_GetDescription(err, &desc);
        IErrorInfo_GetSource(err, &source);
        IErrorInfo_GetHelpFile(err, &help);

        wprintf(L"IErrorInfo.Source: %s\n", source ? source : L"<null>");
        wprintf(L"IErrorInfo.Description: %s\n", desc ? desc : L"<null>");
        wprintf(L"IErrorInfo.HelpFile: %s\n", help ? help : L"<null>");

        if (desc) SysFreeString(desc);
        if (source) SysFreeString(source);
        if (help) SysFreeString(help);
    }

    IErrorInfo_Release(err);
}

static HRESULT test_getdatasource(const WCHAR* init_string)
{
    IDataInitialize* init = NULL;
    IDBInitialize* dbinit = NULL;
    HRESULT hr;

    wprintf(L"\n=== IDataInitialize::GetDataSource ===\n%s\n", init_string);

    hr = CoCreateInstance(&CLSID_MSDAINITIALIZE, NULL, CLSCTX_INPROC_SERVER | CLSCTX_LOCAL_SERVER, &IID_IDataInitialize, (void**)&init);
    print_hr("CoCreateInstance(CLSID_MSDAINITIALIZE)", hr);
    if (FAILED(hr))
    {
        print_error_info();
        return hr;
    }

    hr = IDataInitialize_GetDataSource(init, NULL, CLSCTX_INPROC_SERVER | CLSCTX_LOCAL_SERVER, (LPOLESTR)init_string, &IID_IDBInitialize, (IUnknown**)&dbinit);
    print_hr("IDataInitialize::GetDataSource", hr);
    print_error_info();

    if (dbinit)
    {
        IDBInitialize_Release(dbinit);
    }
    IDataInitialize_Release(init);
    return hr;
}

static HRESULT test_direct_provider(void)
{
    CLSID provider_clsid;
    IDBInitialize* dbinit = NULL;
    IDBProperties* props = NULL;
    DBPROPSET set;
    DBPROP prop[4];
    HRESULT hr;

    ZeroMemory(&set, sizeof(set));
    ZeroMemory(prop, sizeof(prop));

    wprintf(L"\n=== Direct CoCreateInstance(provider) ===\n");

    hr = CLSIDFromString(L"{A3F2D8E1-7B4C-4E9A-B5D6-1C8F3E2A9D07}", &provider_clsid);
    print_hr("CLSIDFromString(provider)", hr);
    if (FAILED(hr))
    {
        return hr;
    }

    hr = CoCreateInstance(&provider_clsid, NULL, CLSCTX_INPROC_SERVER | CLSCTX_LOCAL_SERVER, &IID_IDBInitialize, (void**)&dbinit);
    print_hr("CoCreateInstance(CLSID_MonetDBOleDbProvider)", hr);
    if (FAILED(hr))
    {
        print_error_info();
        return hr;
    }

    hr = IDBInitialize_QueryInterface(dbinit, &IID_IDBProperties, (void**)&props);
    print_hr("QueryInterface(IID_IDBProperties)", hr);
    if (FAILED(hr))
    {
        print_error_info();
        IDBInitialize_Release(dbinit);
        return hr;
    }

    prop[0].dwPropertyID = DBPROP_INIT_DATASOURCE;
    prop[0].dwOptions = DBPROPOPTIONS_REQUIRED;
    prop[0].vValue.vt = VT_BSTR;
    prop[0].vValue.bstrVal = SysAllocString(L"MonetDB");

    prop[1].dwPropertyID = DBPROP_AUTH_USERID;
    prop[1].dwOptions = DBPROPOPTIONS_REQUIRED;
    prop[1].vValue.vt = VT_BSTR;
    prop[1].vValue.bstrVal = SysAllocString(L"monetdb");

    prop[2].dwPropertyID = DBPROP_AUTH_PASSWORD;
    prop[2].dwOptions = DBPROPOPTIONS_REQUIRED;
    prop[2].vValue.vt = VT_BSTR;
    prop[2].vValue.bstrVal = SysAllocString(L"monetdb");

    prop[3].dwPropertyID = DBPROP_INIT_CATALOG;
    prop[3].dwOptions = DBPROPOPTIONS_OPTIONAL;
    prop[3].vValue.vt = VT_BSTR;
    prop[3].vValue.bstrVal = SysAllocString(L"demo");

    set.guidPropertySet = DBPROPSET_DBINIT;
    set.cProperties = 4;
    set.rgProperties = prop;

    hr = IDBProperties_SetProperties(props, 1, &set);
    print_hr("IDBProperties::SetProperties", hr);
    print_error_info();

    hr = IDBInitialize_Initialize(dbinit);
    print_hr("IDBInitialize::Initialize", hr);
    print_error_info();

    VariantClear(&prop[0].vValue);
    VariantClear(&prop[1].vValue);
    VariantClear(&prop[2].vValue);
    VariantClear(&prop[3].vValue);
    IDBProperties_Release(props);
    IDBInitialize_Release(dbinit);
    return hr;
}

int wmain(void)
{
    HRESULT hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    print_hr("CoInitializeEx", hr);
    if (FAILED(hr))
    {
        return 1;
    }

    test_getdatasource(L"Provider=MonetDB.OleDb;");
    test_getdatasource(L"Provider=MonetDB.OleDb;Data Source=MonetDB;User ID=monetdb;Password=monetdb;Initial Catalog=demo;");
    test_direct_provider();

    CoUninitialize();
    return 0;
}
