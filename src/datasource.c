#include "monetdb_oledb.h"

typedef struct MonetPropertyMeta
{
    DBPROPID propid;
    VARTYPE vt_type;
    DWORD flags;
    const WCHAR* description;
} MonetPropertyMeta;

static const MonetPropertyMeta g_init_property_meta[] =
{
    { DBPROP_AUTH_PERSIST_SENSITIVE_AUTHINFO, VT_BOOL, DBPROPFLAGS_DBINIT | DBPROPFLAGS_WRITE, L"Persist sensitive authentication information" },
    { DBPROP_AUTH_USERID, VT_BSTR, DBPROPFLAGS_DBINIT | DBPROPFLAGS_WRITE | DBPROPFLAGS_REQUIRED, L"MonetDB username" },
    { DBPROP_AUTH_PASSWORD, VT_BSTR, DBPROPFLAGS_DBINIT | DBPROPFLAGS_WRITE | DBPROPFLAGS_REQUIRED, L"MonetDB password" },
    { DBPROP_INIT_DATASOURCE, VT_BSTR, DBPROPFLAGS_DBINIT | DBPROPFLAGS_WRITE | DBPROPFLAGS_REQUIRED, L"ODBC DSN name" },
    { DBPROP_INIT_CATALOG, VT_BSTR, DBPROPFLAGS_DBINIT | DBPROPFLAGS_WRITE, L"Initial MonetDB catalog/database" },
    { DBPROP_INIT_TIMEOUT, VT_I4, DBPROPFLAGS_DBINIT | DBPROPFLAGS_WRITE, L"Connection timeout in seconds" },
    { DBPROP_INIT_PROMPT, VT_I4, DBPROPFLAGS_DBINIT | DBPROPFLAGS_WRITE, L"Prompt mode" },
    { DBPROP_INIT_LCID, VT_I4, DBPROPFLAGS_DBINIT | DBPROPFLAGS_WRITE, L"Locale identifier" },
    { DBPROP_INIT_MODE, VT_I4, DBPROPFLAGS_DBINIT | DBPROPFLAGS_WRITE, L"Access mode" },
    { DBPROP_INIT_PROVIDERSTRING, VT_BSTR, DBPROPFLAGS_DBINIT | DBPROPFLAGS_WRITE, L"Provider-specific initialization string" },
    { DBPROP_INIT_OLEDBSERVICES, VT_I4, DBPROPFLAGS_DBINIT | DBPROPFLAGS_WRITE, L"OLE DB services bitmask" }
};

static GUID DS_NormalizePropertySet(REFGUID guid)
{
    if (Monet_IsEqualPropertySet(guid, &DBPROPSET_DBINITALL))
    {
        return DBPROPSET_DBINIT;
    }
    return *guid;
}

static const WCHAR* DS_GetPropertyDescription(DBPROPID propid)
{
    size_t i;
    for (i = 0; i < MONET_ARRAY_SIZE(g_init_property_meta); ++i)
    {
        if (g_init_property_meta[i].propid == propid)
        {
            return g_init_property_meta[i].description;
        }
    }
    return L"MonetDB property";
}

static const MonetPropertyMeta* DS_FindPropertyMeta(DBPROPID propid)
{
    size_t i;
    for (i = 0; i < MONET_ARRAY_SIZE(g_init_property_meta); ++i)
    {
        if (g_init_property_meta[i].propid == propid)
        {
            return &g_init_property_meta[i];
        }
    }
    return NULL;
}

static void DS_FormatGuidA(REFGUID guid, CHAR* buffer, size_t cch_buffer)
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

static const CHAR* DS_GetPropertySetName(REFGUID guid)
{
    if (Monet_IsEqualPropertySet(guid, &DBPROPSET_DBINIT))
    {
        return "DBPROPSET_DBINIT";
    }
    if (Monet_IsEqualPropertySet(guid, &DBPROPSET_DBINITALL))
    {
        return "DBPROPSET_DBINITALL";
    }
    return "DBPROPSET_UNKNOWN";
}

static const CHAR* DS_GetPropertyName(DBPROPID propid)
{
    switch (propid)
    {
    case DBPROP_AUTH_PERSIST_SENSITIVE_AUTHINFO: return "DBPROP_AUTH_PERSIST_SENSITIVE_AUTHINFO";
    case DBPROP_AUTH_USERID: return "DBPROP_AUTH_USERID";
    case DBPROP_AUTH_PASSWORD: return "DBPROP_AUTH_PASSWORD";
    case DBPROP_INIT_DATASOURCE: return "DBPROP_INIT_DATASOURCE";
    case DBPROP_INIT_CATALOG: return "DBPROP_INIT_CATALOG";
    case DBPROP_INIT_TIMEOUT: return "DBPROP_INIT_TIMEOUT";
    case DBPROP_INIT_PROMPT: return "DBPROP_INIT_PROMPT";
    case DBPROP_INIT_LCID: return "DBPROP_INIT_LCID";
    case DBPROP_INIT_MODE: return "DBPROP_INIT_MODE";
    case DBPROP_INIT_PROVIDERSTRING: return "DBPROP_INIT_PROVIDERSTRING";
    case DBPROP_INIT_OLEDBSERVICES: return "DBPROP_INIT_OLEDBSERVICES";
    default: return "DBPROP_UNKNOWN";
    }
}

static const CHAR* DS_GetVariantTypeName(VARTYPE vt)
{
    switch (vt)
    {
    case VT_EMPTY: return "VT_EMPTY";
    case VT_NULL: return "VT_NULL";
    case VT_I2: return "VT_I2";
    case VT_I4: return "VT_I4";
    case VT_R4: return "VT_R4";
    case VT_R8: return "VT_R8";
    case VT_BSTR: return "VT_BSTR";
    case VT_BOOL: return "VT_BOOL";
    case VT_UI1: return "VT_UI1";
    case VT_UI2: return "VT_UI2";
    case VT_UI4: return "VT_UI4";
    default: return "VT_UNKNOWN";
    }
}

static const CHAR* DS_GetPropertyStatusName(DBSTATUS status)
{
    switch (status)
    {
    case DBPROPSTATUS_OK: return "DBPROPSTATUS_OK";
    case DBPROPSTATUS_NOTSUPPORTED: return "DBPROPSTATUS_NOTSUPPORTED";
    case DBPROPSTATUS_BADVALUE: return "DBPROPSTATUS_BADVALUE";
    case DBPROPSTATUS_NOTSET: return "DBPROPSTATUS_NOTSET";
    case DBPROPSTATUS_BADOPTION: return "DBPROPSTATUS_BADOPTION";
    case DBPROPSTATUS_BADCOLUMN: return "DBPROPSTATUS_BADCOLUMN";
    case DBPROPSTATUS_NOTALLSETTABLE: return "DBPROPSTATUS_NOTALLSETTABLE";
    case DBPROPSTATUS_NOTSETTABLE: return "DBPROPSTATUS_NOTSETTABLE";
    case DBPROPSTATUS_NOTAVAILABLE: return "DBPROPSTATUS_NOTAVAILABLE";
    case DBPROPSTATUS_CONFLICTING: return "DBPROPSTATUS_CONFLICTING";
    default: return "DBPROPSTATUS_UNKNOWN";
    }
}

static const CHAR* DS_GetPropertyOptionName(DWORD options)
{
    switch (options)
    {
    case DBPROPOPTIONS_REQUIRED: return "DBPROPOPTIONS_REQUIRED";
    case DBPROPOPTIONS_OPTIONAL: return "DBPROPOPTIONS_OPTIONAL";
    default: return "DBPROPOPTIONS_UNKNOWN";
    }
}

static void DS_FormatWidePreviewA(const WCHAR* text, CHAR* buffer, size_t cch_buffer)
{
    WCHAR temp[256];

    if (!buffer || cch_buffer == 0)
    {
        return;
    }

    buffer[0] = '\0';
    if (!text)
    {
        Monet_StringCopyA(buffer, cch_buffer, "<null>");
        return;
    }

    Monet_StringCopyW(temp, MONET_ARRAY_SIZE(temp), text);
    Monet_WideToAnsi(buffer, cch_buffer, temp);
}

static void DS_FormatVariantValueA(const VARIANT* value, CHAR* buffer, size_t cch_buffer)
{
    if (!buffer || cch_buffer == 0)
    {
        return;
    }

    buffer[0] = '\0';
    if (!value)
    {
        Monet_StringCopyA(buffer, cch_buffer, "<null-variant>");
        return;
    }

    switch (value->vt)
    {
    case VT_EMPTY:
        Monet_StringCopyA(buffer, cch_buffer, "<empty>");
        return;
    case VT_NULL:
        Monet_StringCopyA(buffer, cch_buffer, "<null>");
        return;
    case VT_BSTR:
        DS_FormatWidePreviewA(value->bstrVal, buffer, cch_buffer);
        return;
    case VT_I2:
        _snprintf(buffer, cch_buffer, "%d", (int)value->iVal);
        break;
    case VT_I4:
        _snprintf(buffer, cch_buffer, "%ld", value->lVal);
        break;
    case VT_UI1:
        _snprintf(buffer, cch_buffer, "%u", (unsigned int)value->bVal);
        break;
    case VT_UI2:
        _snprintf(buffer, cch_buffer, "%u", (unsigned int)value->uiVal);
        break;
    case VT_UI4:
        _snprintf(buffer, cch_buffer, "%lu", (unsigned long)value->ulVal);
        break;
    case VT_BOOL:
        Monet_StringCopyA(buffer, cch_buffer, (value->boolVal == VARIANT_TRUE) ? "TRUE" : "FALSE");
        return;
    default:
        _snprintf(buffer, cch_buffer, "<unsupported-vt=0x%04X>", (unsigned int)value->vt);
        break;
    }

    buffer[cch_buffer - 1] = '\0';
}

static void DS_LogPropIdSetRequest(const CHAR* scope, ULONG index, const DBPROPIDSET* set)
{
    CHAR guid_text[64];
    ULONG property_index;

    if (!Config_IsTraceEnabled() || !set)
    {
        return;
    }

    DS_FormatGuidA(&set->guidPropertySet, guid_text, MONET_ARRAY_SIZE(guid_text));
    Log_WriteA(MONET_LOG_TRACE, scope,
        "request[%lu] guid=%s name=%s cPropertyIDs=%lu",
        (unsigned long)index,
        guid_text,
        DS_GetPropertySetName(&set->guidPropertySet),
        (unsigned long)set->cPropertyIDs);

    if (set->cPropertyIDs == 0 || !set->rgPropertyIDs)
    {
        Log_WriteA(MONET_LOG_TRACE, scope, "request[%lu] all properties requested", (unsigned long)index);
        return;
    }

    for (property_index = 0; property_index < set->cPropertyIDs; ++property_index)
    {
        Log_WriteA(MONET_LOG_TRACE, scope,
            "request[%lu].property[%lu] id=%lu name=%s",
            (unsigned long)index,
            (unsigned long)property_index,
            (unsigned long)set->rgPropertyIDs[property_index],
            DS_GetPropertyName(set->rgPropertyIDs[property_index]));
    }
}

static void DS_LogPropertyArray(const CHAR* scope, ULONG set_index, const DBPROPSET* set)
{
    CHAR guid_text[64];
    ULONG property_index;

    if (!Config_IsTraceEnabled() || !set)
    {
        return;
    }

    DS_FormatGuidA(&set->guidPropertySet, guid_text, MONET_ARRAY_SIZE(guid_text));
    Log_WriteA(MONET_LOG_TRACE, scope,
        "response[%lu] guid=%s name=%s cProperties=%lu",
        (unsigned long)set_index,
        guid_text,
        DS_GetPropertySetName(&set->guidPropertySet),
        (unsigned long)set->cProperties);

    for (property_index = 0; property_index < set->cProperties; ++property_index)
    {
        CHAR value_text[256];
        const DBPROP* prop = &set->rgProperties[property_index];

        DS_FormatVariantValueA(&prop->vValue, value_text, MONET_ARRAY_SIZE(value_text));
        Log_WriteA(MONET_LOG_TRACE, scope,
            "response[%lu].property[%lu] id=%lu name=%s status=%s(0x%08lX) options=%s(0x%08lX) vt=%s(0x%04X) value='%s'",
            (unsigned long)set_index,
            (unsigned long)property_index,
            (unsigned long)prop->dwPropertyID,
            DS_GetPropertyName(prop->dwPropertyID),
            DS_GetPropertyStatusName(prop->dwStatus),
            (unsigned long)prop->dwStatus,
            DS_GetPropertyOptionName(prop->dwOptions),
            (unsigned long)prop->dwOptions,
            DS_GetVariantTypeName(prop->vValue.vt),
            (unsigned int)prop->vValue.vt,
            value_text);
    }
}

static void DS_LogPropertyInfoArray(const CHAR* scope, ULONG set_index, const DBPROPINFOSET* set)
{
    CHAR guid_text[64];
    ULONG property_index;

    if (!Config_IsTraceEnabled() || !set)
    {
        return;
    }

    DS_FormatGuidA(&set->guidPropertySet, guid_text, MONET_ARRAY_SIZE(guid_text));
    Log_WriteA(MONET_LOG_TRACE, scope,
        "response[%lu] guid=%s name=%s cPropertyInfos=%lu",
        (unsigned long)set_index,
        guid_text,
        DS_GetPropertySetName(&set->guidPropertySet),
        (unsigned long)set->cPropertyInfos);

    for (property_index = 0; property_index < set->cPropertyInfos; ++property_index)
    {
        CHAR description_text[256];
        CHAR values_text[256];
        const DBPROPINFO* info = &set->rgPropertyInfos[property_index];

        DS_FormatWidePreviewA(info->pwszDescription, description_text, MONET_ARRAY_SIZE(description_text));
        DS_FormatVariantValueA(&info->vValues, values_text, MONET_ARRAY_SIZE(values_text));
        Log_WriteA(MONET_LOG_TRACE, scope,
            "response[%lu].info[%lu] id=%lu name=%s flags=0x%08lX vt=%s(0x%04X) description='%s' values='%s'",
            (unsigned long)set_index,
            (unsigned long)property_index,
            (unsigned long)info->dwPropertyID,
            DS_GetPropertyName(info->dwPropertyID),
            (unsigned long)info->dwFlags,
            DS_GetVariantTypeName(info->vtType),
            (unsigned int)info->vtType,
            description_text,
            values_text);
    }
}

static void DS_LogInitSnapshot(MonetDataSource* self, const CHAR* scope, const CHAR* label)
{
    CHAR datasource[MONETDB_MAX_NAME];
    CHAR catalog[MONETDB_MAX_NAME];
    CHAR user[MONETDB_MAX_NAME];
    CHAR password[MONETDB_MAX_NAME];
    CHAR providerstring[256];

    if (!Config_IsTraceEnabled() || !self)
    {
        return;
    }

    Monet_WideToAnsi(datasource, MONET_ARRAY_SIZE(datasource), self->init_datasource);
    Monet_WideToAnsi(catalog, MONET_ARRAY_SIZE(catalog), self->init_catalog);
    Monet_WideToAnsi(user, MONET_ARRAY_SIZE(user), self->auth_user);
    Monet_WideToAnsi(password, MONET_ARRAY_SIZE(password), self->auth_password);
    Monet_WideToAnsi(providerstring, MONET_ARRAY_SIZE(providerstring), self->init_providerstring);

    Log_WriteA(MONET_LOG_TRACE, scope,
        "%s datasource='%s' catalog='%s' user='%s' password='%s' persist-sensitive=%d prompt=%lu lcid=%lu mode=0x%08lX oledbservices=0x%08lX initialized=%d fetch_rows=%ld fetch_window_kb=%ld providerstring='%s'",
        label ? label : "state",
        datasource,
        catalog,
        user,
        password,
        self->auth_persist_sensitive == VARIANT_TRUE ? 1 : 0,
        (unsigned long)self->init_prompt,
        (unsigned long)self->init_lcid,
        (unsigned long)self->init_mode,
        (unsigned long)self->init_oledbservices,
        self->initialized ? 1 : 0,
        self->config.fetch_rows,
        self->config.fetch_window_kb,
        providerstring);
}

static MonetDataSource* DS_FromInit(IDBInitialize* iface)
{
    return CONTAINING_RECORD(iface, MonetDataSource, IDBInitialize_iface);
}

static MonetDataSource* DS_FromInnerUnknown(IUnknown* iface)
{
    return CONTAINING_RECORD(iface, MonetDataSource, IUnknown_inner_iface);
}

static MonetDataSource* DS_FromProps(IDBProperties* iface)
{
    return CONTAINING_RECORD(iface, MonetDataSource, IDBProperties_iface);
}

static MonetDataSource* DS_FromCreateSession(IDBCreateSession* iface)
{
    return CONTAINING_RECORD(iface, MonetDataSource, IDBCreateSession_iface);
}

static MonetDataSource* DS_FromPersist(IPersist* iface)
{
    return CONTAINING_RECORD(iface, MonetDataSource, IPersist_iface);
}

static MonetDataSource* DS_FromInfo(IDBInfo* iface)
{
    return CONTAINING_RECORD(iface, MonetDataSource, IDBInfo_iface);
}

static MonetDataSource* DS_FromSupportErrorInfo(ISupportErrorInfo* iface)
{
    return CONTAINING_RECORD(iface, MonetDataSource, ISupportErrorInfo_iface);
}

static void DS_ResetFromConfig(MonetDataSource* self)
{
    Monet_AnsiToWide(self->init_datasource, MONET_ARRAY_SIZE(self->init_datasource), self->config.dsn);
    Monet_AnsiToWide(self->init_catalog, MONET_ARRAY_SIZE(self->init_catalog), self->config.database);
    Monet_AnsiToWide(self->auth_user, MONET_ARRAY_SIZE(self->auth_user), self->config.user);
    Monet_AnsiToWide(self->auth_password, MONET_ARRAY_SIZE(self->auth_password), self->config.password);
    self->init_providerstring[0] = L'\0';
    self->auth_persist_sensitive = VARIANT_FALSE;
    self->init_lcid = GetThreadLocale();
    self->init_mode = self->config.read_only ? DB_MODE_READ : DB_MODE_READWRITE;
    self->init_oledbservices = (LONG)DBPROPVAL_OS_ENABLEALL;
    self->init_prompt = DBPROMPT_NOPROMPT;
}

static HRESULT DS_QueryInterfaceInternal(MonetDataSource* self, REFIID riid, void** ppv)
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
    else if (IsEqualIID(riid, &IID_IDBInitialize))
    {
        *ppv = &self->IDBInitialize_iface;
    }
    else if (IsEqualIID(riid, &IID_IDBProperties))
    {
        *ppv = &self->IDBProperties_iface;
    }
    else if (IsEqualIID(riid, &IID_IDBCreateSession))
    {
        *ppv = &self->IDBCreateSession_iface;
    }
    else if (IsEqualIID(riid, &IID_IPersist))
    {
        *ppv = &self->IPersist_iface;
    }
    else if (IsEqualIID(riid, &IID_IDBInfo))
    {
        *ppv = &self->IDBInfo_iface;
    }
    else if (IsEqualIID(riid, &IID_ISupportErrorInfo))
    {
        *ppv = &self->ISupportErrorInfo_iface;
    }
    else
    {
        CHAR iid_text[64];
        DS_FormatGuidA(riid, iid_text, MONET_ARRAY_SIZE(iid_text));
        Log_WriteA(MONET_LOG_DEBUG, "DataSource::QueryInterface", "IID non supportato {%08lX-%04X-%04X-...}",
            riid->Data1, riid->Data2, riid->Data3);
        Log_WriteA(MONET_LOG_TRACE, "DataSource::QueryInterface", "IID non supportato full=%s", iid_text);
        return E_NOINTERFACE;
    }

    if (Config_IsTraceEnabled())
    {
        CHAR iid_text[64];
        DS_FormatGuidA(riid, iid_text, MONET_ARRAY_SIZE(iid_text));
        Log_WriteA(MONET_LOG_TRACE, "DataSource::QueryInterface", "IID supportato full=%s", iid_text);
    }
    InterlockedIncrement(&self->ref_count);
    Log_WriteA(MONET_LOG_DEBUG, "DataSource::QueryInterface", "IID supportato {%08lX-%04X-%04X-...}",
        riid->Data1, riid->Data2, riid->Data3);
    return S_OK;
}

static ULONG DS_AddRefInternal(MonetDataSource* self)
{
    ULONG ref = (ULONG)InterlockedIncrement(&self->ref_count);
    if (Config_IsTraceEnabled())
    {
        Log_WriteA(MONET_LOG_TRACE, "DataSource::AddRef", "ref=%lu", (unsigned long)ref);
    }
    return ref;
}

static ULONG DS_ReleaseInternal(MonetDataSource* self)
{
    ULONG ref = (ULONG)InterlockedDecrement(&self->ref_count);
    if (Config_IsTraceEnabled())
    {
        Log_WriteA(MONET_LOG_TRACE, "DataSource::Release", "ref=%lu", (unsigned long)ref);
    }
    if (ref == 0)
    {
        Log_WriteA(MONET_LOG_DEBUG, "DataSource::Release", "Distruzione DataSource");
        Odbc_CloseConnection(self);
        DeleteCriticalSection(&self->lock);
        CoTaskMemFree(self);
        Monet_ObjectRelease();
    }
    return ref;
}

static HRESULT DS_QueryInterface_InnerUnknown(IUnknown* iface, REFIID riid, void** ppv) { return DS_QueryInterfaceInternal(DS_FromInnerUnknown(iface), riid, ppv); }
static ULONG DS_AddRef_InnerUnknown(IUnknown* iface) { return DS_AddRefInternal(DS_FromInnerUnknown(iface)); }
static ULONG DS_Release_InnerUnknown(IUnknown* iface) { return DS_ReleaseInternal(DS_FromInnerUnknown(iface)); }

static HRESULT DS_QueryInterface_IDBInitialize(IDBInitialize* iface, REFIID riid, void** ppv) { return DS_QueryInterfaceInternal(DS_FromInit(iface), riid, ppv); }
static ULONG DS_AddRef_IDBInitialize(IDBInitialize* iface) { return DS_AddRefInternal(DS_FromInit(iface)); }
static ULONG DS_Release_IDBInitialize(IDBInitialize* iface) { return DS_ReleaseInternal(DS_FromInit(iface)); }
static HRESULT DS_QueryInterface_IDBProperties(IDBProperties* iface, REFIID riid, void** ppv) { return DS_QueryInterfaceInternal(DS_FromProps(iface), riid, ppv); }
static ULONG DS_AddRef_IDBProperties(IDBProperties* iface) { return DS_AddRefInternal(DS_FromProps(iface)); }
static ULONG DS_Release_IDBProperties(IDBProperties* iface) { return DS_ReleaseInternal(DS_FromProps(iface)); }
static HRESULT DS_QueryInterface_IDBCreateSession(IDBCreateSession* iface, REFIID riid, void** ppv) { return DS_QueryInterfaceInternal(DS_FromCreateSession(iface), riid, ppv); }
static ULONG DS_AddRef_IDBCreateSession(IDBCreateSession* iface) { return DS_AddRefInternal(DS_FromCreateSession(iface)); }
static ULONG DS_Release_IDBCreateSession(IDBCreateSession* iface) { return DS_ReleaseInternal(DS_FromCreateSession(iface)); }
static HRESULT DS_QueryInterface_IPersist(IPersist* iface, REFIID riid, void** ppv) { return DS_QueryInterfaceInternal(DS_FromPersist(iface), riid, ppv); }
static ULONG DS_AddRef_IPersist(IPersist* iface) { return DS_AddRefInternal(DS_FromPersist(iface)); }
static ULONG DS_Release_IPersist(IPersist* iface) { return DS_ReleaseInternal(DS_FromPersist(iface)); }
static HRESULT DS_QueryInterface_IDBInfo(IDBInfo* iface, REFIID riid, void** ppv) { return DS_QueryInterfaceInternal(DS_FromInfo(iface), riid, ppv); }
static ULONG DS_AddRef_IDBInfo(IDBInfo* iface) { return DS_AddRefInternal(DS_FromInfo(iface)); }
static ULONG DS_Release_IDBInfo(IDBInfo* iface) { return DS_ReleaseInternal(DS_FromInfo(iface)); }
static HRESULT DS_QueryInterface_SupportErrorInfo(ISupportErrorInfo* iface, REFIID riid, void** ppv) { return DS_QueryInterfaceInternal(DS_FromSupportErrorInfo(iface), riid, ppv); }
static ULONG DS_AddRef_SupportErrorInfo(ISupportErrorInfo* iface) { return DS_AddRefInternal(DS_FromSupportErrorInfo(iface)); }
static ULONG DS_Release_SupportErrorInfo(ISupportErrorInfo* iface) { return DS_ReleaseInternal(DS_FromSupportErrorInfo(iface)); }

static HRESULT STDMETHODCALLTYPE DS_InterfaceSupportsErrorInfo(ISupportErrorInfo* iface, REFIID riid)
{
    MonetDataSource* self = DS_FromSupportErrorInfo(iface);
    MONET_UNUSED(self);

    if (IsEqualIID(riid, &IID_IDBInitialize) ||
        IsEqualIID(riid, &IID_IDBProperties) ||
        IsEqualIID(riid, &IID_IDBCreateSession) ||
        IsEqualIID(riid, &IID_IPersist) ||
        IsEqualIID(riid, &IID_IDBInfo))
    {
        return S_OK;
    }

    return S_FALSE;
}

static HRESULT DS_FillSingleProperty(MonetDataSource* self, DBPROPID propid, DBPROP* prop)
{
    const MonetPropertyMeta* meta = DS_FindPropertyMeta(propid);
    VariantInit(&prop->vValue);
    ZeroMemory(&prop->colid, sizeof(prop->colid));
    prop->dwOptions = (meta && (meta->flags & DBPROPFLAGS_REQUIRED)) ? DBPROPOPTIONS_REQUIRED : DBPROPOPTIONS_OPTIONAL;
    prop->dwStatus = DBPROPSTATUS_OK;
    prop->dwPropertyID = propid;

    switch (propid)
    {
    case DBPROP_AUTH_PERSIST_SENSITIVE_AUTHINFO:
        prop->vValue.vt = VT_BOOL;
        prop->vValue.boolVal = self->auth_persist_sensitive;
        return S_OK;

    case DBPROP_AUTH_USERID:
        if (self->auth_user[0])
        {
            prop->vValue.vt = VT_BSTR;
            prop->vValue.bstrVal = SysAllocString(self->auth_user);
            if (!prop->vValue.bstrVal) return E_OUTOFMEMORY;
        }
        else
        {
            prop->dwStatus = DBPROPSTATUS_NOTSET;
            prop->vValue.vt = VT_EMPTY;
        }
        return S_OK;

    case DBPROP_AUTH_PASSWORD:
        if (self->auth_password[0])
        {
            prop->vValue.vt = VT_BSTR;
            prop->vValue.bstrVal = SysAllocString(self->auth_password);
            if (!prop->vValue.bstrVal) return E_OUTOFMEMORY;
        }
        else
        {
            prop->dwStatus = DBPROPSTATUS_NOTSET;
            prop->vValue.vt = VT_EMPTY;
        }
        return S_OK;

    case DBPROP_INIT_DATASOURCE:
        if (self->init_datasource[0])
        {
            prop->vValue.vt = VT_BSTR;
            prop->vValue.bstrVal = SysAllocString(self->init_datasource);
            if (!prop->vValue.bstrVal) return E_OUTOFMEMORY;
        }
        else
        {
            prop->dwStatus = DBPROPSTATUS_NOTSET;
            prop->vValue.vt = VT_EMPTY;
        }
        return S_OK;

    case DBPROP_INIT_CATALOG:
        if (self->init_catalog[0])
        {
            prop->vValue.vt = VT_BSTR;
            prop->vValue.bstrVal = SysAllocString(self->init_catalog);
            if (!prop->vValue.bstrVal) return E_OUTOFMEMORY;
        }
        else
        {
            prop->dwStatus = DBPROPSTATUS_NOTSET;
            prop->vValue.vt = VT_EMPTY;
        }
        return S_OK;

    case DBPROP_INIT_TIMEOUT:
        prop->vValue.vt = VT_I4;
        prop->vValue.lVal = self->config.connection_timeout;
        return S_OK;

    case DBPROP_INIT_PROMPT:
        prop->vValue.vt = VT_I4;
        prop->vValue.lVal = (LONG)self->init_prompt;
        return S_OK;

    case DBPROP_INIT_LCID:
        prop->vValue.vt = VT_I4;
        prop->vValue.lVal = (LONG)self->init_lcid;
        return S_OK;

    case DBPROP_INIT_MODE:
        prop->vValue.vt = VT_I4;
        prop->vValue.lVal = (LONG)self->init_mode;
        return S_OK;

    case DBPROP_INIT_PROVIDERSTRING:
        if (self->init_providerstring[0])
        {
            prop->vValue.vt = VT_BSTR;
            prop->vValue.bstrVal = SysAllocString(self->init_providerstring);
            if (!prop->vValue.bstrVal) return E_OUTOFMEMORY;
        }
        else
        {
            prop->dwStatus = DBPROPSTATUS_NOTSET;
            prop->vValue.vt = VT_EMPTY;
        }
        return S_OK;

    case DBPROP_INIT_OLEDBSERVICES:
        prop->vValue.vt = VT_I4;
        prop->vValue.lVal = self->init_oledbservices;
        return S_OK;

    default:
        prop->dwStatus = DBPROPSTATUS_NOTSUPPORTED;
        prop->vValue.vt = VT_EMPTY;
        return DB_S_ERRORSOCCURRED;
    }
}

static HRESULT DS_BuildPropertyArray(MonetDataSource* self, DBPROPSET* set, ULONG property_count, const DBPROPID* property_ids)
{
    ULONG i;
    HRESULT hr;

    set->guidPropertySet = DBPROPSET_DBINIT;
    set->cProperties = property_count;
    set->rgProperties = (DBPROP*)CoTaskMemAlloc(sizeof(DBPROP) * property_count);
    if (!set->rgProperties)
    {
        return E_OUTOFMEMORY;
    }
    ZeroMemory(set->rgProperties, sizeof(DBPROP) * property_count);

    for (i = 0; i < property_count; ++i)
    {
        hr = DS_FillSingleProperty(self, property_ids[i], &set->rgProperties[i]);
        if (FAILED(hr))
        {
            return hr;
        }
    }

    return S_OK;
}

static HRESULT DS_GetPropertiesCore(MonetDataSource* self, ULONG c_sets, const DBPROPIDSET* rg_sets, ULONG* pc_sets, DBPROPSET** prg_sets)
{
    ULONG i = 0;
    DBPROPSET* sets = NULL;
    HRESULT hr;

    if (!pc_sets || !prg_sets)
    {
        return E_POINTER;
    }

    *pc_sets = 0;
    *prg_sets = NULL;

    if (c_sets == 0 || !rg_sets)
    {
        DBPROPID ids[MONET_ARRAY_SIZE(g_init_property_meta)];
        MONET_TRACE("DataSource::GetProperties", "request without explicit sets, returning full DBINIT property bag", 0);
        for (i = 0; i < MONET_ARRAY_SIZE(g_init_property_meta); ++i)
        {
            ids[i] = g_init_property_meta[i].propid;
        }

        sets = (DBPROPSET*)CoTaskMemAlloc(sizeof(DBPROPSET));
        if (!sets)
        {
            return E_OUTOFMEMORY;
        }
        ZeroMemory(sets, sizeof(DBPROPSET));
        hr = DS_BuildPropertyArray(self, &sets[0], (ULONG)MONET_ARRAY_SIZE(ids), ids);
        if (FAILED(hr))
        {
            CoTaskMemFree(sets);
            return hr;
        }
        *pc_sets = 1;
        *prg_sets = sets;
        Log_WriteA(MONET_LOG_DEBUG, "DataSource::GetProperties", "Restituito set DBINIT completo (%lu props)", (unsigned long)MONET_ARRAY_SIZE(ids));
        DS_LogPropertyArray("DataSource::GetProperties", 0, &sets[0]);
        return S_OK;
    }

    sets = (DBPROPSET*)CoTaskMemAlloc(sizeof(DBPROPSET) * c_sets);
    if (!sets)
    {
        return E_OUTOFMEMORY;
    }
    ZeroMemory(sets, sizeof(DBPROPSET) * c_sets);

    for (i = 0; i < c_sets; ++i)
    {
        ULONG j;
        DS_LogPropIdSetRequest("DataSource::GetProperties", i, &rg_sets[i]);
        if (Monet_IsEqualPropertySet(&rg_sets[i].guidPropertySet, &DBPROPSET_DBINIT) ||
            Monet_IsEqualPropertySet(&rg_sets[i].guidPropertySet, &DBPROPSET_DBINITALL))
        {
            if (rg_sets[i].cPropertyIDs == 0 || !rg_sets[i].rgPropertyIDs)
            {
                DBPROPID ids[MONET_ARRAY_SIZE(g_init_property_meta)];
                for (j = 0; j < MONET_ARRAY_SIZE(g_init_property_meta); ++j)
                {
                    ids[j] = g_init_property_meta[j].propid;
                }
                hr = DS_BuildPropertyArray(self, &sets[i], (ULONG)MONET_ARRAY_SIZE(ids), ids);
            }
            else
            {
                hr = DS_BuildPropertyArray(self, &sets[i], rg_sets[i].cPropertyIDs, rg_sets[i].rgPropertyIDs);
            }
            if (FAILED(hr))
            {
                return hr;
            }
            sets[i].guidPropertySet = DS_NormalizePropertySet(&rg_sets[i].guidPropertySet);
        }
        else
        {
            sets[i].guidPropertySet = rg_sets[i].guidPropertySet;
            sets[i].cProperties = 0;
            sets[i].rgProperties = NULL;
        }
    }

    *pc_sets = c_sets;
    *prg_sets = sets;
    Log_WriteA(MONET_LOG_DEBUG, "DataSource::GetProperties", "Restituiti %lu property set", (unsigned long)c_sets);
    for (i = 0; i < c_sets; ++i)
    {
        DS_LogPropertyArray("DataSource::GetProperties", i, &sets[i]);
    }
    return S_OK;
}

static HRESULT DS_GetPropertyInfoCore(ULONG c_sets, const DBPROPIDSET* rg_sets, ULONG* pc_sets, DBPROPINFOSET** prg_sets, OLECHAR** pp_desc)
{
    ULONG request_count = (c_sets == 0 || !rg_sets) ? 1 : c_sets;
    DBPROPINFOSET* info_sets = NULL;
    DBPROPINFO* infos = NULL;
    size_t desc_chars = 1;
    ULONG info_count = 0;
    OLECHAR* desc_buffer = NULL;
    ULONG set_index;

    if (!pc_sets || !prg_sets || !pp_desc)
    {
        return E_POINTER;
    }

    *pc_sets = 0;
    *prg_sets = NULL;
    *pp_desc = NULL;

    if (c_sets == 0 || !rg_sets)
    {
        MONET_TRACE("DataSource::GetPropertyInfo", "request without explicit sets, returning full DBINIT property info", 0);
    }
    else
    {
        for (set_index = 0; set_index < c_sets; ++set_index)
        {
            DS_LogPropIdSetRequest("DataSource::GetPropertyInfo", set_index, &rg_sets[set_index]);
        }
    }

    info_sets = (DBPROPINFOSET*)CoTaskMemAlloc(sizeof(DBPROPINFOSET) * request_count);
    if (!info_sets)
    {
        return E_OUTOFMEMORY;
    }
    ZeroMemory(info_sets, sizeof(DBPROPINFOSET) * request_count);

    for (set_index = 0; set_index < request_count; ++set_index)
    {
        ULONG prop_index;
        const DBPROPIDSET* request = (c_sets == 0 || !rg_sets) ? NULL : &rg_sets[set_index];

        if (request && !(Monet_IsEqualPropertySet(&request->guidPropertySet, &DBPROPSET_DBINIT) ||
            Monet_IsEqualPropertySet(&request->guidPropertySet, &DBPROPSET_DBINITALL)))
        {
            info_sets[set_index].guidPropertySet = request->guidPropertySet;
            continue;
        }

        info_sets[set_index].guidPropertySet = request ? DS_NormalizePropertySet(&request->guidPropertySet) : DBPROPSET_DBINIT;
        info_sets[set_index].cPropertyInfos = request && request->cPropertyIDs > 0 ? request->cPropertyIDs : (ULONG)MONET_ARRAY_SIZE(g_init_property_meta);
        info_count += info_sets[set_index].cPropertyInfos;

        for (prop_index = 0; prop_index < info_sets[set_index].cPropertyInfos; ++prop_index)
        {
            DBPROPID propid = request && request->cPropertyIDs > 0 ? request->rgPropertyIDs[prop_index] : g_init_property_meta[prop_index].propid;
            desc_chars += wcslen(DS_GetPropertyDescription(propid)) + 1;
        }
    }

    infos = (DBPROPINFO*)CoTaskMemAlloc(sizeof(DBPROPINFO) * info_count);
    if (!infos)
    {
        CoTaskMemFree(info_sets);
        return E_OUTOFMEMORY;
    }
    ZeroMemory(infos, sizeof(DBPROPINFO) * info_count);

    desc_buffer = (OLECHAR*)CoTaskMemAlloc(sizeof(OLECHAR) * desc_chars);
    if (!desc_buffer)
    {
        CoTaskMemFree(info_sets);
        CoTaskMemFree(infos);
        return E_OUTOFMEMORY;
    }
    ZeroMemory(desc_buffer, sizeof(OLECHAR) * desc_chars);

    {
        ULONG running_index = 0;
        OLECHAR* cursor = desc_buffer;

        for (set_index = 0; set_index < request_count; ++set_index)
        {
            ULONG prop_index;
            const DBPROPIDSET* request = (c_sets == 0 || !rg_sets) ? NULL : &rg_sets[set_index];
            if (info_sets[set_index].cPropertyInfos == 0)
            {
                continue;
            }

            info_sets[set_index].rgPropertyInfos = &infos[running_index];
            for (prop_index = 0; prop_index < info_sets[set_index].cPropertyInfos; ++prop_index, ++running_index)
            {
                DBPROPID propid = request && request->cPropertyIDs > 0 ? request->rgPropertyIDs[prop_index] : g_init_property_meta[prop_index].propid;
                const MonetPropertyMeta* meta = DS_FindPropertyMeta(propid);
                DBPROPINFO* info = &infos[running_index];
                const WCHAR* description = DS_GetPropertyDescription(propid);
                size_t len = wcslen(description);

                info->dwPropertyID = propid;
                info->dwFlags = meta ? meta->flags : (DBPROPFLAGS_DBINIT | DBPROPFLAGS_WRITE);
                info->vtType = meta ? meta->vt_type : VT_EMPTY;
                info->pwszDescription = cursor;
                wcscpy(cursor, description);
                cursor += len + 1;
                VariantInit(&info->vValues);
            }
        }
    }

    *pc_sets = request_count;
    *prg_sets = info_sets;
    *pp_desc = desc_buffer;
    Log_WriteA(MONET_LOG_DEBUG, "DataSource::GetPropertyInfo", "Restituiti %lu info set", (unsigned long)request_count);
    for (set_index = 0; set_index < request_count; ++set_index)
    {
        DS_LogPropertyInfoArray("DataSource::GetPropertyInfo", set_index, &info_sets[set_index]);
    }
    return S_OK;
}

static LONG VariantToLong(const VARIANT* v, LONG default_value)
{
    if (!v) return default_value;
    switch (v->vt)
    {
    case VT_I2: return v->iVal;
    case VT_I4: return v->lVal;
    case VT_UI2: return (LONG)v->uiVal;
    case VT_UI4: return (LONG)v->ulVal;
    case VT_BOOL: return (v->boolVal == VARIANT_TRUE) ? 1 : 0;
    default: return default_value;
    }
}

static HRESULT DS_ApplyProperty(MonetDataSource* self, DBPROP* prop)
{
    CHAR value_text[256];

    if (!prop)
    {
        return E_POINTER;
    }

    DS_FormatVariantValueA(&prop->vValue, value_text, MONET_ARRAY_SIZE(value_text));
    if (Config_IsTraceEnabled())
    {
        Log_WriteA(MONET_LOG_TRACE, "DataSource::SetProperties",
            "apply property id=%lu name=%s incoming-status=%s(0x%08lX) options=%s(0x%08lX) vt=%s(0x%04X) value='%s'",
            (unsigned long)prop->dwPropertyID,
            DS_GetPropertyName(prop->dwPropertyID),
            DS_GetPropertyStatusName(prop->dwStatus),
            (unsigned long)prop->dwStatus,
            DS_GetPropertyOptionName(prop->dwOptions),
            (unsigned long)prop->dwOptions,
            DS_GetVariantTypeName(prop->vValue.vt),
            (unsigned int)prop->vValue.vt,
            value_text);
    }

    prop->dwStatus = DBPROPSTATUS_OK;
    switch (prop->dwPropertyID)
    {
    case DBPROP_AUTH_PERSIST_SENSITIVE_AUTHINFO:
        if (prop->vValue.vt == VT_EMPTY)
        {
            self->auth_persist_sensitive = VARIANT_FALSE;
            return S_OK;
        }
        switch (prop->vValue.vt)
        {
        case VT_BOOL:
        case VT_I2:
        case VT_I4:
        case VT_UI2:
        case VT_UI4:
            self->auth_persist_sensitive = VariantToLong(&prop->vValue, 0) ? VARIANT_TRUE : VARIANT_FALSE;
            return S_OK;
        default:
            prop->dwStatus = DBPROPSTATUS_BADVALUE;
            return DB_S_ERRORSOCCURRED;
        }

    case DBPROP_AUTH_USERID:
        if (prop->vValue.vt != VT_BSTR)
        {
            prop->dwStatus = DBPROPSTATUS_BADVALUE;
            return DB_S_ERRORSOCCURRED;
        }
        Monet_StringCopyW(self->auth_user, MONET_ARRAY_SIZE(self->auth_user), prop->vValue.bstrVal);
        return S_OK;

    case DBPROP_AUTH_PASSWORD:
        if (prop->vValue.vt != VT_BSTR)
        {
            prop->dwStatus = DBPROPSTATUS_BADVALUE;
            return DB_S_ERRORSOCCURRED;
        }
        Monet_StringCopyW(self->auth_password, MONET_ARRAY_SIZE(self->auth_password), prop->vValue.bstrVal);
        return S_OK;

    case DBPROP_INIT_DATASOURCE:
        if (prop->vValue.vt != VT_BSTR)
        {
            prop->dwStatus = DBPROPSTATUS_BADVALUE;
            return DB_S_ERRORSOCCURRED;
        }
        Monet_StringCopyW(self->init_datasource, MONET_ARRAY_SIZE(self->init_datasource), prop->vValue.bstrVal);
        return S_OK;

    case DBPROP_INIT_CATALOG:
        if (prop->vValue.vt != VT_BSTR)
        {
            prop->dwStatus = DBPROPSTATUS_BADVALUE;
            return DB_S_ERRORSOCCURRED;
        }
        Monet_StringCopyW(self->init_catalog, MONET_ARRAY_SIZE(self->init_catalog), prop->vValue.bstrVal);
        return S_OK;

    case DBPROP_INIT_TIMEOUT:
        self->config.connection_timeout = VariantToLong(&prop->vValue, self->config.connection_timeout);
        return S_OK;

    case DBPROP_INIT_PROMPT:
        self->init_prompt = (ULONG)VariantToLong(&prop->vValue, (LONG)self->init_prompt);
        return S_OK;

    case DBPROP_INIT_LCID:
        self->init_lcid = (ULONG)VariantToLong(&prop->vValue, (LONG)self->init_lcid);
        return S_OK;

    case DBPROP_INIT_MODE:
        self->init_mode = (ULONG)VariantToLong(&prop->vValue, (LONG)self->init_mode);
        self->config.read_only = ((self->init_mode & DB_MODE_READWRITE) == 0);
        return S_OK;

    case DBPROP_INIT_PROVIDERSTRING:
        if (prop->vValue.vt == VT_EMPTY)
        {
            self->init_providerstring[0] = L'\0';
            return S_OK;
        }
        if (prop->vValue.vt != VT_BSTR)
        {
            prop->dwStatus = DBPROPSTATUS_BADVALUE;
            return DB_S_ERRORSOCCURRED;
        }
        Monet_StringCopyW(self->init_providerstring, MONET_ARRAY_SIZE(self->init_providerstring), prop->vValue.bstrVal);
        return S_OK;

    case DBPROP_INIT_OLEDBSERVICES:
        if (prop->vValue.vt == VT_EMPTY)
        {
            self->init_oledbservices = (LONG)DBPROPVAL_OS_ENABLEALL;
            return S_OK;
        }
        switch (prop->vValue.vt)
        {
        case VT_BOOL:
        case VT_I2:
        case VT_I4:
        case VT_UI2:
        case VT_UI4:
            self->init_oledbservices = VariantToLong(&prop->vValue, self->init_oledbservices);
            return S_OK;
        default:
            prop->dwStatus = DBPROPSTATUS_BADVALUE;
            return DB_S_ERRORSOCCURRED;
        }

    default:
        prop->dwStatus = DBPROPSTATUS_NOTSUPPORTED;
        return DB_S_ERRORSOCCURRED;
    }
}

static HRESULT STDMETHODCALLTYPE DS_Initialize(IDBInitialize* iface)
{
    MonetDataSource* self = DS_FromInit(iface);
    HRESULT hr;

    MONET_TRACE("DataSource::Initialize", "enter ref=%ld", self->ref_count);
    DS_LogInitSnapshot(self, "DataSource::Initialize", "before connect");
    EnterCriticalSection(&self->lock);
    if (self->initialized)
    {
        LeaveCriticalSection(&self->lock);
        MONET_TRACE("DataSource::Initialize", "exit already-initialized hr=0x%08lx", S_OK);
        return S_OK;
    }

    Log_Init(&self->config);
    hr = Odbc_OpenConnection(self);
    if (SUCCEEDED(hr))
    {
        self->initialized = TRUE;
        Log_WriteA(MONET_LOG_INFO, "DataSource::Initialize", "DataSource inizializzato");
    }
    LeaveCriticalSection(&self->lock);
    DS_LogInitSnapshot(self, "DataSource::Initialize", "after connect");
    MONET_TRACE("DataSource::Initialize", "exit hr=0x%08lx", hr);
    return hr;
}

static HRESULT STDMETHODCALLTYPE DS_Uninitialize(IDBInitialize* iface)
{
    MonetDataSource* self = DS_FromInit(iface);

    MONET_TRACE("DataSource::Uninitialize", "enter", 0);
    EnterCriticalSection(&self->lock);
    Odbc_CloseConnection(self);
    self->initialized = FALSE;
    LeaveCriticalSection(&self->lock);
    Log_WriteA(MONET_LOG_INFO, "DataSource::Uninitialize", "DataSource chiuso");
    MONET_TRACE("DataSource::Uninitialize", "exit hr=0x%08lx", S_OK);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE DS_GetProperties(IDBProperties* iface, ULONG c_sets, const DBPROPIDSET rg_sets[], ULONG* pc_sets, DBPROPSET** prg_sets)
{
    MonetDataSource* self = DS_FromProps(iface);
    HRESULT hr;

    Log_WriteA(MONET_LOG_DEBUG, "DataSource::GetProperties", "Richiesti %lu property id sets", (unsigned long)c_sets);
    hr = DS_GetPropertiesCore(self, c_sets, rg_sets, pc_sets, prg_sets);
    MONET_TRACE("DataSource::GetProperties", "exit hr=0x%08lx pc_sets=%lu", hr, (unsigned long)(pc_sets ? *pc_sets : 0));
    return hr;
}

static HRESULT STDMETHODCALLTYPE DS_GetPropertyInfo(IDBProperties* iface, ULONG c_sets, const DBPROPIDSET rg_sets[], ULONG* pc_sets, DBPROPINFOSET** prg_sets, OLECHAR** pp_desc)
{
    HRESULT hr;
    MONET_UNUSED(iface);
    Log_WriteA(MONET_LOG_DEBUG, "DataSource::GetPropertyInfo", "Richiesti %lu property info sets", (unsigned long)c_sets);
    hr = DS_GetPropertyInfoCore(c_sets, rg_sets, pc_sets, prg_sets, pp_desc);
    MONET_TRACE("DataSource::GetPropertyInfo", "exit hr=0x%08lx pc_sets=%lu", hr, (unsigned long)(pc_sets ? *pc_sets : 0));
    return hr;
}

static HRESULT STDMETHODCALLTYPE DS_SetProperties(IDBProperties* iface, ULONG c_sets, DBPROPSET rg_sets[])
{
    MonetDataSource* self = DS_FromProps(iface);
    ULONG i;
    HRESULT hr = S_OK;

    Log_WriteA(MONET_LOG_DEBUG, "DataSource::SetProperties", "Impostazione di %lu property set", (unsigned long)c_sets);
    for (i = 0; i < c_sets; ++i)
    {
        ULONG j;
        CHAR guid_text[64];

        DS_FormatGuidA(&rg_sets[i].guidPropertySet, guid_text, MONET_ARRAY_SIZE(guid_text));
        Log_WriteA(MONET_LOG_TRACE, "DataSource::SetProperties",
            "set[%lu] guid=%s name=%s cProperties=%lu",
            (unsigned long)i,
            guid_text,
            DS_GetPropertySetName(&rg_sets[i].guidPropertySet),
            (unsigned long)rg_sets[i].cProperties);
        if (!(Monet_IsEqualPropertySet(&rg_sets[i].guidPropertySet, &DBPROPSET_DBINIT) ||
            Monet_IsEqualPropertySet(&rg_sets[i].guidPropertySet, &DBPROPSET_DBINITALL)))
        {
            for (j = 0; j < rg_sets[i].cProperties; ++j)
            {
                rg_sets[i].rgProperties[j].dwStatus = DBPROPSTATUS_NOTSUPPORTED;
            }
            hr = DB_S_ERRORSOCCURRED;
            continue;
        }

        for (j = 0; j < rg_sets[i].cProperties; ++j)
        {
            HRESULT one = DS_ApplyProperty(self, &rg_sets[i].rgProperties[j]);
            if (FAILED(one) || one == DB_S_ERRORSOCCURRED)
            {
                hr = DB_S_ERRORSOCCURRED;
            }

            if (Config_IsTraceEnabled())
            {
                CHAR value_text[256];
                DS_FormatVariantValueA(&rg_sets[i].rgProperties[j].vValue, value_text, MONET_ARRAY_SIZE(value_text));
                Log_WriteA(MONET_LOG_TRACE, "DataSource::SetProperties",
                    "set[%lu].property[%lu] result status=%s(0x%08lX) id=%lu name=%s vt=%s(0x%04X) value='%s'",
                    (unsigned long)i,
                    (unsigned long)j,
                    DS_GetPropertyStatusName(rg_sets[i].rgProperties[j].dwStatus),
                    (unsigned long)rg_sets[i].rgProperties[j].dwStatus,
                    (unsigned long)rg_sets[i].rgProperties[j].dwPropertyID,
                    DS_GetPropertyName(rg_sets[i].rgProperties[j].dwPropertyID),
                    DS_GetVariantTypeName(rg_sets[i].rgProperties[j].vValue.vt),
                    (unsigned int)rg_sets[i].rgProperties[j].vValue.vt,
                    value_text);
            }
        }
    }

    DS_LogInitSnapshot(self, "DataSource::SetProperties", "after set");
    MONET_TRACE("DataSource::SetProperties", "exit hr=0x%08lx", hr);
    return hr;
}

static HRESULT STDMETHODCALLTYPE DS_CreateSession(IDBCreateSession* iface, IUnknown* outer, REFIID riid, IUnknown** pp_session)
{
    MonetDataSource* self = DS_FromCreateSession(iface);
    HRESULT hr;
    CHAR iid_text[64];

    DS_FormatGuidA(riid, iid_text, MONET_ARRAY_SIZE(iid_text));
    MONET_TRACE("DataSource::CreateSession", "enter initialized=%d outer=%p riid=%s", self->initialized ? 1 : 0, outer, iid_text);
    if (!self->initialized)
    {
        Log_WriteA(MONET_LOG_DEBUG, "DataSource::CreateSession", "data source non inizializzato, ritorno E_UNEXPECTED");
        return E_UNEXPECTED;
    }
    hr = Session_Create(self, outer, riid, (void**)pp_session);
    MONET_TRACE("DataSource::CreateSession", "exit hr=0x%08lx", hr);
    return hr;
}

static HRESULT STDMETHODCALLTYPE DS_GetClassID(IPersist* iface, CLSID* p_classid)
{
    MONET_UNUSED(iface);
    if (!p_classid)
    {
        return E_POINTER;
    }
    *p_classid = CLSID_MonetDBOleDbProvider;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE DS_GetKeywords(IDBInfo* iface, LPOLESTR* pp_keywords)
{
    MONET_UNUSED(iface);
    return Monet_AllocWideStringFromAnsi("SELECT,INSERT,UPDATE,DELETE,FROM,WHERE,JOIN,INNER,LEFT,RIGHT,ORDER,BY,GROUP,LIMIT", pp_keywords);
}

static HRESULT STDMETHODCALLTYPE DS_GetLiteralInfo(IDBInfo* iface, ULONG c_literals, const DBLITERAL rg_literals[], ULONG* pc_info, DBLITERALINFO** prg_info, OLECHAR** pp_buffer)
{
    static const DBLITERAL all_literals[] = { DBLITERAL_CATALOG_SEPARATOR, DBLITERAL_SCHEMA_SEPARATOR, DBLITERAL_QUOTE_PREFIX, DBLITERAL_QUOTE_SUFFIX };
    const DBLITERAL* literals = rg_literals;
    ULONG literal_count = c_literals;
    DBLITERALINFO* info = NULL;
    OLECHAR* buffer = NULL;
    OLECHAR* cursor = NULL;
    ULONG i;

    MONET_UNUSED(iface);
    if (!pc_info || !prg_info || !pp_buffer)
    {
        return E_POINTER;
    }

    *pc_info = 0;
    *prg_info = NULL;
    *pp_buffer = NULL;

    if (literal_count == 0 || !literals)
    {
        literals = all_literals;
        literal_count = (ULONG)MONET_ARRAY_SIZE(all_literals);
    }

    info = (DBLITERALINFO*)CoTaskMemAlloc(sizeof(DBLITERALINFO) * literal_count);
    if (!info)
    {
        return E_OUTOFMEMORY;
    }
    ZeroMemory(info, sizeof(DBLITERALINFO) * literal_count);

    buffer = (OLECHAR*)CoTaskMemAlloc(sizeof(OLECHAR) * 32);
    if (!buffer)
    {
        CoTaskMemFree(info);
        return E_OUTOFMEMORY;
    }
    ZeroMemory(buffer, sizeof(OLECHAR) * 32);
    cursor = buffer;

    for (i = 0; i < literal_count; ++i)
    {
        const WCHAR* value = L"";
        switch (literals[i])
        {
        case DBLITERAL_CATALOG_SEPARATOR:
            value = L".";
            break;
        case DBLITERAL_SCHEMA_SEPARATOR:
            value = L".";
            break;
        case DBLITERAL_QUOTE_PREFIX:
            value = L"\"";
            break;
        case DBLITERAL_QUOTE_SUFFIX:
            value = L"\"";
            break;
        default:
            value = L"";
            break;
        }

        info[i].lt = literals[i];
        info[i].fSupported = TRUE;
        info[i].cchMaxLen = (ULONG)wcslen(value);
        info[i].pwszLiteralValue = cursor;
        wcscpy(cursor, value);
        cursor += wcslen(value) + 1;
    }

    *pc_info = literal_count;
    *prg_info = info;
    *pp_buffer = buffer;
    return S_OK;
}

static IUnknownVtbl g_ds_unknown_inner_vtbl =
{
    DS_QueryInterface_InnerUnknown,
    DS_AddRef_InnerUnknown,
    DS_Release_InnerUnknown
};

static IDBInitializeVtbl g_ds_init_vtbl =
{
    DS_QueryInterface_IDBInitialize,
    DS_AddRef_IDBInitialize,
    DS_Release_IDBInitialize,
    DS_Initialize,
    DS_Uninitialize
};

static IDBPropertiesVtbl g_ds_props_vtbl =
{
    DS_QueryInterface_IDBProperties,
    DS_AddRef_IDBProperties,
    DS_Release_IDBProperties,
    DS_GetProperties,
    DS_GetPropertyInfo,
    DS_SetProperties
};

static IDBCreateSessionVtbl g_ds_create_session_vtbl =
{
    DS_QueryInterface_IDBCreateSession,
    DS_AddRef_IDBCreateSession,
    DS_Release_IDBCreateSession,
    DS_CreateSession
};

static IPersistVtbl g_ds_persist_vtbl =
{
    DS_QueryInterface_IPersist,
    DS_AddRef_IPersist,
    DS_Release_IPersist,
    DS_GetClassID
};

static IDBInfoVtbl g_ds_info_vtbl =
{
    DS_QueryInterface_IDBInfo,
    DS_AddRef_IDBInfo,
    DS_Release_IDBInfo,
    DS_GetKeywords,
    DS_GetLiteralInfo
};

static ISupportErrorInfoVtbl g_ds_support_error_info_vtbl =
{
    DS_QueryInterface_SupportErrorInfo,
    DS_AddRef_SupportErrorInfo,
    DS_Release_SupportErrorInfo,
    DS_InterfaceSupportsErrorInfo
};

HRESULT DataSource_CreateInstance(IUnknown* outer, REFIID riid, void** ppv)
{
    MonetDataSource* self = NULL;
    HRESULT hr;

    if (!ppv)
    {
        return E_POINTER;
    }

    *ppv = NULL;
    if (outer != NULL && !IsEqualIID(riid, &IID_IUnknown))
    {
        Log_WriteA(MONET_LOG_ERROR, "DataSource::CreateInstance", "Richiesta aggregata con IID non-IUnknown {%08lX-%04X-%04X-...}",
            riid->Data1, riid->Data2, riid->Data3);
        return CLASS_E_NOAGGREGATION;
    }

    self = (MonetDataSource*)CoTaskMemAlloc(sizeof(*self));
    if (!self)
    {
        return E_OUTOFMEMORY;
    }
    ZeroMemory(self, sizeof(*self));

    self->IUnknown_inner_iface.lpVtbl = &g_ds_unknown_inner_vtbl;
    self->IDBInitialize_iface.lpVtbl = &g_ds_init_vtbl;
    self->IDBProperties_iface.lpVtbl = &g_ds_props_vtbl;
    self->IDBCreateSession_iface.lpVtbl = &g_ds_create_session_vtbl;
    self->IPersist_iface.lpVtbl = &g_ds_persist_vtbl;
    self->IDBInfo_iface.lpVtbl = &g_ds_info_vtbl;
    self->ISupportErrorInfo_iface.lpVtbl = &g_ds_support_error_info_vtbl;
    self->outer_unknown = outer ? outer : &self->IUnknown_inner_iface;
    self->ref_count = 1;
    self->hdbc = SQL_NULL_HDBC;
    InitializeCriticalSection(&self->lock);
    Config_Load(&self->config, NULL);
    self->config_loaded = TRUE;
    DS_ResetFromConfig(self);
    Log_Init(&self->config);
    Log_WriteA(MONET_LOG_INFO, "DataSource::CreateInstance", "DataSource creato v%s ini='%s' aggregated=%d outer=%p",
        MONETDB_OLEDB_VERSION_A, self->config.ini_path, outer ? 1 : 0, outer);
    DS_LogInitSnapshot(self, "DataSource::CreateInstance", "from ini");
    Monet_ObjectAddRef();

    hr = IUnknown_QueryInterface(&self->IUnknown_inner_iface, riid, ppv);
    DS_ReleaseInternal(self);
    return hr;
}
