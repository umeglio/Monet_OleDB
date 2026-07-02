#include <stdio.h>

#include "monetdb_oledb.h"

static void print_hr(const char* label, HRESULT hr)
{
    printf("%s: hr=0x%08lX\n", label, (unsigned long)hr);
}

static DBLENGTH align_offset(DBLENGTH offset, DBLENGTH alignment)
{
    DBLENGTH remainder = offset % alignment;
    if (remainder == 0)
    {
        return offset;
    }
    return offset + (alignment - remainder);
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

static void print_first_value(const DBCOLUMNINFO* col, const BYTE* row_buffer, const DBBINDING* binding)
{
    const DBSTATUS* status = (const DBSTATUS*)(row_buffer + binding->obStatus);
    const BYTE* value = row_buffer + binding->obValue;

    if (*status == DBSTATUS_S_ISNULL)
    {
        printf("<null>");
        return;
    }

    switch (col->wType)
    {
    case DBTYPE_WSTR:
        wprintf(L"%s", (const WCHAR*)value);
        break;
    case DBTYPE_STR:
        printf("%s", (const CHAR*)value);
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
    case DBTYPE_DBDATE:
    {
        const DBDATE* d = (const DBDATE*)value;
        printf("%04d-%02u-%02u", (int)d->year, (unsigned)d->month, (unsigned)d->day);
        break;
    }
    case DBTYPE_DBTIME:
    {
        const DBTIME* t = (const DBTIME*)value;
        printf("%02u:%02u:%02u", (unsigned)t->hour, (unsigned)t->minute, (unsigned)t->second);
        break;
    }
    case DBTYPE_DBTIMESTAMP:
    {
        const DBTIMESTAMP* ts = (const DBTIMESTAMP*)value;
        printf(
            "%04d-%02u-%02u %02u:%02u:%02u.%09lu",
            (int)ts->year,
            (unsigned)ts->month,
            (unsigned)ts->day,
            (unsigned)ts->hour,
            (unsigned)ts->minute,
            (unsigned)ts->second,
            (unsigned long)ts->fraction);
        break;
    }
    default:
        printf("<type 0x%04X>", (unsigned int)col->wType);
        break;
    }
}

static HRESULT open_command(ICommandText** pp_command)
{
    CLSID provider_clsid;
    IDBInitialize* dbinit = NULL;
    IDBProperties* props = NULL;
    IDBCreateSession* create_session = NULL;
    IDBCreateCommand* create_command = NULL;
    DBPROPSET set;
    DBPROP prop[4];
    HRESULT hr;

    if (!pp_command)
    {
        return E_POINTER;
    }

    *pp_command = NULL;
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
    prop[2].vValue.bstrVal = SysAllocString(L"S1p@$");

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
        hr = IDBCreateSession_CreateSession(create_session, NULL, &IID_IDBCreateCommand, (IUnknown**)&create_command);
        print_hr("IDBCreateSession::CreateSession(IDBCreateCommand)", hr);
    }
    if (SUCCEEDED(hr))
    {
        hr = IDBCreateCommand_CreateCommand(create_command, NULL, &IID_ICommandText, (IUnknown**)pp_command);
        print_hr("IDBCreateCommand::CreateCommand(ICommandText)", hr);
    }

    VariantClear(&prop[0].vValue);
    VariantClear(&prop[1].vValue);
    VariantClear(&prop[2].vValue);
    VariantClear(&prop[3].vValue);

    if (create_command) IDBCreateCommand_Release(create_command);
    if (create_session) IDBCreateSession_Release(create_session);
    if (props) IDBProperties_Release(props);
    if (dbinit) IDBInitialize_Release(dbinit);

    return hr;
}

int wmain(int argc, wchar_t* argv[])
{
    const WCHAR* sql_text = L"SELECT left(apr010,2) AS x FROM sys.art";
    ICommandText* command = NULL;
    IColumnsInfo* command_columns = NULL;
    IRowset* rowset = NULL;
    IColumnsInfo* columns = NULL;
    IAccessor* accessor = NULL;
    DBCOLUMNINFO* info = NULL;
    OLECHAR* names = NULL;
    DBCOLUMNINFO* pre_info = NULL;
    OLECHAR* pre_names = NULL;
    DBORDINAL pre_columns = 0;
    DBORDINAL c_columns = 0;
    DBBINDING* bindings = NULL;
    DBBINDSTATUS* bind_status = NULL;
    BYTE* row_buffer = NULL;
    DBLENGTH row_size = 0;
    HACCESSOR h_accessor = 0;
    HROW* rows = NULL;
    DBROWCOUNT affected = 0;
    DBCOUNTITEM fetched = 0;
    ULONGLONG total_rows = 0;
    HRESULT hr;
    DBORDINAL i;
    LARGE_INTEGER freq;
    LARGE_INTEGER start;
    LARGE_INTEGER stop;
    ULONGLONG elapsed_us = 0;
    BOOL finished = FALSE;

    hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    print_hr("CoInitializeEx", hr);
    if (FAILED(hr))
    {
        return 1;
    }

    hr = open_command(&command);
    if (FAILED(hr))
    {
        CoUninitialize();
        return 1;
    }

    if (argc > 1 && argv[1] && argv[1][0])
    {
        sql_text = argv[1];
    }

    wprintf(L"SQL: %s\n", sql_text);
    hr = ICommandText_SetCommandText(command, &DBGUID_DBSQL, (LPOLESTR)sql_text);
    print_hr("ICommandText::SetCommandText", hr);
    if (FAILED(hr))
    {
        ICommandText_Release(command);
        CoUninitialize();
        return 1;
    }

    hr = ICommandText_QueryInterface(command, &IID_IColumnsInfo, (void**)&command_columns);
    print_hr("QueryInterface(command IColumnsInfo)", hr);
    if (FAILED(hr))
    {
        ICommandText_Release(command);
        CoUninitialize();
        return 1;
    }

    hr = IColumnsInfo_GetColumnInfo(command_columns, &pre_columns, &pre_info, &pre_names);
    print_hr("Command IColumnsInfo::GetColumnInfo", hr);
    if (FAILED(hr))
    {
        IColumnsInfo_Release(command_columns);
        ICommandText_Release(command);
        CoUninitialize();
        return 1;
    }
    printf("pre-exec columns=%lu\n", (unsigned long)pre_columns);
    CoTaskMemFree(pre_info);
    pre_info = NULL;
    CoTaskMemFree(pre_names);
    pre_names = NULL;
    IColumnsInfo_Release(command_columns);
    command_columns = NULL;

    hr = ICommandText_Execute(command, NULL, &IID_IRowset, NULL, &affected, (IUnknown**)&rowset);
    print_hr("ICommandText::Execute(IRowset)", hr);
    if (FAILED(hr))
    {
        ICommandText_Release(command);
        CoUninitialize();
        return 1;
    }

    hr = IRowset_QueryInterface(rowset, &IID_IColumnsInfo, (void**)&columns);
    print_hr("QueryInterface(IColumnsInfo)", hr);
    if (FAILED(hr))
    {
        IRowset_Release(rowset);
        ICommandText_Release(command);
        CoUninitialize();
        return 1;
    }

    hr = IRowset_QueryInterface(rowset, &IID_IAccessor, (void**)&accessor);
    print_hr("QueryInterface(IAccessor)", hr);
    if (FAILED(hr))
    {
        IColumnsInfo_Release(columns);
        IRowset_Release(rowset);
        ICommandText_Release(command);
        CoUninitialize();
        return 1;
    }

    hr = IColumnsInfo_GetColumnInfo(columns, &c_columns, &info, &names);
    print_hr("IColumnsInfo::GetColumnInfo", hr);
    if (FAILED(hr))
    {
        IAccessor_Release(accessor);
        IColumnsInfo_Release(columns);
        IRowset_Release(rowset);
        ICommandText_Release(command);
        CoUninitialize();
        return 1;
    }

    bindings = (DBBINDING*)CoTaskMemAlloc(sizeof(DBBINDING) * c_columns);
    bind_status = (DBBINDSTATUS*)CoTaskMemAlloc(sizeof(DBBINDSTATUS) * c_columns);
    if (!bindings || !bind_status)
    {
        hr = E_OUTOFMEMORY;
        print_hr("Allocate bindings", hr);
        goto cleanup;
    }
    ZeroMemory(bindings, sizeof(DBBINDING) * c_columns);

    for (i = 0; i < c_columns; ++i)
    {
        DBLENGTH value_size = buffer_size_for_column(&info[i]);
        row_size = align_offset(row_size, sizeof(void*));
        bindings[i].iOrdinal = info[i].iOrdinal;
        bindings[i].obStatus = row_size;
        row_size += sizeof(DBSTATUS);
        row_size = align_offset(row_size, sizeof(DBLENGTH));
        bindings[i].obLength = row_size;
        row_size += sizeof(DBLENGTH);
        row_size = align_offset(row_size, sizeof(void*));
        bindings[i].obValue = row_size;
        row_size += value_size;
        bindings[i].dwPart = DBPART_STATUS | DBPART_LENGTH | DBPART_VALUE;
        bindings[i].dwMemOwner = DBMEMOWNER_CLIENTOWNED;
        bindings[i].eParamIO = DBPARAMIO_NOTPARAM;
        bindings[i].cbMaxLen = value_size;
        bindings[i].wType = info[i].wType;
        bindings[i].bPrecision = info[i].bPrecision;
        bindings[i].bScale = info[i].bScale;
    }

    row_buffer = (BYTE*)CoTaskMemAlloc(row_size);
    if (!row_buffer)
    {
        hr = E_OUTOFMEMORY;
        print_hr("Allocate row buffer", hr);
        goto cleanup;
    }

    hr = IAccessor_CreateAccessor(
        accessor,
        DBACCESSOR_ROWDATA,
        c_columns,
        bindings,
        row_size,
        &h_accessor,
        bind_status);
    print_hr("IAccessor::CreateAccessor", hr);
    if (FAILED(hr))
    {
        goto cleanup;
    }

    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&start);

    while (!finished)
    {
        HRESULT fetch_hr = IRowset_GetNextRows(rowset, DB_NULL_HCHAPTER, 0, 256, &fetched, &rows);
        if (FAILED(fetch_hr))
        {
            print_hr("IRowset::GetNextRows", fetch_hr);
            hr = fetch_hr;
            goto cleanup;
        }

        for (i = 0; i < fetched; ++i)
        {
            ZeroMemory(row_buffer, row_size);
            hr = IRowset_GetData(rowset, rows[i], h_accessor, row_buffer);
            if (FAILED(hr))
            {
                print_hr("IRowset::GetData", hr);
                goto cleanup;
            }

            ++total_rows;
            if (total_rows <= 3)
            {
                printf("row[%llu] ", (unsigned long long)total_rows);
                print_first_value(&info[0], row_buffer, &bindings[0]);
                printf("\n");
            }
        }

        if (rows)
        {
            IRowset_ReleaseRows(rowset, fetched, rows, NULL, NULL, NULL);
            CoTaskMemFree(rows);
            rows = NULL;
        }

        if (fetch_hr == DB_S_ENDOFROWSET || fetched == 0)
        {
            finished = TRUE;
        }
    }

    QueryPerformanceCounter(&stop);
    elapsed_us = (ULONGLONG)((stop.QuadPart - start.QuadPart) * 1000000ULL / freq.QuadPart);
    printf("rows=%llu elapsed_us=%llu rows_per_sec=%llu\n",
        (unsigned long long)total_rows,
        (unsigned long long)elapsed_us,
        elapsed_us ? (unsigned long long)((total_rows * 1000000ULL) / elapsed_us) : 0ULL);

cleanup:
    if (rows)
    {
        IRowset_ReleaseRows(rowset, fetched, rows, NULL, NULL, NULL);
        CoTaskMemFree(rows);
    }
    if (h_accessor)
    {
        IAccessor_ReleaseAccessor(accessor, h_accessor, NULL);
    }
    if (row_buffer)
    {
        CoTaskMemFree(row_buffer);
    }
    if (bindings)
    {
        CoTaskMemFree(bindings);
    }
    if (bind_status)
    {
        CoTaskMemFree(bind_status);
    }
    if (info)
    {
        CoTaskMemFree(info);
    }
    if (names)
    {
        CoTaskMemFree(names);
    }
    if (accessor)
    {
        IAccessor_Release(accessor);
    }
    if (columns)
    {
        IColumnsInfo_Release(columns);
    }
    if (rowset)
    {
        IRowset_Release(rowset);
    }
    if (command)
    {
        ICommandText_Release(command);
    }
    if (pre_info)
    {
        CoTaskMemFree(pre_info);
    }
    if (pre_names)
    {
        CoTaskMemFree(pre_names);
    }
    if (command_columns)
    {
        IColumnsInfo_Release(command_columns);
    }

    CoUninitialize();
    return FAILED(hr) ? 1 : 0;
}
