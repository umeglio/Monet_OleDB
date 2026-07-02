#include "monetdb_oledb.h"

static MonetCommand* Command_FromText(ICommandText* iface)
{
    return CONTAINING_RECORD(iface, MonetCommand, ICommandText_iface);
}

static MonetCommand* Command_FromProps(ICommandProperties* iface)
{
    return CONTAINING_RECORD(iface, MonetCommand, ICommandProperties_iface);
}

static MonetCommand* Command_FromColumns(IColumnsInfo* iface)
{
    return CONTAINING_RECORD(iface, MonetCommand, IColumnsInfo_iface);
}

static MonetCommand* Command_FromAccessor(IAccessor* iface)
{
    return CONTAINING_RECORD(iface, MonetCommand, IAccessor_iface);
}

static MonetCommand* Command_FromConvert(IConvertType* iface)
{
    return CONTAINING_RECORD(iface, MonetCommand, IConvertType_iface);
}

static MonetCommand* Command_FromPrepare(ICommandPrepare* iface)
{
    return CONTAINING_RECORD(iface, MonetCommand, ICommandPrepare_iface);
}

static MonetCommand* Command_FromSupportErrorInfo(ISupportErrorInfo* iface)
{
    return CONTAINING_RECORD(iface, MonetCommand, ISupportErrorInfo_iface);
}

static HRESULT Command_QueryInterfaceInternal(MonetCommand* self, REFIID riid, void** ppv)
{
    if (!ppv)
    {
        return E_POINTER;
    }

    *ppv = NULL;
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_ICommand) || IsEqualIID(riid, &IID_ICommandText))
    {
        *ppv = &self->ICommandText_iface;
    }
    else if (IsEqualIID(riid, &IID_ICommandProperties))
    {
        *ppv = &self->ICommandProperties_iface;
    }
    else if (IsEqualIID(riid, &IID_IColumnsInfo))
    {
        *ppv = &self->IColumnsInfo_iface;
    }
    else if (IsEqualIID(riid, &IID_IAccessor))
    {
        *ppv = &self->IAccessor_iface;
    }
    else if (IsEqualIID(riid, &IID_IConvertType))
    {
        *ppv = &self->IConvertType_iface;
    }
    else if (IsEqualIID(riid, &IID_ICommandPrepare))
    {
        *ppv = &self->ICommandPrepare_iface;
    }
    else if (IsEqualIID(riid, &IID_ISupportErrorInfo))
    {
        *ppv = &self->ISupportErrorInfo_iface;
    }
    else
    {
        return E_NOINTERFACE;
    }

    InterlockedIncrement(&self->ref_count);
    return S_OK;
}

static ULONG Command_AddRefInternal(MonetCommand* self)
{
    return (ULONG)InterlockedIncrement(&self->ref_count);
}

static void Command_FreeColumns(MonetCommand* self)
{
    if (self->columns)
    {
        CoTaskMemFree(self->columns);
        self->columns = NULL;
        self->column_count = 0;
    }
}

static BOOL Command_ColumnsNeedRuntimeMetadata(const MonetColumnInfo* columns, DBORDINAL column_count)
{
    DBORDINAL i;

    for (i = 0; i < column_count; ++i)
    {
        switch (columns[i].type)
        {
        case DBTYPE_STR:
        case DBTYPE_WSTR:
        case DBTYPE_BYTES:
            if (columns[i].column_size == 0)
            {
                return TRUE;
            }
            break;

        default:
            break;
        }
    }

    return FALSE;
}

static HRESULT Command_RefreshColumnsFromTemporaryExecution(MonetCommand* self, MonetColumnInfo** ppcolumns, DBORDINAL* pccolumns)
{
    SQLHSTMT hstmt = SQL_NULL_HSTMT;
    MonetColumnInfo* refreshed = NULL;
    DBORDINAL refreshed_count = 0;
    SQLRETURN rc;
    HRESULT hr;
    CHAR text[1024];

    if (!self || !ppcolumns || !pccolumns)
    {
        return E_POINTER;
    }

    hr = Odbc_PrepareA(self->session->datasource->hdbc, self->sql_text, &hstmt);
    if (FAILED(hr))
    {
        return hr;
    }

    rc = SQLSetStmtAttr(hstmt, SQL_ATTR_MAX_ROWS, (SQLPOINTER)1, 0);
    MONET_UNUSED(rc);

    rc = SQLExecute(hstmt);
    if (!SQL_SUCCEEDED(rc))
    {
        Odbc_GetErrorMessage(SQL_HANDLE_STMT, hstmt, DB_E_ERRORSINCOMMAND, text, MONET_ARRAY_SIZE(text));
        Log_WriteA(MONET_LOG_ERROR, "Command::MetadataExecute", "%s", text);
        Odbc_CloseStatement(&hstmt);
        return DB_E_ERRORSINCOMMAND;
    }

    hr = Odbc_DescribeColumns(hstmt, &refreshed, &refreshed_count);
    SQLCloseCursor(hstmt);
    Odbc_CloseStatement(&hstmt);
    if (FAILED(hr))
    {
        return hr;
    }

    if (*ppcolumns)
    {
        CoTaskMemFree(*ppcolumns);
    }

    *ppcolumns = refreshed;
    *pccolumns = refreshed_count;
    return S_OK;
}

static HRESULT Command_ExecutePreparedHandle(MonetCommand* self, SQLHSTMT hstmt, const CHAR* scope)
{
    LARGE_INTEGER perf_start = { 0 };
    LARGE_INTEGER perf_end = { 0 };
    LARGE_INTEGER perf_freq = { 0 };
    BOOL have_perf = FALSE;
    ULONGLONG elapsed_us = 0;
    SQLRETURN rc;
    CHAR text[1024];

    if (!self || hstmt == SQL_NULL_HSTMT)
    {
        return E_POINTER;
    }

    if (QueryPerformanceFrequency(&perf_freq))
    {
        QueryPerformanceCounter(&perf_start);
        have_perf = TRUE;
    }

    MONET_TRACE(scope ? scope : "Command::ExecutePreparedHandle", "sql=\"%s\"", self->sql_text);
    Log_WriteQueryA(scope ? scope : "ODBC::Execute", "sql=\"%s\"", self->sql_text);
    rc = SQLExecute(hstmt);
    if (have_perf)
    {
        QueryPerformanceCounter(&perf_end);
        elapsed_us = (ULONGLONG)((perf_end.QuadPart - perf_start.QuadPart) * 1000000ULL / perf_freq.QuadPart);
    }

    if (!SQL_SUCCEEDED(rc))
    {
        Odbc_GetErrorMessage(SQL_HANDLE_STMT, hstmt, DB_E_ERRORSINCOMMAND, text, MONET_ARRAY_SIZE(text));
        Log_WriteA(MONET_LOG_ERROR, scope ? scope : "ODBC::Execute", "%s", text);
        return DB_E_ERRORSINCOMMAND;
    }

    MONET_TRACE(scope ? scope : "ODBC::Execute", "elapsed_us=%llu", (unsigned long long)elapsed_us);
    return S_OK;
}

static ULONG Command_ReleaseInternal(MonetCommand* self)
{
    ULONG ref = (ULONG)InterlockedDecrement(&self->ref_count);
    if (ref == 0)
    {
        Odbc_CloseStatement(&self->prepared_stmt);
        Command_FreeColumns(self);
        AccessorTable_Destroy(&self->accessors);
        if (self->session)
        {
            self->session->IOpenRowset_iface.lpVtbl->Release(&self->session->IOpenRowset_iface);
        }
        CoTaskMemFree(self);
        Monet_ObjectRelease();
    }
    return ref;
}

static HRESULT Command_QueryInterface_Text(ICommandText* iface, REFIID riid, void** ppv) { return Command_QueryInterfaceInternal(Command_FromText(iface), riid, ppv); }
static ULONG Command_AddRef_Text(ICommandText* iface) { return Command_AddRefInternal(Command_FromText(iface)); }
static ULONG Command_Release_Text(ICommandText* iface) { return Command_ReleaseInternal(Command_FromText(iface)); }
static HRESULT Command_QueryInterface_Props(ICommandProperties* iface, REFIID riid, void** ppv) { return Command_QueryInterfaceInternal(Command_FromProps(iface), riid, ppv); }
static ULONG Command_AddRef_Props(ICommandProperties* iface) { return Command_AddRefInternal(Command_FromProps(iface)); }
static ULONG Command_Release_Props(ICommandProperties* iface) { return Command_ReleaseInternal(Command_FromProps(iface)); }
static HRESULT Command_QueryInterface_Columns(IColumnsInfo* iface, REFIID riid, void** ppv) { return Command_QueryInterfaceInternal(Command_FromColumns(iface), riid, ppv); }
static ULONG Command_AddRef_Columns(IColumnsInfo* iface) { return Command_AddRefInternal(Command_FromColumns(iface)); }
static ULONG Command_Release_Columns(IColumnsInfo* iface) { return Command_ReleaseInternal(Command_FromColumns(iface)); }
static HRESULT Command_QueryInterface_Accessor(IAccessor* iface, REFIID riid, void** ppv) { return Command_QueryInterfaceInternal(Command_FromAccessor(iface), riid, ppv); }
static ULONG Command_AddRef_Accessor(IAccessor* iface) { return Command_AddRefInternal(Command_FromAccessor(iface)); }
static ULONG Command_Release_Accessor(IAccessor* iface) { return Command_ReleaseInternal(Command_FromAccessor(iface)); }
static HRESULT Command_QueryInterface_Convert(IConvertType* iface, REFIID riid, void** ppv) { return Command_QueryInterfaceInternal(Command_FromConvert(iface), riid, ppv); }
static ULONG Command_AddRef_Convert(IConvertType* iface) { return Command_AddRefInternal(Command_FromConvert(iface)); }
static ULONG Command_Release_Convert(IConvertType* iface) { return Command_ReleaseInternal(Command_FromConvert(iface)); }
static HRESULT Command_QueryInterface_Prepare(ICommandPrepare* iface, REFIID riid, void** ppv) { return Command_QueryInterfaceInternal(Command_FromPrepare(iface), riid, ppv); }
static ULONG Command_AddRef_Prepare(ICommandPrepare* iface) { return Command_AddRefInternal(Command_FromPrepare(iface)); }
static ULONG Command_Release_Prepare(ICommandPrepare* iface) { return Command_ReleaseInternal(Command_FromPrepare(iface)); }
static HRESULT Command_QueryInterface_SupportErrorInfo(ISupportErrorInfo* iface, REFIID riid, void** ppv) { return Command_QueryInterfaceInternal(Command_FromSupportErrorInfo(iface), riid, ppv); }
static ULONG Command_AddRef_SupportErrorInfo(ISupportErrorInfo* iface) { return Command_AddRefInternal(Command_FromSupportErrorInfo(iface)); }
static ULONG Command_Release_SupportErrorInfo(ISupportErrorInfo* iface) { return Command_ReleaseInternal(Command_FromSupportErrorInfo(iface)); }

static HRESULT STDMETHODCALLTYPE Command_InterfaceSupportsErrorInfo(ISupportErrorInfo* iface, REFIID riid)
{
    MonetCommand* self = Command_FromSupportErrorInfo(iface);
    MONET_UNUSED(self);

    if (IsEqualIID(riid, &IID_ICommand) ||
        IsEqualIID(riid, &IID_ICommandText) ||
        IsEqualIID(riid, &IID_ICommandProperties) ||
        IsEqualIID(riid, &IID_IColumnsInfo) ||
        IsEqualIID(riid, &IID_IAccessor) ||
        IsEqualIID(riid, &IID_IConvertType) ||
        IsEqualIID(riid, &IID_ICommandPrepare))
    {
        return S_OK;
    }

    return S_FALSE;
}

static HRESULT Command_EnsureColumns(MonetCommand* self)
{
    SQLHSTMT hstmt = SQL_NULL_HSTMT;
    HRESULT hr;

    if (self->columns)
    {
        return S_OK;
    }

    if (!self->sql_text[0])
    {
        return DB_E_NOCOMMAND;
    }

    if (self->prepared_stmt != SQL_NULL_HSTMT)
    {
        hr = Odbc_DescribeColumns(self->prepared_stmt, &self->columns, &self->column_count);
        if (FAILED(hr))
        {
            return hr;
        }
        if (Command_ColumnsNeedRuntimeMetadata(self->columns, self->column_count))
        {
            MONET_TRACE("Command::EnsureColumns", "%s", "prepared metadata incomplete, executing temporary statement to refresh column sizes");
            hr = Command_RefreshColumnsFromTemporaryExecution(self, &self->columns, &self->column_count);
        }
        return hr;
    }

    hr = Odbc_PrepareA(self->session->datasource->hdbc, self->sql_text, &hstmt);
    if (FAILED(hr))
    {
        return hr;
    }
    hr = Odbc_DescribeColumns(hstmt, &self->columns, &self->column_count);
    if (SUCCEEDED(hr) && Command_ColumnsNeedRuntimeMetadata(self->columns, self->column_count))
    {
        MONET_TRACE("Command::EnsureColumns", "%s", "temporary prepared metadata incomplete, executing second temporary statement to refresh column sizes");
        hr = Command_RefreshColumnsFromTemporaryExecution(self, &self->columns, &self->column_count);
    }
    if (FAILED(hr))
    {
        Odbc_CloseStatement(&hstmt);
        return hr;
    }

    Odbc_CloseStatement(&self->prepared_stmt);
    self->prepared_stmt = hstmt;
    MONET_TRACE("Command::EnsureColumns", "%s", "cached prepared statement for later execution");
    return S_OK;
}

static HRESULT Command_BuildColumnInfo(MonetCommand* self, DBORDINAL* pc_columns, DBCOLUMNINFO** prg_info, OLECHAR** pp_buffer)
{
    size_t buffer_chars = 1;
    DBCOLUMNINFO* info = NULL;
    OLECHAR* buffer = NULL;
    OLECHAR* cursor = NULL;
    DBORDINAL i;
    HRESULT hr = Command_EnsureColumns(self);

    if (FAILED(hr))
    {
        return hr;
    }

    if (!pc_columns || !prg_info || !pp_buffer)
    {
        return E_POINTER;
    }

    for (i = 0; i < self->column_count; ++i)
    {
        buffer_chars += wcslen(self->columns[i].name_w) + 1;
    }

    info = (DBCOLUMNINFO*)CoTaskMemAlloc(sizeof(DBCOLUMNINFO) * self->column_count);
    if (!info)
    {
        return E_OUTOFMEMORY;
    }
    ZeroMemory(info, sizeof(DBCOLUMNINFO) * self->column_count);

    buffer = (OLECHAR*)CoTaskMemAlloc(sizeof(OLECHAR) * buffer_chars);
    if (!buffer)
    {
        CoTaskMemFree(info);
        return E_OUTOFMEMORY;
    }
    ZeroMemory(buffer, sizeof(OLECHAR) * buffer_chars);
    cursor = buffer;

    for (i = 0; i < self->column_count; ++i)
    {
        DBCOLUMNINFO* col = &info[i];
        col->iOrdinal = self->columns[i].ordinal;
        col->dwFlags = self->columns[i].flags;
        col->ulColumnSize = self->columns[i].column_size;
        col->wType = self->columns[i].type;
        col->bPrecision = self->columns[i].precision;
        col->bScale = self->columns[i].scale;
        col->pwszName = cursor;
        wcscpy(cursor, self->columns[i].name_w);
        cursor += wcslen(self->columns[i].name_w) + 1;
        ZeroMemory(&col->columnid, sizeof(col->columnid));

        MONET_TRACE(
            "Command::GetColumnInfo",
            "column[%lu] name='%S' wType=0x%04x ulColumnSize=%ld precision=%u scale=%u flags=0x%08lx sql_type=%d sql_size=%llu",
            (unsigned long)i,
            self->columns[i].name_w,
            (unsigned)col->wType,
            (long)col->ulColumnSize,
            (unsigned)col->bPrecision,
            (unsigned)col->bScale,
            (unsigned long)col->dwFlags,
            (int)self->columns[i].sql_type,
            (unsigned long long)self->columns[i].sql_size);
    }

    *pc_columns = self->column_count;
    *prg_info = info;
    *pp_buffer = buffer;
    return S_OK;
}

static BOOL Command_NameEquals(const WCHAR* left, const WCHAR* right)
{
    return (_wcsicmp(left ? left : L"", right ? right : L"") == 0);
}

static HRESULT STDMETHODCALLTYPE Command_Cancel(ICommandText* iface)
{
    MonetCommand* self = Command_FromText(iface);
    if (self->prepared_stmt != SQL_NULL_HSTMT)
    {
        SQLCancel(self->prepared_stmt);
    }
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE Command_Execute(ICommandText* iface, IUnknown* outer, REFIID riid, DBPARAMS* params, DBROWCOUNT* pc_rows_affected, IUnknown** pp_rowset)
{
    MonetCommand* self = Command_FromText(iface);
    SQLHSTMT hstmt = SQL_NULL_HSTMT;
    SQLRETURN rc;
    SQLSMALLINT col_count = 0;
    SQLLEN row_count = 0;
    HRESULT hr;

    if (outer != NULL)
    {
        return DB_E_NOAGGREGATION;
    }
    if (!self->sql_text[0])
    {
        return DB_E_NOCOMMAND;
    }
    if (params && params->cParamSets > 0)
    {
        return DB_E_PARAMNOTOPTIONAL;
    }

    if (pc_rows_affected)
    {
        *pc_rows_affected = 0;
    }
    if (pp_rowset)
    {
        *pp_rowset = NULL;
    }

    if (self->prepared)
    {
        if (self->prepared_stmt != SQL_NULL_HSTMT)
        {
            hstmt = self->prepared_stmt;
            self->prepared_stmt = SQL_NULL_HSTMT;
        }
        else
        {
            hr = Odbc_PrepareA(self->session->datasource->hdbc, self->sql_text, &hstmt);
            if (FAILED(hr))
            {
                return hr;
            }
        }

        hr = Command_ExecutePreparedHandle(self, hstmt, "ODBC::Execute");
        if (FAILED(hr))
        {
            Odbc_CloseStatement(&hstmt);
            return hr;
        }
    }
    else
    {
        if (self->prepared_stmt != SQL_NULL_HSTMT)
        {
            hstmt = self->prepared_stmt;
            self->prepared_stmt = SQL_NULL_HSTMT;
            hr = Command_ExecutePreparedHandle(self, hstmt, "ODBC::Execute");
            if (FAILED(hr))
            {
                Odbc_CloseStatement(&hstmt);
                return hr;
            }
        }
        else
        {
            hr = Odbc_ExecDirectA(self->session->datasource->hdbc, self->sql_text, &hstmt);
            if (FAILED(hr))
            {
                return hr;
            }
        }
    }

    rc = SQLNumResultCols(hstmt, &col_count);
    if (!SQL_SUCCEEDED(rc))
    {
        Odbc_CloseStatement(&hstmt);
        return DB_E_ERRORSINCOMMAND;
    }

    if (col_count > 0)
    {
        if (!pp_rowset)
        {
            Odbc_CloseStatement(&hstmt);
            return DB_S_NORESULT;
        }
    return Rowset_Create(self->session, self, hstmt, NULL, riid, (void**)pp_rowset);
    }

    SQLRowCount(hstmt, &row_count);
    Odbc_CloseStatement(&hstmt);
    if (pc_rows_affected)
    {
        *pc_rows_affected = row_count;
    }
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE Command_GetDBSession(ICommandText* iface, REFIID riid, IUnknown** pp_session)
{
    MonetCommand* self = Command_FromText(iface);
    return self->session->IOpenRowset_iface.lpVtbl->QueryInterface(&self->session->IOpenRowset_iface, riid, (void**)pp_session);
}

static HRESULT STDMETHODCALLTYPE Command_GetCommandText(ICommandText* iface, GUID* pguid_dialect, LPOLESTR* pp_command)
{
    MonetCommand* self = Command_FromText(iface);
    if (!pp_command)
    {
        return E_POINTER;
    }
    if (!self->sql_text[0])
    {
        *pp_command = NULL;
        return DB_E_NOCOMMAND;
    }
    if (pguid_dialect)
    {
        *pguid_dialect = self->dialect;
    }
    return Monet_AllocWideStringFromAnsi(self->sql_text, pp_command);
}

static HRESULT STDMETHODCALLTYPE Command_SetCommandText(ICommandText* iface, REFGUID rguid_dialect, LPCOLESTR command_text)
{
    MonetCommand* self = Command_FromText(iface);
    CHAR temp[MONETDB_MAX_SQL_TEXT];

    if (!(IsEqualGUID(rguid_dialect, &DBGUID_DEFAULT) || IsEqualGUID(rguid_dialect, &DBGUID_SQL)))
    {
        return DB_E_DIALECTNOTSUPPORTED;
    }

    Odbc_CloseStatement(&self->prepared_stmt);
    self->prepared = FALSE;
    Command_FreeColumns(self);
    self->dialect = *rguid_dialect;

    if (!command_text || !command_text[0])
    {
        self->sql_text[0] = '\0';
        return S_OK;
    }

    Monet_WideToAnsi(temp, MONET_ARRAY_SIZE(temp), command_text);
    Monet_StringCopyA(self->sql_text, MONET_ARRAY_SIZE(self->sql_text), temp);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE Command_GetProperties(ICommandProperties* iface, const ULONG c_sets, const DBPROPIDSET rg_sets[], ULONG* pc_sets, DBPROPSET** prg_sets)
{
    MONET_UNUSED(iface);
    MONET_UNUSED(c_sets);
    MONET_UNUSED(rg_sets);
    if (!pc_sets || !prg_sets)
    {
        return E_POINTER;
    }
    *pc_sets = 0;
    *prg_sets = NULL;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE Command_SetProperties(ICommandProperties* iface, ULONG c_sets, DBPROPSET rg_sets[])
{
    ULONG i, j;
    MONET_UNUSED(iface);
    for (i = 0; i < c_sets; ++i)
    {
        for (j = 0; j < rg_sets[i].cProperties; ++j)
        {
            rg_sets[i].rgProperties[j].dwStatus = DBPROPSTATUS_NOTSUPPORTED;
        }
    }
    return c_sets ? DB_S_ERRORSOCCURRED : S_OK;
}

static HRESULT STDMETHODCALLTYPE Command_GetColumnInfo(IColumnsInfo* iface, DBORDINAL* pc_columns, DBCOLUMNINFO** prg_info, OLECHAR** pp_buffer)
{
    return Command_BuildColumnInfo(Command_FromColumns(iface), pc_columns, prg_info, pp_buffer);
}

static HRESULT STDMETHODCALLTYPE Command_MapColumnIDs(IColumnsInfo* iface, DBORDINAL c_column_ids, const DBID rg_column_ids[], DBORDINAL rg_columns[])
{
    MonetCommand* self = Command_FromColumns(iface);
    DBORDINAL i;
    HRESULT hr = Command_EnsureColumns(self);
    if (FAILED(hr))
    {
        return hr;
    }

    if (!rg_column_ids || !rg_columns)
    {
        return E_POINTER;
    }

    for (i = 0; i < c_column_ids; ++i)
    {
        DBORDINAL j;
        rg_columns[i] = 0;
        if (rg_column_ids[i].eKind != DBKIND_NAME || !rg_column_ids[i].uName.pwszName)
        {
            continue;
        }
        for (j = 0; j < self->column_count; ++j)
        {
            if (Command_NameEquals(rg_column_ids[i].uName.pwszName, self->columns[j].name_w))
            {
                rg_columns[i] = self->columns[j].ordinal;
                break;
            }
        }
    }

    return S_OK;
}

static HRESULT STDMETHODCALLTYPE Command_AddRefAccessor(IAccessor* iface, HACCESSOR h_accessor, DBREFCOUNT* pc_refcount)
{
    return AccessorTable_AddRef(&Command_FromAccessor(iface)->accessors, h_accessor, pc_refcount);
}

static HRESULT STDMETHODCALLTYPE Command_CreateAccessor(IAccessor* iface, DBACCESSORFLAGS flags, DBCOUNTITEM c_bindings, const DBBINDING bindings[], DBLENGTH row_size, HACCESSOR* ph_accessor, DBBINDSTATUS rg_status[])
{
    return AccessorTable_Create(&Command_FromAccessor(iface)->accessors, flags, c_bindings, bindings, row_size, ph_accessor, rg_status);
}

static HRESULT STDMETHODCALLTYPE Command_GetBindings(IAccessor* iface, HACCESSOR h_accessor, DBACCESSORFLAGS* pflags, DBCOUNTITEM* pc_bindings, DBBINDING** pp_bindings)
{
    return AccessorTable_GetBindings(&Command_FromAccessor(iface)->accessors, h_accessor, pflags, pc_bindings, pp_bindings);
}

static HRESULT STDMETHODCALLTYPE Command_ReleaseAccessor(IAccessor* iface, HACCESSOR h_accessor, DBREFCOUNT* pc_refcount)
{
    return AccessorTable_Release(&Command_FromAccessor(iface)->accessors, h_accessor, pc_refcount);
}

static BOOL Command_CanConvertTypes(DBTYPE from_type, DBTYPE to_type)
{
    if (from_type == to_type)
    {
        return TRUE;
    }
    if (to_type == DBTYPE_STR || to_type == DBTYPE_WSTR)
    {
        return TRUE;
    }
    if (from_type == DBTYPE_STR || from_type == DBTYPE_WSTR)
    {
        switch (to_type)
        {
        case DBTYPE_I2:
        case DBTYPE_I4:
        case DBTYPE_I8:
        case DBTYPE_UI1:
        case DBTYPE_R4:
        case DBTYPE_R8:
        case DBTYPE_BOOL:
        case DBTYPE_NUMERIC:
        case DBTYPE_DBDATE:
        case DBTYPE_DBTIME:
        case DBTYPE_DBTIMESTAMP:
            return TRUE;
        default:
            return FALSE;
        }
    }
    return FALSE;
}

static HRESULT STDMETHODCALLTYPE Command_CanConvert(IConvertType* iface, DBTYPE from_type, DBTYPE to_type, DBCONVERTFLAGS flags)
{
    MONET_UNUSED(iface);
    MONET_UNUSED(flags);
    return Command_CanConvertTypes(from_type, to_type) ? S_OK : DB_E_UNSUPPORTEDCONVERSION;
}

static HRESULT STDMETHODCALLTYPE Command_Prepare(ICommandPrepare* iface, ULONG expected_runs)
{
    MonetCommand* self = Command_FromPrepare(iface);
    HRESULT hr;
    MONET_UNUSED(expected_runs);

    if (!self->sql_text[0])
    {
        return DB_E_NOCOMMAND;
    }

    Odbc_CloseStatement(&self->prepared_stmt);
    self->prepared = FALSE;
    Command_FreeColumns(self);
    if (FAILED(Odbc_PrepareA(self->session->datasource->hdbc, self->sql_text, &self->prepared_stmt)))
    {
        return DB_E_ERRORSINCOMMAND;
    }
    self->prepared = TRUE;
    if (FAILED(Odbc_DescribeColumns(self->prepared_stmt, &self->columns, &self->column_count)))
    {
        Odbc_CloseStatement(&self->prepared_stmt);
        self->prepared = FALSE;
        return DB_E_ERRORSINCOMMAND;
    }
    if (Command_ColumnsNeedRuntimeMetadata(self->columns, self->column_count))
    {
        MONET_TRACE("Command::Prepare", "%s", "prepared metadata incomplete, executing temporary statement to refresh column sizes");
        hr = Command_RefreshColumnsFromTemporaryExecution(self, &self->columns, &self->column_count);
        if (FAILED(hr))
        {
            Command_FreeColumns(self);
            Odbc_CloseStatement(&self->prepared_stmt);
            self->prepared = FALSE;
        }
        return hr;
    }
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE Command_Unprepare(ICommandPrepare* iface)
{
    MonetCommand* self = Command_FromPrepare(iface);
    Odbc_CloseStatement(&self->prepared_stmt);
    self->prepared = FALSE;
    return S_OK;
}

static ICommandTextVtbl g_command_text_vtbl =
{
    Command_QueryInterface_Text,
    Command_AddRef_Text,
    Command_Release_Text,
    Command_Cancel,
    Command_Execute,
    Command_GetDBSession,
    Command_GetCommandText,
    Command_SetCommandText
};

static ICommandPropertiesVtbl g_command_props_vtbl =
{
    Command_QueryInterface_Props,
    Command_AddRef_Props,
    Command_Release_Props,
    Command_GetProperties,
    Command_SetProperties
};

static IColumnsInfoVtbl g_command_columns_vtbl =
{
    Command_QueryInterface_Columns,
    Command_AddRef_Columns,
    Command_Release_Columns,
    Command_GetColumnInfo,
    Command_MapColumnIDs
};

static IAccessorVtbl g_command_accessor_vtbl =
{
    Command_QueryInterface_Accessor,
    Command_AddRef_Accessor,
    Command_Release_Accessor,
    Command_AddRefAccessor,
    Command_CreateAccessor,
    Command_GetBindings,
    Command_ReleaseAccessor
};

static IConvertTypeVtbl g_command_convert_vtbl =
{
    Command_QueryInterface_Convert,
    Command_AddRef_Convert,
    Command_Release_Convert,
    Command_CanConvert
};

static ICommandPrepareVtbl g_command_prepare_vtbl =
{
    Command_QueryInterface_Prepare,
    Command_AddRef_Prepare,
    Command_Release_Prepare,
    Command_Prepare,
    Command_Unprepare
};

static ISupportErrorInfoVtbl g_command_support_error_info_vtbl =
{
    Command_QueryInterface_SupportErrorInfo,
    Command_AddRef_SupportErrorInfo,
    Command_Release_SupportErrorInfo,
    Command_InterfaceSupportsErrorInfo
};

HRESULT Command_Create(MonetSession* session, REFIID riid, void** ppv)
{
    MonetCommand* self = NULL;
    HRESULT hr;

    if (!session || !ppv)
    {
        return E_POINTER;
    }

    *ppv = NULL;
    self = (MonetCommand*)CoTaskMemAlloc(sizeof(*self));
    if (!self)
    {
        return E_OUTOFMEMORY;
    }
    ZeroMemory(self, sizeof(*self));

    self->ICommandText_iface.lpVtbl = &g_command_text_vtbl;
    self->ICommandProperties_iface.lpVtbl = &g_command_props_vtbl;
    self->IColumnsInfo_iface.lpVtbl = &g_command_columns_vtbl;
    self->IAccessor_iface.lpVtbl = &g_command_accessor_vtbl;
    self->IConvertType_iface.lpVtbl = &g_command_convert_vtbl;
    self->ICommandPrepare_iface.lpVtbl = &g_command_prepare_vtbl;
    self->ISupportErrorInfo_iface.lpVtbl = &g_command_support_error_info_vtbl;
    self->ref_count = 1;
    self->session = session;
    self->dialect = DBGUID_SQL;
    self->prepared_stmt = SQL_NULL_HSTMT;
    AccessorTable_Init(&self->accessors);
    session->IOpenRowset_iface.lpVtbl->AddRef(&session->IOpenRowset_iface);
    Monet_ObjectAddRef();

    hr = Command_QueryInterfaceInternal(self, riid, ppv);
    Command_ReleaseInternal(self);
    return hr;
}
