#include <stdio.h>
#include <stddef.h>

#include "monetdb_oledb.h"

typedef HRESULT (STDAPICALLTYPE* DllGetClassObjectProc)(REFCLSID rclsid, REFIID riid, LPVOID* ppv);

typedef struct ProbeInsertRowData
{
    DBSTATUS codpro_status;
    LONG codpro;
    DBSTATUS rifor_status;
    BSTR rifor;
} ProbeInsertRowData;

static void print_hr(const char* label, HRESULT hr)
{
    printf("%s: hr=0x%08lX\n", label, (unsigned long)hr);
}

static HRESULT create_provider_from_dll(LPCWSTR dll_path, REFCLSID provider_clsid, IDBInitialize** pp_dbinit)
{
    HMODULE module_handle;
    DllGetClassObjectProc get_class_object;
    IClassFactory* factory = NULL;
    HRESULT hr;

    if (!dll_path || !pp_dbinit)
    {
        return E_POINTER;
    }

    module_handle = LoadLibraryW(dll_path);
    if (!module_handle)
    {
        hr = HRESULT_FROM_WIN32(GetLastError());
        print_hr("LoadLibrary(provider dll)", hr);
        return hr;
    }

    get_class_object = (DllGetClassObjectProc)GetProcAddress(module_handle, "DllGetClassObject");
    if (!get_class_object)
    {
        hr = HRESULT_FROM_WIN32(GetLastError());
        print_hr("GetProcAddress(DllGetClassObject)", hr);
        return hr;
    }

    hr = get_class_object(provider_clsid, &IID_IClassFactory, (void**)&factory);
    print_hr("DllGetClassObject(IID_IClassFactory)", hr);
    if (FAILED(hr))
    {
        return hr;
    }

    hr = IClassFactory_CreateInstance(factory, NULL, &IID_IDBInitialize, (void**)pp_dbinit);
    print_hr("IClassFactory::CreateInstance(IID_IDBInitialize)", hr);
    IClassFactory_Release(factory);
    return hr;
}

static HRESULT open_provider(LPCWSTR dll_path, IDBInitialize** pp_dbinit)
{
    CLSID provider_clsid;
    IDBInitialize* dbinit = NULL;
    IDBProperties* props = NULL;
    DBPROPSET set;
    DBPROP prop[4];
    HRESULT hr;

    if (!pp_dbinit)
    {
        return E_POINTER;
    }
    *pp_dbinit = NULL;

    ZeroMemory(&set, sizeof(set));
    ZeroMemory(prop, sizeof(prop));

    hr = CLSIDFromString(L"{A3F2D8E1-7B4C-4E9A-B5D6-1C8F3E2A9D07}", &provider_clsid);
    print_hr("CLSIDFromString(provider)", hr);
    if (FAILED(hr))
    {
        return hr;
    }

    if (dll_path && dll_path[0])
    {
        wprintf(L"DLL: %ls\n", dll_path);
        hr = create_provider_from_dll(dll_path, &provider_clsid, &dbinit);
    }
    else
    {
        hr = CoCreateInstance(&provider_clsid, NULL, CLSCTX_INPROC_SERVER | CLSCTX_LOCAL_SERVER, &IID_IDBInitialize, (void**)&dbinit);
        print_hr("CoCreateInstance(provider)", hr);
    }
    if (FAILED(hr))
    {
        return hr;
    }

    hr = IDBInitialize_QueryInterface(dbinit, &IID_IDBProperties, (void**)&props);
    print_hr("QueryInterface(IID_IDBProperties)", hr);
    if (FAILED(hr))
    {
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
    if (SUCCEEDED(hr))
    {
        hr = IDBInitialize_Initialize(dbinit);
        print_hr("IDBInitialize::Initialize", hr);
    }

    VariantClear(&prop[0].vValue);
    VariantClear(&prop[1].vValue);
    VariantClear(&prop[2].vValue);
    VariantClear(&prop[3].vValue);
    IDBProperties_Release(props);
    if (FAILED(hr))
    {
        IDBInitialize_Release(dbinit);
        return hr;
    }

    *pp_dbinit = dbinit;
    return S_OK;
}

static HRESULT execute_sql(IDBCreateCommand* create_command, LPCWSTR sql)
{
    HRESULT hr;
    ICommandText* command = NULL;
    DBROWCOUNT rows_affected = 0;

    hr = IDBCreateCommand_CreateCommand(create_command, NULL, &IID_ICommandText, (IUnknown**)&command);
    print_hr("IDBCreateCommand::CreateCommand", hr);
    if (SUCCEEDED(hr))
    {
        wprintf(L"SQL: %ls\n", sql);
        hr = ICommandText_SetCommandText(command, &DBGUID_DEFAULT, sql);
        print_hr("ICommandText::SetCommandText", hr);
    }
    if (SUCCEEDED(hr))
    {
        hr = ICommandText_Execute(command, NULL, &IID_NULL, NULL, &rows_affected, NULL);
        print_hr("ICommandText::Execute(IID_NULL)", hr);
    }
    if (command)
    {
        ICommandText_Release(command);
    }
    return hr;
}

static HRESULT probe_rowsetchange_sql(IDBCreateCommand* create_command, LPCWSTR sql)
{
    HRESULT hr;
    ICommandText* command = NULL;
    DBROWCOUNT rows_affected = 0;
    IRowsetChange* rowset_change = NULL;

    hr = IDBCreateCommand_CreateCommand(create_command, NULL, &IID_ICommandText, (IUnknown**)&command);
    print_hr("IDBCreateCommand::CreateCommand", hr);
    if (SUCCEEDED(hr))
    {
        wprintf(L"SQL: %ls\n", sql);
        hr = ICommandText_SetCommandText(command, &DBGUID_DEFAULT, sql);
        print_hr("ICommandText::SetCommandText", hr);
    }
    if (SUCCEEDED(hr))
    {
        hr = ICommandText_Execute(command, NULL, &IID_IRowsetChange, NULL, &rows_affected, (IUnknown**)&rowset_change);
        print_hr("ICommandText::Execute(IID_IRowsetChange)", hr);
    }

    if (rowset_change)
    {
        IRowsetChange_Release(rowset_change);
    }
    if (command)
    {
        ICommandText_Release(command);
    }
    return hr;
}

static HRESULT probe_insert_bstr(IDBCreateCommand* create_command)
{
    HRESULT hr;
    ICommandText* command = NULL;
    IRowsetChange* rowset_change = NULL;
    IAccessor* accessor = NULL;
    HACCESSOR h_accessor = DB_NULL_HACCESSOR;
    DBBINDING bindings[2];
    DBBINDSTATUS bind_status[2];
    ProbeInsertRowData row;
    DBROWCOUNT rows_affected = 0;

    execute_sql(create_command, L"DROP TABLE IF EXISTS codex_irchg_test");
    hr = execute_sql(create_command, L"CREATE TABLE codex_irchg_test (codpro INT, rifor VARCHAR(4))");
    if (FAILED(hr))
    {
        return hr;
    }

    hr = IDBCreateCommand_CreateCommand(create_command, NULL, &IID_ICommandText, (IUnknown**)&command);
    print_hr("IDBCreateCommand::CreateCommand", hr);
    if (SUCCEEDED(hr))
    {
        hr = ICommandText_SetCommandText(command, &DBGUID_DEFAULT, L"SELECT codpro, rifor FROM codex_irchg_test WHERE 1=0");
        print_hr("ICommandText::SetCommandText", hr);
    }
    if (SUCCEEDED(hr))
    {
        hr = ICommandText_Execute(command, NULL, &IID_IRowsetChange, NULL, &rows_affected, (IUnknown**)&rowset_change);
        print_hr("ICommandText::Execute(IID_IRowsetChange for insert)", hr);
    }
    if (SUCCEEDED(hr))
    {
        hr = IRowsetChange_QueryInterface(rowset_change, &IID_IAccessor, (void**)&accessor);
        print_hr("IRowsetChange::QueryInterface(IID_IAccessor)", hr);
    }
    if (SUCCEEDED(hr))
    {
        ZeroMemory(bindings, sizeof(bindings));
        bindings[0].iOrdinal = 1;
        bindings[0].obStatus = offsetof(ProbeInsertRowData, codpro_status);
        bindings[0].obValue = offsetof(ProbeInsertRowData, codpro);
        bindings[0].dwPart = DBPART_STATUS | DBPART_VALUE;
        bindings[0].wType = DBTYPE_I4;
        bindings[0].cbMaxLen = sizeof(LONG);

        bindings[1].iOrdinal = 2;
        bindings[1].obStatus = offsetof(ProbeInsertRowData, rifor_status);
        bindings[1].obValue = offsetof(ProbeInsertRowData, rifor);
        bindings[1].dwPart = DBPART_STATUS | DBPART_VALUE;
        bindings[1].wType = DBTYPE_BSTR;
        bindings[1].cbMaxLen = sizeof(BSTR);

        hr = IAccessor_CreateAccessor(accessor, DBACCESSOR_ROWDATA, 2, bindings, sizeof(row), &h_accessor, bind_status);
        print_hr("IAccessor::CreateAccessor(DBTYPE_BSTR)", hr);
    }
    if (SUCCEEDED(hr))
    {
        ZeroMemory(&row, sizeof(row));
        row.codpro_status = DBSTATUS_S_OK;
        row.codpro = 137;
        row.rifor_status = DBSTATUS_S_OK;
        row.rifor = SysAllocString(L"AB");
        hr = IRowsetChange_InsertRow(rowset_change, (HCHAPTER)0, h_accessor, &row, NULL);
        print_hr("IRowsetChange::InsertRow(DBTYPE_BSTR)", hr);
        SysFreeString(row.rifor);
    }

    if (accessor && h_accessor)
    {
        IAccessor_ReleaseAccessor(accessor, h_accessor, NULL);
    }
    if (accessor)
    {
        IAccessor_Release(accessor);
    }
    if (rowset_change)
    {
        IRowsetChange_Release(rowset_change);
    }
    if (command)
    {
        ICommandText_Release(command);
    }
    execute_sql(create_command, L"DROP TABLE IF EXISTS codex_irchg_test");
    return hr;
}

int wmain(int argc, wchar_t** argv)
{
    HRESULT hr;
    IDBInitialize* dbinit = NULL;
    IDBCreateSession* create_session = NULL;
    IDBCreateCommand* create_command = NULL;
    LPCWSTR dll_path = (argc > 1) ? argv[1] : NULL;

    hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    print_hr("CoInitializeEx", hr);
    if (FAILED(hr))
    {
        return 1;
    }

    hr = open_provider(dll_path, &dbinit);
    if (FAILED(hr))
    {
        CoUninitialize();
        return 1;
    }

    hr = IDBInitialize_QueryInterface(dbinit, &IID_IDBCreateSession, (void**)&create_session);
    print_hr("QueryInterface(IID_IDBCreateSession)", hr);
    if (SUCCEEDED(hr))
    {
        hr = IDBCreateSession_CreateSession(create_session, NULL, &IID_IDBCreateCommand, (IUnknown**)&create_command);
        print_hr("IDBCreateSession::CreateSession", hr);
    }
    if (SUCCEEDED(hr))
    {
        hr = probe_rowsetchange_sql(create_command, L"Select * from sys.cmw_anagrafica");
    }
    if (SUCCEEDED(hr))
    {
        hr = probe_rowsetchange_sql(create_command, L"Select codpro, rifor from sys.cmw_anagrafica where codpro is not null");
    }
    if (SUCCEEDED(hr))
    {
        hr = probe_rowsetchange_sql(create_command, L"Select left(rifor,1) as x from sys.cmw_anagrafica");
    }
    if (SUCCEEDED(hr))
    {
        hr = probe_insert_bstr(create_command);
    }

    if (create_command)
    {
        IDBCreateCommand_Release(create_command);
    }
    if (create_session)
    {
        IDBCreateSession_Release(create_session);
    }
    if (dbinit)
    {
        IDBInitialize_Uninitialize(dbinit);
        IDBInitialize_Release(dbinit);
    }

    CoUninitialize();
    return FAILED(hr) ? 1 : 0;
}
