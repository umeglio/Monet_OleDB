#include <stdio.h>

#include "monetdb_oledb.h"

static void print_hr(const char* label, HRESULT hr)
{
    printf("%s: hr=0x%08lX\n", label, (unsigned long)hr);
}

static DBLENGTH buffer_size_for_column(const DBCOLUMNINFO* col)
{
    DBLENGTH size = 0;

    switch (col->wType)
    {
    case DBTYPE_BOOL:
        return sizeof(VARIANT_BOOL);
    case DBTYPE_UI1:
        return sizeof(BYTE);
    case DBTYPE_I2:
    case DBTYPE_UI2:
        return sizeof(SHORT);
    case DBTYPE_I4:
    case DBTYPE_UI4:
        return sizeof(LONG);
    case DBTYPE_I8:
    case DBTYPE_UI8:
        return sizeof(LONGLONG);
    case DBTYPE_R4:
        return sizeof(FLOAT);
    case DBTYPE_R8:
        return sizeof(DOUBLE);
    case DBTYPE_DATE:
        return sizeof(DATE);
    case DBTYPE_GUID:
        return sizeof(GUID);
    case DBTYPE_NUMERIC:
        return sizeof(DB_NUMERIC);
    case DBTYPE_DBDATE:
        return sizeof(DBDATE);
    case DBTYPE_DBTIME:
        return sizeof(DBTIME);
    case DBTYPE_DBTIMESTAMP:
        return sizeof(DBTIMESTAMP);
    case DBTYPE_BYTES:
        size = (DBLENGTH)(col->ulColumnSize ? col->ulColumnSize : 256);
        return size ? size : 256;
    case DBTYPE_STR:
        size = (DBLENGTH)(col->ulColumnSize ? col->ulColumnSize + 1 : 256);
        return size ? size : 256;
    case DBTYPE_WSTR:
        size = (DBLENGTH)((col->ulColumnSize ? col->ulColumnSize + 1 : 256) * sizeof(WCHAR));
        return size ? size : 256 * (DBLENGTH)sizeof(WCHAR);
    default:
        return 512;
    }
}

static void print_value(const DBCOLUMNINFO* col, const BYTE* value, DBSTATUS status)
{
    if (status == DBSTATUS_S_ISNULL)
    {
        printf("<null>");
        return;
    }

    switch (col->wType)
    {
    case DBTYPE_BOOL:
        printf("%s", *((const VARIANT_BOOL*)value) ? "TRUE" : "FALSE");
        break;
    case DBTYPE_UI1:
        printf("%u", (unsigned int)(*((const BYTE*)value)));
        break;
    case DBTYPE_I2:
        printf("%d", (int)(*((const SHORT*)value)));
        break;
    case DBTYPE_UI2:
        printf("%u", (unsigned int)(*((const USHORT*)value)));
        break;
    case DBTYPE_I4:
        printf("%ld", *((const LONG*)value));
        break;
    case DBTYPE_UI4:
        printf("%lu", *((const ULONG*)value));
        break;
    case DBTYPE_I8:
        printf("%lld", *((const LONGLONG*)value));
        break;
    case DBTYPE_UI8:
        printf("%llu", *((const ULONGLONG*)value));
        break;
    case DBTYPE_WSTR:
        wprintf(L"%s", (const WCHAR*)value);
        break;
    case DBTYPE_STR:
        printf("%s", (const CHAR*)value);
        break;
    case DBTYPE_GUID:
    {
        WCHAR guid_text[64];
        if (StringFromGUID2((const GUID*)value, guid_text, (int)MONET_ARRAY_SIZE(guid_text)) > 0)
        {
            wprintf(L"%s", guid_text);
        }
        else
        {
            printf("<guid>");
        }
        break;
    }
    default:
        printf("<type 0x%04X>", (unsigned int)col->wType);
        break;
    }
}

static HRESULT open_schema_session(IDBSchemaRowset** pp_schema)
{
    CLSID provider_clsid;
    IDBInitialize* dbinit = NULL;
    IDBProperties* props = NULL;
    IDBCreateSession* create_session = NULL;
    DBPROPSET set;
    DBPROP prop[4];
    HRESULT hr;

    if (!pp_schema)
    {
        return E_POINTER;
    }

    *pp_schema = NULL;
    ZeroMemory(&set, sizeof(set));
    ZeroMemory(prop, sizeof(prop));

    hr = CLSIDFromString(L"{A3F2D8E1-7B4C-4E9A-B5D6-1C8F3E2A9D07}", &provider_clsid);
    print_hr("CLSIDFromString(provider)", hr);
    if (FAILED(hr))
    {
        return hr;
    }

    hr = CoCreateInstance(&provider_clsid, NULL, CLSCTX_INPROC_SERVER | CLSCTX_LOCAL_SERVER, &IID_IDBInitialize, (void**)&dbinit);
    print_hr("CoCreateInstance(provider)", hr);
    if (FAILED(hr))
    {
        return hr;
    }

    hr = IDBInitialize_QueryInterface(dbinit, &IID_IDBProperties, (void**)&props);
    print_hr("QueryInterface(IDBProperties)", hr);
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

    if (SUCCEEDED(hr))
    {
        hr = IDBInitialize_QueryInterface(dbinit, &IID_IDBCreateSession, (void**)&create_session);
        print_hr("QueryInterface(IDBCreateSession)", hr);
    }

    if (SUCCEEDED(hr))
    {
        hr = IDBCreateSession_CreateSession(create_session, NULL, &IID_IDBSchemaRowset, (IUnknown**)pp_schema);
        print_hr("IDBCreateSession::CreateSession(IDBSchemaRowset)", hr);
    }

    VariantClear(&prop[0].vValue);
    VariantClear(&prop[1].vValue);
    VariantClear(&prop[2].vValue);
    VariantClear(&prop[3].vValue);
    if (create_session) IDBCreateSession_Release(create_session);
    if (props) IDBProperties_Release(props);
    if (dbinit) IDBInitialize_Release(dbinit);
    return hr;
}

static HRESULT probe_rowset(IDBSchemaRowset* schema_iface, REFGUID schema_guid, const WCHAR* label)
{
    IRowset* rowset = NULL;
    IColumnsInfo* columns = NULL;
    IAccessor* accessor = NULL;
    DBCOLUMNINFO* info = NULL;
    OLECHAR* names = NULL;
    DBORDINAL c_columns = 0;
    DBBINDING* bindings = NULL;
    BYTE* buffer = NULL;
    DBLENGTH row_size = 0;
    HACCESSOR h_accessor = 0;
    HROW* rows = NULL;
    DBCOUNTITEM fetched = 0;
    HRESULT hr;
    DBORDINAL i;

    wprintf(L"\n=== %s ===\n", label);
    hr = IDBSchemaRowset_GetRowset(schema_iface, NULL, schema_guid, 0, NULL, &IID_IRowset, 0, NULL, (IUnknown**)&rowset);
    print_hr("IDBSchemaRowset::GetRowset", hr);
    if (FAILED(hr))
    {
        return hr;
    }

    hr = IRowset_QueryInterface(rowset, &IID_IColumnsInfo, (void**)&columns);
    print_hr("QueryInterface(IColumnsInfo)", hr);
    if (FAILED(hr))
    {
        IRowset_Release(rowset);
        return hr;
    }

    hr = IColumnsInfo_GetColumnInfo(columns, &c_columns, &info, &names);
    print_hr("IColumnsInfo::GetColumnInfo", hr);
    if (FAILED(hr))
    {
        IColumnsInfo_Release(columns);
        IRowset_Release(rowset);
        return hr;
    }

    for (i = 0; i < c_columns; ++i)
    {
        wprintf(L"  [%lu] %s type=0x%04X size=%lu precision=%u scale=%u\n",
            (unsigned long)(i + 1),
            info[i].pwszName ? info[i].pwszName : L"<null>",
            (unsigned int)info[i].wType,
            (unsigned long)info[i].ulColumnSize,
            (unsigned int)info[i].bPrecision,
            (unsigned int)info[i].bScale);
    }

    hr = IRowset_QueryInterface(rowset, &IID_IAccessor, (void**)&accessor);
    print_hr("QueryInterface(IAccessor)", hr);
    if (FAILED(hr))
    {
        CoTaskMemFree(info);
        CoTaskMemFree(names);
        IColumnsInfo_Release(columns);
        IRowset_Release(rowset);
        return hr;
    }

    bindings = (DBBINDING*)CoTaskMemAlloc(sizeof(DBBINDING) * c_columns);
    if (!bindings)
    {
        hr = E_OUTOFMEMORY;
    }
    if (SUCCEEDED(hr))
    {
        ZeroMemory(bindings, sizeof(DBBINDING) * c_columns);
        for (i = 0; i < c_columns; ++i)
        {
            DBLENGTH cb = buffer_size_for_column(&info[i]);
            bindings[i].iOrdinal = info[i].iOrdinal;
            bindings[i].obStatus = row_size;
            row_size += sizeof(DBSTATUS);
            bindings[i].obLength = row_size;
            row_size += sizeof(DBLENGTH);
            bindings[i].obValue = row_size;
            row_size += cb;
            bindings[i].dwPart = DBPART_STATUS | DBPART_LENGTH | DBPART_VALUE;
            bindings[i].dwMemOwner = DBMEMOWNER_CLIENTOWNED;
            bindings[i].eParamIO = DBPARAMIO_NOTPARAM;
            bindings[i].cbMaxLen = cb;
            bindings[i].wType = info[i].wType;
            bindings[i].bPrecision = info[i].bPrecision;
            bindings[i].bScale = info[i].bScale;
        }
    }

    if (SUCCEEDED(hr))
    {
        buffer = (BYTE*)CoTaskMemAlloc(row_size);
        if (!buffer)
        {
            hr = E_OUTOFMEMORY;
        }
    }

    if (SUCCEEDED(hr))
    {
        ZeroMemory(buffer, row_size);
        hr = IAccessor_CreateAccessor(accessor, DBACCESSOR_ROWDATA, c_columns, bindings, row_size, &h_accessor, NULL);
        print_hr("IAccessor::CreateAccessor", hr);
    }

    if (SUCCEEDED(hr))
    {
        hr = IRowset_GetNextRows(rowset, DB_NULL_HCHAPTER, 0, 1, &fetched, &rows);
        print_hr("IRowset::GetNextRows", hr);
    }

    if (SUCCEEDED(hr) && fetched == 1 && rows != NULL)
    {
        hr = IRowset_GetData(rowset, rows[0], h_accessor, buffer);
        print_hr("IRowset::GetData", hr);

        for (i = 0; i < c_columns; ++i)
        {
            const DBSTATUS* status = (const DBSTATUS*)(buffer + bindings[i].obStatus);
            const BYTE* value = buffer + bindings[i].obValue;
            wprintf(L"  %s status=0x%08lX value=",
                info[i].pwszName ? info[i].pwszName : L"<null>",
                (unsigned long)(*status));
            print_value(&info[i], value, *status);
            printf("\n");
        }
    }

    if (rows)
    {
        IRowset_ReleaseRows(rowset, fetched, rows, NULL, NULL, NULL);
        CoTaskMemFree(rows);
    }
    if (h_accessor)
    {
        IAccessor_ReleaseAccessor(accessor, h_accessor, NULL);
    }
    CoTaskMemFree(buffer);
    CoTaskMemFree(bindings);
    CoTaskMemFree(info);
    CoTaskMemFree(names);
    if (accessor) IAccessor_Release(accessor);
    IColumnsInfo_Release(columns);
    IRowset_Release(rowset);
    return hr;
}

int wmain(void)
{
    IDBSchemaRowset* schema_iface = NULL;
    HRESULT hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    print_hr("CoInitializeEx", hr);
    if (FAILED(hr))
    {
        return 1;
    }

    hr = open_schema_session(&schema_iface);
    if (SUCCEEDED(hr))
    {
        probe_rowset(schema_iface, &DBSCHEMA_TABLES, L"DBSCHEMA_TABLES");
        probe_rowset(schema_iface, &DBSCHEMA_COLUMNS, L"DBSCHEMA_COLUMNS");
        probe_rowset(schema_iface, &DBSCHEMA_PRIMARY_KEYS, L"DBSCHEMA_PRIMARY_KEYS");
        probe_rowset(schema_iface, &DBSCHEMA_FOREIGN_KEYS, L"DBSCHEMA_FOREIGN_KEYS");
        probe_rowset(schema_iface, &DBSCHEMA_INDEXES, L"DBSCHEMA_INDEXES");
        probe_rowset(schema_iface, &DBSCHEMA_PROVIDER_TYPES, L"DBSCHEMA_PROVIDER_TYPES");
        IDBSchemaRowset_Release(schema_iface);
    }

    CoUninitialize();
    return FAILED(hr) ? 1 : 0;
}
