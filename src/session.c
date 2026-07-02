#include "monetdb_oledb.h"

static MonetSession* Session_FromOpenRowset(IOpenRowset* iface)
{
    return CONTAINING_RECORD(iface, MonetSession, IOpenRowset_iface);
}

static MonetSession* Session_FromInnerUnknown(IUnknown* iface)
{
    return CONTAINING_RECORD(iface, MonetSession, IUnknown_inner_iface);
}

static MonetSession* Session_FromGetDataSource(IGetDataSource* iface)
{
    return CONTAINING_RECORD(iface, MonetSession, IGetDataSource_iface);
}

static MonetSession* Session_FromCreateCommand(IDBCreateCommand* iface)
{
    return CONTAINING_RECORD(iface, MonetSession, IDBCreateCommand_iface);
}

static MonetSession* Session_FromSchema(IDBSchemaRowset* iface)
{
    return CONTAINING_RECORD(iface, MonetSession, IDBSchemaRowset_iface);
}

static MonetSession* Session_FromProps(ISessionProperties* iface)
{
    return CONTAINING_RECORD(iface, MonetSession, ISessionProperties_iface);
}

static MonetSession* Session_FromJoin(ITransactionJoin* iface)
{
    return CONTAINING_RECORD(iface, MonetSession, ITransactionJoin_iface);
}

static MonetSession* Session_FromLocal(ITransactionLocal* iface)
{
    return CONTAINING_RECORD(iface, MonetSession, ITransactionLocal_iface);
}

static MonetSession* Session_FromSupportErrorInfo(ISupportErrorInfo* iface)
{
    return CONTAINING_RECORD(iface, MonetSession, ISupportErrorInfo_iface);
}

static void Session_FormatGuidA(REFGUID guid, CHAR* buffer, size_t cch_buffer)
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

static HRESULT Session_QueryInterfaceInternal(MonetSession* self, REFIID riid, void** ppv)
{
    if (!ppv)
    {
        return E_POINTER;
    }

    *ppv = NULL;
    if (IsEqualIID(riid, &IID_IUnknown))
    {
        *ppv = &self->IUnknown_inner_iface;
    }
    else if (IsEqualIID(riid, &IID_IOpenRowset))
    {
        *ppv = &self->IOpenRowset_iface;
    }
    else if (IsEqualIID(riid, &IID_IGetDataSource))
    {
        *ppv = &self->IGetDataSource_iface;
    }
    else if (IsEqualIID(riid, &IID_IDBCreateCommand))
    {
        *ppv = &self->IDBCreateCommand_iface;
    }
    else if (IsEqualIID(riid, &IID_IDBSchemaRowset))
    {
        *ppv = &self->IDBSchemaRowset_iface;
    }
    else if (IsEqualIID(riid, &IID_ISessionProperties))
    {
        *ppv = &self->ISessionProperties_iface;
    }
    else if (IsEqualIID(riid, &IID_ITransactionJoin))
    {
        *ppv = &self->ITransactionJoin_iface;
    }
    else if (IsEqualIID(riid, &IID_ITransactionLocal) || IsEqualIID(riid, &IID_ITransaction))
    {
        *ppv = &self->ITransactionLocal_iface;
    }
    else if (IsEqualIID(riid, &IID_ISupportErrorInfo))
    {
        *ppv = &self->ISupportErrorInfo_iface;
    }
    else
    {
        CHAR iid_text[64];
        Session_FormatGuidA(riid, iid_text, MONET_ARRAY_SIZE(iid_text));
        Log_WriteA(MONET_LOG_DEBUG, "Session::QueryInterface", "IID non supportato full=%s", iid_text);
        return E_NOINTERFACE;
    }

    InterlockedIncrement(&self->ref_count);
    if (Config_IsTraceEnabled())
    {
        CHAR iid_text[64];
        Session_FormatGuidA(riid, iid_text, MONET_ARRAY_SIZE(iid_text));
        Log_WriteA(MONET_LOG_TRACE, "Session::QueryInterface", "IID supportato full=%s ref=%ld", iid_text, self->ref_count);
    }
    return S_OK;
}

static ULONG Session_AddRefInternal(MonetSession* self)
{
    return (ULONG)InterlockedIncrement(&self->ref_count);
}

static ULONG Session_ReleaseInternal(MonetSession* self)
{
    ULONG ref = (ULONG)InterlockedDecrement(&self->ref_count);
    if (ref == 0)
    {
        if (self->datasource)
        {
            self->datasource->IDBInitialize_iface.lpVtbl->Release(&self->datasource->IDBInitialize_iface);
        }
        CoTaskMemFree(self);
        Monet_ObjectRelease();
    }
    return ref;
}

static HRESULT Session_QueryInterface_InnerUnknown(IUnknown* iface, REFIID riid, void** ppv) { return Session_QueryInterfaceInternal(Session_FromInnerUnknown(iface), riid, ppv); }
static ULONG Session_AddRef_InnerUnknown(IUnknown* iface) { return Session_AddRefInternal(Session_FromInnerUnknown(iface)); }
static ULONG Session_Release_InnerUnknown(IUnknown* iface) { return Session_ReleaseInternal(Session_FromInnerUnknown(iface)); }
static HRESULT Session_QueryInterface_OpenRowset(IOpenRowset* iface, REFIID riid, void** ppv) { return Session_QueryInterfaceInternal(Session_FromOpenRowset(iface), riid, ppv); }
static ULONG Session_AddRef_OpenRowset(IOpenRowset* iface) { return Session_AddRefInternal(Session_FromOpenRowset(iface)); }
static ULONG Session_Release_OpenRowset(IOpenRowset* iface) { return Session_ReleaseInternal(Session_FromOpenRowset(iface)); }
static HRESULT Session_QueryInterface_GetDataSource(IGetDataSource* iface, REFIID riid, void** ppv) { return Session_QueryInterfaceInternal(Session_FromGetDataSource(iface), riid, ppv); }
static ULONG Session_AddRef_GetDataSource(IGetDataSource* iface) { return Session_AddRefInternal(Session_FromGetDataSource(iface)); }
static ULONG Session_Release_GetDataSource(IGetDataSource* iface) { return Session_ReleaseInternal(Session_FromGetDataSource(iface)); }
static HRESULT Session_QueryInterface_CreateCommand(IDBCreateCommand* iface, REFIID riid, void** ppv) { return Session_QueryInterfaceInternal(Session_FromCreateCommand(iface), riid, ppv); }
static ULONG Session_AddRef_CreateCommand(IDBCreateCommand* iface) { return Session_AddRefInternal(Session_FromCreateCommand(iface)); }
static ULONG Session_Release_CreateCommand(IDBCreateCommand* iface) { return Session_ReleaseInternal(Session_FromCreateCommand(iface)); }
static HRESULT Session_QueryInterface_Schema(IDBSchemaRowset* iface, REFIID riid, void** ppv) { return Session_QueryInterfaceInternal(Session_FromSchema(iface), riid, ppv); }
static ULONG Session_AddRef_Schema(IDBSchemaRowset* iface) { return Session_AddRefInternal(Session_FromSchema(iface)); }
static ULONG Session_Release_Schema(IDBSchemaRowset* iface) { return Session_ReleaseInternal(Session_FromSchema(iface)); }
static HRESULT Session_QueryInterface_Props(ISessionProperties* iface, REFIID riid, void** ppv) { return Session_QueryInterfaceInternal(Session_FromProps(iface), riid, ppv); }
static ULONG Session_AddRef_Props(ISessionProperties* iface) { return Session_AddRefInternal(Session_FromProps(iface)); }
static ULONG Session_Release_Props(ISessionProperties* iface) { return Session_ReleaseInternal(Session_FromProps(iface)); }
static HRESULT Session_QueryInterface_Join(ITransactionJoin* iface, REFIID riid, void** ppv) { return Session_QueryInterfaceInternal(Session_FromJoin(iface), riid, ppv); }
static ULONG Session_AddRef_Join(ITransactionJoin* iface) { return Session_AddRefInternal(Session_FromJoin(iface)); }
static ULONG Session_Release_Join(ITransactionJoin* iface) { return Session_ReleaseInternal(Session_FromJoin(iface)); }
static HRESULT Session_QueryInterface_Local(ITransactionLocal* iface, REFIID riid, void** ppv) { return Session_QueryInterfaceInternal(Session_FromLocal(iface), riid, ppv); }
static ULONG Session_AddRef_Local(ITransactionLocal* iface) { return Session_AddRefInternal(Session_FromLocal(iface)); }
static ULONG Session_Release_Local(ITransactionLocal* iface) { return Session_ReleaseInternal(Session_FromLocal(iface)); }
static HRESULT Session_QueryInterface_SupportErrorInfo(ISupportErrorInfo* iface, REFIID riid, void** ppv) { return Session_QueryInterfaceInternal(Session_FromSupportErrorInfo(iface), riid, ppv); }
static ULONG Session_AddRef_SupportErrorInfo(ISupportErrorInfo* iface) { return Session_AddRefInternal(Session_FromSupportErrorInfo(iface)); }
static ULONG Session_Release_SupportErrorInfo(ISupportErrorInfo* iface) { return Session_ReleaseInternal(Session_FromSupportErrorInfo(iface)); }

static HRESULT STDMETHODCALLTYPE Session_InterfaceSupportsErrorInfo(ISupportErrorInfo* iface, REFIID riid)
{
    MonetSession* self = Session_FromSupportErrorInfo(iface);
    MONET_UNUSED(self);

    if (IsEqualIID(riid, &IID_IOpenRowset) ||
        IsEqualIID(riid, &IID_IGetDataSource) ||
        IsEqualIID(riid, &IID_IDBCreateCommand) ||
        IsEqualIID(riid, &IID_IDBSchemaRowset) ||
        IsEqualIID(riid, &IID_ISessionProperties) ||
        IsEqualIID(riid, &IID_ITransactionJoin) ||
        IsEqualIID(riid, &IID_ITransactionLocal) ||
        IsEqualIID(riid, &IID_ITransaction))
    {
        return S_OK;
    }

    return S_FALSE;
}

static HRESULT STDMETHODCALLTYPE Session_OpenRowset(IOpenRowset* iface, IUnknown* outer, DBID* table_id, DBID* index_id, REFIID riid, ULONG c_property_sets, DBPROPSET rg_property_sets[], IUnknown** pp_rowset)
{
    MonetSession* self = Session_FromOpenRowset(iface);
    CHAR schema_name[MONETDB_MAX_NAME];
    CHAR table_name[MONETDB_MAX_NAME];
    CHAR sql[1024];
    SQLHSTMT hstmt = SQL_NULL_HSTMT;
    HRESULT hr;

    MONET_UNUSED(index_id);
    MONET_UNUSED(c_property_sets);
    MONET_UNUSED(rg_property_sets);

    if (outer != NULL)
    {
        return DB_E_NOAGGREGATION;
    }
    if (!table_id || !pp_rowset)
    {
        return E_POINTER;
    }

    schema_name[0] = '\0';
    table_name[0] = '\0';
    if (table_id->eKind == DBKIND_NAME && table_id->uName.pwszName)
    {
        CHAR full_name[MONETDB_MAX_NAME * 2];
        CHAR* dot = NULL;
        Monet_WideToAnsi(full_name, MONET_ARRAY_SIZE(full_name), table_id->uName.pwszName);
        dot = strchr(full_name, '.');
        if (dot)
        {
            *dot = '\0';
            Monet_StringCopyA(schema_name, MONET_ARRAY_SIZE(schema_name), full_name);
            Monet_StringCopyA(table_name, MONET_ARRAY_SIZE(table_name), dot + 1);
        }
        else
        {
            Monet_StringCopyA(schema_name, MONET_ARRAY_SIZE(schema_name), self->datasource->config.schema);
            Monet_StringCopyA(table_name, MONET_ARRAY_SIZE(table_name), full_name);
        }
    }

    _snprintf(sql, MONET_ARRAY_SIZE(sql), "SELECT * FROM \"%s\".\"%s\"", schema_name, table_name);
    sql[MONET_ARRAY_SIZE(sql) - 1] = '\0';
    hr = Odbc_ExecDirectA(self->datasource->hdbc, sql, &hstmt);
    if (FAILED(hr))
    {
        return hr;
    }

    return Rowset_Create(self, NULL, hstmt, NULL, riid, (void**)pp_rowset);
}

static HRESULT STDMETHODCALLTYPE Session_GetDataSource(IGetDataSource* iface, REFIID riid, IUnknown** pp_ds)
{
    MonetSession* self = Session_FromGetDataSource(iface);
    return self->datasource->IDBInitialize_iface.lpVtbl->QueryInterface(&self->datasource->IDBInitialize_iface, riid, (void**)pp_ds);
}

static HRESULT STDMETHODCALLTYPE Session_CreateCommand(IDBCreateCommand* iface, IUnknown* outer, REFIID riid, IUnknown** pp_command)
{
    MonetSession* self = Session_FromCreateCommand(iface);
    if (outer != NULL)
    {
        return DB_E_NOAGGREGATION;
    }
    return Command_Create(self, riid, (void**)pp_command);
}

static HRESULT STDMETHODCALLTYPE Session_GetRowset(IDBSchemaRowset* iface, IUnknown* outer, REFGUID schema, ULONG c_restrictions, const VARIANT rg_restrictions[], REFIID riid, ULONG c_property_sets, DBPROPSET rg_property_sets[], IUnknown** pp_rowset)
{
    MONET_UNUSED(c_property_sets);
    MONET_UNUSED(rg_property_sets);
    if (outer != NULL)
    {
        return DB_E_NOAGGREGATION;
    }
    return Schema_CreateRowset(Session_FromSchema(iface), schema, c_restrictions, rg_restrictions, riid, (void**)pp_rowset);
}

static HRESULT STDMETHODCALLTYPE Session_GetSchemas(IDBSchemaRowset* iface, ULONG* pc_schemas, GUID** prg_schemas, ULONG** prg_restrictions)
{
    MONET_UNUSED(iface);
    return Schema_GetSupported(pc_schemas, prg_schemas, prg_restrictions);
}

static HRESULT STDMETHODCALLTYPE Session_GetProperties(ISessionProperties* iface, ULONG c_sets, const DBPROPIDSET rg_sets[], ULONG* pc_sets, DBPROPSET** prg_sets)
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

static HRESULT STDMETHODCALLTYPE Session_SetProperties(ISessionProperties* iface, ULONG c_sets, DBPROPSET rg_sets[])
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

static HRESULT STDMETHODCALLTYPE Session_JoinGetOptions(ITransactionJoin* iface, ITransactionOptions** pp_options)
{
    MONET_UNUSED(iface);
    if (pp_options)
    {
        *pp_options = NULL;
    }
    return XACT_E_NOTSUPPORTED;
}

static HRESULT STDMETHODCALLTYPE Session_JoinTransaction(ITransactionJoin* iface, IUnknown* punk_transaction_coord, ISOLEVEL iso_level, ULONG iso_flags, ITransactionOptions* other_options)
{
    MONET_UNUSED(iface);
    MONET_UNUSED(punk_transaction_coord);
    MONET_UNUSED(iso_level);
    MONET_UNUSED(iso_flags);
    MONET_UNUSED(other_options);
    return XACT_E_NOTSUPPORTED;
}

static HRESULT STDMETHODCALLTYPE Session_Abort(ITransactionLocal* iface, BOID* reason, BOOL retaining, BOOL async)
{
    MonetSession* self = Session_FromLocal(iface);
    SQLRETURN rc;
    MONET_UNUSED(reason);
    MONET_UNUSED(async);

    if (self->transaction_level <= 0)
    {
        return XACT_E_NOTRANSACTION;
    }

    rc = SQLEndTran(SQL_HANDLE_DBC, self->datasource->hdbc, SQL_ROLLBACK);
    if (!SQL_SUCCEEDED(rc))
    {
        return E_FAIL;
    }

    if (!retaining)
    {
        SQLSetConnectAttr(self->datasource->hdbc, SQL_ATTR_AUTOCOMMIT, (SQLPOINTER)(INT_PTR)SQL_AUTOCOMMIT_ON, 0);
        self->transaction_level = 0;
    }
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE Session_Commit(ITransactionLocal* iface, BOOL retaining, DWORD grf_tc, DWORD grf_rm)
{
    MonetSession* self = Session_FromLocal(iface);
    SQLRETURN rc;
    MONET_UNUSED(grf_tc);
    MONET_UNUSED(grf_rm);

    if (self->transaction_level <= 0)
    {
        return XACT_E_NOTRANSACTION;
    }

    rc = SQLEndTran(SQL_HANDLE_DBC, self->datasource->hdbc, SQL_COMMIT);
    if (!SQL_SUCCEEDED(rc))
    {
        return E_FAIL;
    }

    if (!retaining)
    {
        SQLSetConnectAttr(self->datasource->hdbc, SQL_ATTR_AUTOCOMMIT, (SQLPOINTER)(INT_PTR)SQL_AUTOCOMMIT_ON, 0);
        self->transaction_level = 0;
    }
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE Session_GetTransactionInfo(ITransactionLocal* iface, XACTTRANSINFO* p_info)
{
    MONET_UNUSED(iface);
    if (!p_info)
    {
        return E_POINTER;
    }
    ZeroMemory(p_info, sizeof(*p_info));
    p_info->isoLevel = ISOLATIONLEVEL_READCOMMITTED;
    p_info->isoFlags = 0;
    p_info->grfTCSupported = XACTTC_SYNC;
    p_info->grfRMSupported = 0;
    p_info->grfTCSupportedRetaining = 0;
    p_info->grfRMSupportedRetaining = 0;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE Session_LocalGetOptions(ITransactionLocal* iface, ITransactionOptions** pp_options)
{
    MONET_UNUSED(iface);
    if (pp_options)
    {
        *pp_options = NULL;
    }
    return XACT_E_NOTSUPPORTED;
}

static HRESULT STDMETHODCALLTYPE Session_StartTransaction(ITransactionLocal* iface, ISOLEVEL iso_level, ULONG iso_flags, ITransactionOptions* other_options, ULONG* p_level)
{
    MonetSession* self = Session_FromLocal(iface);
    SQLRETURN rc;
    MONET_UNUSED(iso_flags);
    MONET_UNUSED(other_options);

    if (p_level)
    {
        *p_level = 0;
    }

    if (self->transaction_level > 0)
    {
        return XACT_E_XTIONEXISTS;
    }

    if (iso_level != ISOLATIONLEVEL_READCOMMITTED && iso_level != ISOLATIONLEVEL_UNSPECIFIED)
    {
        return XACT_E_ISOLATIONLEVEL;
    }

    rc = SQLSetConnectAttr(self->datasource->hdbc, SQL_ATTR_AUTOCOMMIT, (SQLPOINTER)(INT_PTR)SQL_AUTOCOMMIT_OFF, 0);
    if (!SQL_SUCCEEDED(rc))
    {
        return E_FAIL;
    }

    self->transaction_level = 1;
    if (p_level)
    {
        *p_level = 1;
    }
    return S_OK;
}

static IUnknownVtbl g_session_unknown_inner_vtbl =
{
    Session_QueryInterface_InnerUnknown,
    Session_AddRef_InnerUnknown,
    Session_Release_InnerUnknown
};

static IOpenRowsetVtbl g_session_openrowset_vtbl =
{
    Session_QueryInterface_OpenRowset,
    Session_AddRef_OpenRowset,
    Session_Release_OpenRowset,
    Session_OpenRowset
};

static IGetDataSourceVtbl g_session_getdatasource_vtbl =
{
    Session_QueryInterface_GetDataSource,
    Session_AddRef_GetDataSource,
    Session_Release_GetDataSource,
    Session_GetDataSource
};

static IDBCreateCommandVtbl g_session_createcommand_vtbl =
{
    Session_QueryInterface_CreateCommand,
    Session_AddRef_CreateCommand,
    Session_Release_CreateCommand,
    Session_CreateCommand
};

static IDBSchemaRowsetVtbl g_session_schema_vtbl =
{
    Session_QueryInterface_Schema,
    Session_AddRef_Schema,
    Session_Release_Schema,
    Session_GetRowset,
    Session_GetSchemas
};

static ISessionPropertiesVtbl g_session_props_vtbl =
{
    Session_QueryInterface_Props,
    Session_AddRef_Props,
    Session_Release_Props,
    Session_GetProperties,
    Session_SetProperties
};

static ITransactionJoinVtbl g_session_join_vtbl =
{
    Session_QueryInterface_Join,
    Session_AddRef_Join,
    Session_Release_Join,
    Session_JoinGetOptions,
    Session_JoinTransaction
};

static ITransactionLocalVtbl g_session_local_vtbl =
{
    Session_QueryInterface_Local,
    Session_AddRef_Local,
    Session_Release_Local,
    Session_Commit,
    Session_Abort,
    Session_GetTransactionInfo,
    Session_LocalGetOptions,
    Session_StartTransaction
};

static ISupportErrorInfoVtbl g_session_support_error_info_vtbl =
{
    Session_QueryInterface_SupportErrorInfo,
    Session_AddRef_SupportErrorInfo,
    Session_Release_SupportErrorInfo,
    Session_InterfaceSupportsErrorInfo
};

HRESULT Session_Create(MonetDataSource* ds, IUnknown* outer, REFIID riid, void** ppv)
{
    MonetSession* self = NULL;
    HRESULT hr;
    CHAR iid_text[64];

    if (!ds || !ppv)
    {
        return E_POINTER;
    }

    *ppv = NULL;
    if (outer != NULL && !IsEqualIID(riid, &IID_IUnknown))
    {
        Log_WriteA(MONET_LOG_DEBUG, "Session::Create", "richiesta aggregata con IID non-IUnknown");
        return CLASS_E_NOAGGREGATION;
    }
    Session_FormatGuidA(riid, iid_text, MONET_ARRAY_SIZE(iid_text));
    MONET_TRACE("Session::Create", "enter outer=%p riid=%s", outer, iid_text);
    self = (MonetSession*)CoTaskMemAlloc(sizeof(*self));
    if (!self)
    {
        return E_OUTOFMEMORY;
    }
    ZeroMemory(self, sizeof(*self));

    self->IUnknown_inner_iface.lpVtbl = &g_session_unknown_inner_vtbl;
    self->IOpenRowset_iface.lpVtbl = &g_session_openrowset_vtbl;
    self->IGetDataSource_iface.lpVtbl = &g_session_getdatasource_vtbl;
    self->IDBCreateCommand_iface.lpVtbl = &g_session_createcommand_vtbl;
    self->IDBSchemaRowset_iface.lpVtbl = &g_session_schema_vtbl;
    self->ISessionProperties_iface.lpVtbl = &g_session_props_vtbl;
    self->ITransactionJoin_iface.lpVtbl = &g_session_join_vtbl;
    self->ITransactionLocal_iface.lpVtbl = &g_session_local_vtbl;
    self->ISupportErrorInfo_iface.lpVtbl = &g_session_support_error_info_vtbl;
    self->outer_unknown = outer ? outer : &self->IUnknown_inner_iface;
    self->ref_count = 1;
    self->datasource = ds;
    ds->IDBInitialize_iface.lpVtbl->AddRef(&ds->IDBInitialize_iface);
    Monet_ObjectAddRef();

    hr = IUnknown_QueryInterface(&self->IUnknown_inner_iface, riid, ppv);
    MONET_TRACE("Session::Create", "post-qi hr=0x%08lx", hr);
    Session_ReleaseInternal(self);
    MONET_TRACE("Session::Create", "exit hr=0x%08lx", hr);
    return hr;
}
