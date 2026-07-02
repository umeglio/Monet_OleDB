#include "monetdb_oledb.h"

static CRITICAL_SECTION g_log_lock;
static LONG g_log_initialized = 0;
static HANDLE g_log_file = INVALID_HANDLE_VALUE;
static HANDLE g_query_log_file = INVALID_HANDLE_VALUE;
static HANDLE g_result_log_file = INVALID_HANDLE_VALUE;
static LONG g_log_level = MONET_LOG_INFO;
static LONG g_trace_enabled = 0;
static CHAR g_log_path[MAX_PATH];
static CHAR g_bootstrap_path[MAX_PATH];
static CHAR g_query_log_path[MAX_PATH];
static CHAR g_result_log_path[MAX_PATH];

static void Log_WriteUnlocked(const CHAR* prefix, const CHAR* text)
{
    DWORD written = 0;
    if (g_log_file == INVALID_HANDLE_VALUE)
    {
        return;
    }
    WriteFile(g_log_file, prefix, (DWORD)strlen(prefix), &written, NULL);
    WriteFile(g_log_file, text, (DWORD)strlen(text), &written, NULL);
}

static void Log_WriteHandleUnlocked(HANDLE file, const CHAR* prefix, const CHAR* text)
{
    DWORD written = 0;
    if (file == INVALID_HANDLE_VALUE)
    {
        return;
    }
    WriteFile(file, prefix, (DWORD)strlen(prefix), &written, NULL);
    WriteFile(file, text, (DWORD)strlen(text), &written, NULL);
}

static void BuildTimestamp(CHAR* buffer, size_t cch_buffer)
{
    FILETIME ft;
    ULARGE_INTEGER uli;
    ULONGLONG micros_since_1601;
    ULONGLONG micros_since_1970;
    SYSTEMTIME st;
    DWORD micros;

    GetSystemTimePreciseAsFileTime(&ft);
    uli.LowPart = ft.dwLowDateTime;
    uli.HighPart = ft.dwHighDateTime;
    micros_since_1601 = uli.QuadPart / 10ULL;
    micros_since_1970 = micros_since_1601 - 11644473600000000ULL;
    micros = (DWORD)(micros_since_1970 % 1000000ULL);

    GetLocalTime(&st);
    _snprintf(
        buffer,
        cch_buffer,
        "[%04u-%02u-%02u %02u:%02u:%02u.%06lu]",
        st.wYear,
        st.wMonth,
        st.wDay,
        st.wHour,
        st.wMinute,
        st.wSecond,
        (unsigned long)micros);
    buffer[cch_buffer - 1] = '\0';
}

static void BuildDefaultLogPath(CHAR* path, size_t cch_path)
{
    CHAR module_path[MAX_PATH];
    CHAR* slash = NULL;

    module_path[0] = '\0';
    if (!GetModuleFileNameA(Monet_GetModuleHandle(), module_path, (DWORD)MONET_ARRAY_SIZE(module_path)))
    {
        Monet_StringCopyA(path, cch_path, "C:\\ProgramData\\MonetDB_OleDb\\boot.log");
        return;
    }

    slash = strrchr(module_path, '\\');
    if (slash != NULL)
    {
        *slash = '\0';
        _snprintf(path, cch_path, "%s\\monetdb_oledb.log", module_path);
    }
    else
    {
        Monet_StringCopyA(path, cch_path, "C:\\ProgramData\\MonetDB_OleDb\\boot.log");
    }
    path[cch_path - 1] = '\0';
}

static void EnsureParentDirectory(const CHAR* path)
{
    CHAR temp[MAX_PATH];
    CHAR* slash = NULL;

    Monet_StringCopyA(temp, MONET_ARRAY_SIZE(temp), path);
    slash = strrchr(temp, '\\');
    if (slash != NULL)
    {
        *slash = '\0';
        CreateDirectoryA(temp, NULL);
    }
}

static void BuildSiblingLogPath(const CHAR* base_path, const CHAR* suffix, CHAR* path, size_t cch_path)
{
    CHAR folder[MAX_PATH];
    CHAR filename[MAX_PATH];
    CHAR* slash = NULL;
    CHAR* dot = NULL;

    if (!path || cch_path == 0)
    {
        return;
    }

    path[0] = '\0';
    if (!base_path || !base_path[0])
    {
        return;
    }

    Monet_StringCopyA(folder, MONET_ARRAY_SIZE(folder), base_path);
    slash = strrchr(folder, '\\');
    if (!slash)
    {
        return;
    }

    Monet_StringCopyA(filename, MONET_ARRAY_SIZE(filename), slash + 1);
    *slash = '\0';
    dot = strrchr(filename, '.');
    if (dot)
    {
        *dot = '\0';
    }

    _snprintf(path, cch_path, "%s\\%s%s.log", folder, filename, suffix ? suffix : "");
    path[cch_path - 1] = '\0';
}

static void CloseTraceLogFiles(void)
{
    if (g_query_log_file != INVALID_HANDLE_VALUE)
    {
        CloseHandle(g_query_log_file);
        g_query_log_file = INVALID_HANDLE_VALUE;
    }
    if (g_result_log_file != INVALID_HANDLE_VALUE)
    {
        CloseHandle(g_result_log_file);
        g_result_log_file = INVALID_HANDLE_VALUE;
    }
    g_query_log_path[0] = '\0';
    g_result_log_path[0] = '\0';
}

static void OpenTraceLogFiles(const CHAR* base_path)
{
    BuildSiblingLogPath(base_path, ".queries", g_query_log_path, MONET_ARRAY_SIZE(g_query_log_path));
    BuildSiblingLogPath(base_path, ".results", g_result_log_path, MONET_ARRAY_SIZE(g_result_log_path));

    if (g_query_log_path[0])
    {
        EnsureParentDirectory(g_query_log_path);
        g_query_log_file = CreateFileA(g_query_log_path, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    }
    if (g_result_log_path[0])
    {
        EnsureParentDirectory(g_result_log_path);
        g_result_log_file = CreateFileA(g_result_log_path, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    }
}

HMODULE Monet_GetModuleHandle(void)
{
    extern HMODULE g_monet_module;
    return g_monet_module;
}

void Monet_StringCopyA(CHAR* dest, size_t cch_dest, const CHAR* src)
{
    if (!dest || cch_dest == 0)
    {
        return;
    }

    if (!src)
    {
        dest[0] = '\0';
        return;
    }

    strncpy(dest, src, cch_dest - 1);
    dest[cch_dest - 1] = '\0';
}

void Monet_StringCopyW(WCHAR* dest, size_t cch_dest, const WCHAR* src)
{
    if (!dest || cch_dest == 0)
    {
        return;
    }

    if (!src)
    {
        dest[0] = L'\0';
        return;
    }

    wcsncpy(dest, src, cch_dest - 1);
    dest[cch_dest - 1] = L'\0';
}

void Monet_AnsiToWide(WCHAR* dest, size_t cch_dest, const CHAR* src)
{
    if (!dest || cch_dest == 0)
    {
        return;
    }

    dest[0] = L'\0';
    if (!src || !src[0])
    {
        return;
    }

    MultiByteToWideChar(CP_ACP, 0, src, -1, dest, (int)cch_dest);
    dest[cch_dest - 1] = L'\0';
}

void Monet_WideToAnsi(CHAR* dest, size_t cch_dest, const WCHAR* src)
{
    if (!dest || cch_dest == 0)
    {
        return;
    }

    dest[0] = '\0';
    if (!src || !src[0])
    {
        return;
    }

    WideCharToMultiByte(CP_ACP, 0, src, -1, dest, (int)cch_dest, NULL, NULL);
    dest[cch_dest - 1] = '\0';
}

HRESULT Monet_AllocWideString(const WCHAR* src, OLECHAR** ppwz)
{
    size_t length = 0;
    OLECHAR* copy = NULL;

    if (!ppwz)
    {
        return E_POINTER;
    }

    *ppwz = NULL;
    if (!src)
    {
        return S_OK;
    }

    length = wcslen(src) + 1;
    copy = (OLECHAR*)CoTaskMemAlloc(length * sizeof(OLECHAR));
    if (!copy)
    {
        return E_OUTOFMEMORY;
    }

    memcpy(copy, src, length * sizeof(OLECHAR));
    *ppwz = copy;
    return S_OK;
}

HRESULT Monet_AllocWideStringFromAnsi(const CHAR* src, OLECHAR** ppwz)
{
    WCHAR temp[MONETDB_MAX_SQL_TEXT];

    if (!ppwz)
    {
        return E_POINTER;
    }

    *ppwz = NULL;
    Monet_AnsiToWide(temp, MONET_ARRAY_SIZE(temp), src ? src : "");
    return Monet_AllocWideString(temp, ppwz);
}

HRESULT Monet_FormatWideString(OLECHAR** ppwz, const WCHAR* format, ...)
{
    WCHAR buffer[1024];
    va_list args;

    if (!ppwz || !format)
    {
        return E_POINTER;
    }

    va_start(args, format);
    _vsnwprintf(buffer, MONET_ARRAY_SIZE(buffer), format, args);
    va_end(args);
    buffer[MONET_ARRAY_SIZE(buffer) - 1] = L'\0';
    return Monet_AllocWideString(buffer, ppwz);
}

void Monet_SafeReleaseIUnknown(IUnknown* punk)
{
    if (punk)
    {
        punk->lpVtbl->Release(punk);
    }
}

BOOL Monet_IsEqualPropertySet(REFGUID a, REFGUID b)
{
    return IsEqualGUID(a, b);
}

HRESULT Config_InitializeDefaults(MonetConfig* cfg)
{
    if (!cfg)
    {
        return E_POINTER;
    }

    ZeroMemory(cfg, sizeof(*cfg));
    Monet_StringCopyA(cfg->dsn, MONET_ARRAY_SIZE(cfg->dsn), "MonetDB");
    Monet_StringCopyA(cfg->database, MONET_ARRAY_SIZE(cfg->database), "demo");
    Monet_StringCopyA(cfg->schema, MONET_ARRAY_SIZE(cfg->schema), "sys");
    Monet_StringCopyA(cfg->user, MONET_ARRAY_SIZE(cfg->user), "monetdb");
    Monet_StringCopyA(cfg->password, MONET_ARRAY_SIZE(cfg->password), "monetdb");
    cfg->connection_timeout = 30;
    cfg->query_timeout = 120;
    cfg->read_only = 0;
    cfg->autocommit = 1;
    cfg->fetch_rows = MONETDB_DEFAULT_FETCH_ROWS;
    cfg->fetch_window_kb = MONETDB_DEFAULT_FETCH_WINDOW_KB;
    cfg->log_level = MONET_LOG_INFO;
    cfg->trace = 0;
    Monet_StringCopyA(cfg->log_file, MONET_ARRAY_SIZE(cfg->log_file), "monetdb_oledb.log");
    Config_ResolveIniPath(cfg->ini_path, MONET_ARRAY_SIZE(cfg->ini_path));
    return S_OK;
}

BOOL Config_ResolveIniPath(CHAR* path, size_t cch_path)
{
    CHAR module_path[MAX_PATH];
    CHAR installed_ini[MAX_PATH];
    CHAR local_ini[MAX_PATH];
    CHAR* slash = NULL;
    DWORD attrs = INVALID_FILE_ATTRIBUTES;

    if (!path || cch_path == 0)
    {
        return FALSE;
    }

    path[0] = '\0';
    module_path[0] = '\0';
    if (!GetModuleFileNameA(Monet_GetModuleHandle(), module_path, (DWORD)MONET_ARRAY_SIZE(module_path)))
    {
        return FALSE;
    }

    slash = strrchr(module_path, '\\');
    if (!slash)
    {
        return FALSE;
    }
    *slash = '\0';

    _snprintf(installed_ini, MONET_ARRAY_SIZE(installed_ini), "%s\\monetdb_oledb.ini", module_path);
    installed_ini[MONET_ARRAY_SIZE(installed_ini) - 1] = '\0';
    attrs = GetFileAttributesA(installed_ini);
    if (attrs != INVALID_FILE_ATTRIBUTES)
    {
        Monet_StringCopyA(path, cch_path, installed_ini);
        return TRUE;
    }

    _snprintf(local_ini, MONET_ARRAY_SIZE(local_ini), "%s\\config\\monetdb_oledb.ini", module_path);
    local_ini[MONET_ARRAY_SIZE(local_ini) - 1] = '\0';
    attrs = GetFileAttributesA(local_ini);
    if (attrs != INVALID_FILE_ATTRIBUTES)
    {
        Monet_StringCopyA(path, cch_path, local_ini);
        return TRUE;
    }

    Monet_StringCopyA(path, cch_path, installed_ini);
    return TRUE;
}

HRESULT Config_Load(MonetConfig* cfg, const CHAR* explicit_path)
{
    CHAR ini_path[MAX_PATH];

    if (!cfg)
    {
        return E_POINTER;
    }

    Config_InitializeDefaults(cfg);
    if (explicit_path && explicit_path[0])
    {
        Monet_StringCopyA(ini_path, MONET_ARRAY_SIZE(ini_path), explicit_path);
    }
    else
    {
        Config_ResolveIniPath(ini_path, MONET_ARRAY_SIZE(ini_path));
    }

    Monet_StringCopyA(cfg->ini_path, MONET_ARRAY_SIZE(cfg->ini_path), ini_path);
    GetPrivateProfileStringA(MONETDB_OLEDB_INI_SECTION_A, "DSN", cfg->dsn, cfg->dsn, (DWORD)MONET_ARRAY_SIZE(cfg->dsn), ini_path);
    GetPrivateProfileStringA(MONETDB_OLEDB_INI_SECTION_A, "Database", cfg->database, cfg->database, (DWORD)MONET_ARRAY_SIZE(cfg->database), ini_path);
    GetPrivateProfileStringA(MONETDB_OLEDB_INI_SECTION_A, "Schema", cfg->schema, cfg->schema, (DWORD)MONET_ARRAY_SIZE(cfg->schema), ini_path);
    GetPrivateProfileStringA(MONETDB_OLEDB_INI_SECTION_A, "User", cfg->user, cfg->user, (DWORD)MONET_ARRAY_SIZE(cfg->user), ini_path);
    GetPrivateProfileStringA(MONETDB_OLEDB_INI_SECTION_A, "Password", cfg->password, cfg->password, (DWORD)MONET_ARRAY_SIZE(cfg->password), ini_path);
    cfg->connection_timeout = GetPrivateProfileIntA(MONETDB_OLEDB_INI_SECTION_A, "ConnectionTimeout", (INT)cfg->connection_timeout, ini_path);
    cfg->query_timeout = GetPrivateProfileIntA(MONETDB_OLEDB_INI_SECTION_A, "QueryTimeout", (INT)cfg->query_timeout, ini_path);
    cfg->read_only = GetPrivateProfileIntA(MONETDB_OLEDB_INI_SECTION_A, "ReadOnly", (INT)cfg->read_only, ini_path);
    cfg->autocommit = GetPrivateProfileIntA(MONETDB_OLEDB_INI_SECTION_A, "AutoCommit", (INT)cfg->autocommit, ini_path);
    cfg->fetch_rows = GetPrivateProfileIntA(MONETDB_OLEDB_INI_SECTION_A, "FetchRows", (INT)cfg->fetch_rows, ini_path);
    cfg->fetch_window_kb = GetPrivateProfileIntA(MONETDB_OLEDB_INI_SECTION_A, "FetchWindowKB", (INT)cfg->fetch_window_kb, ini_path);
    cfg->log_level = GetPrivateProfileIntA(MONETDB_OLEDB_INI_SECTION_A, "LogLevel", (INT)cfg->log_level, ini_path);
    cfg->trace = GetPrivateProfileIntA(MONETDB_OLEDB_INI_SECTION_A, "Trace", (INT)cfg->trace, ini_path);
    GetPrivateProfileStringA(MONETDB_OLEDB_INI_SECTION_A, "LogFile", cfg->log_file, cfg->log_file, (DWORD)MONET_ARRAY_SIZE(cfg->log_file), ini_path);

    if (cfg->fetch_rows < MONETDB_MIN_FETCH_ROWS)
    {
        cfg->fetch_rows = MONETDB_DEFAULT_FETCH_ROWS;
    }
    else if (cfg->fetch_rows > MONETDB_MAX_FETCH_ROWS)
    {
        cfg->fetch_rows = MONETDB_MAX_FETCH_ROWS;
    }

    if (cfg->fetch_window_kb < MONETDB_MIN_FETCH_WINDOW_KB)
    {
        cfg->fetch_window_kb = MONETDB_DEFAULT_FETCH_WINDOW_KB;
    }
    else if (cfg->fetch_window_kb > MONETDB_MAX_FETCH_WINDOW_KB)
    {
        cfg->fetch_window_kb = MONETDB_MAX_FETCH_WINDOW_KB;
    }

    if (strchr(cfg->log_file, ':') == NULL && strstr(cfg->log_file, "\\") == NULL)
    {
        CHAR base_dir[MAX_PATH];
        CHAR combined[MAX_PATH];
        CHAR* slash = NULL;

        Monet_StringCopyA(base_dir, MONET_ARRAY_SIZE(base_dir), ini_path);
        slash = strrchr(base_dir, '\\');
        if (slash)
        {
            *slash = '\0';
            _snprintf(combined, MONET_ARRAY_SIZE(combined), "%s\\%s", base_dir, cfg->log_file);
            combined[MONET_ARRAY_SIZE(combined) - 1] = '\0';
            Monet_StringCopyA(cfg->log_file, MONET_ARRAY_SIZE(cfg->log_file), combined);
        }
    }

    return S_OK;
}

BOOL Config_IsTraceEnabled(void)
{
    return (InterlockedCompareExchange(&g_trace_enabled, 0, 0) != 0);
}

void Log_Init(const MonetConfig* cfg)
{
    CHAR path[MAX_PATH];

    if (InterlockedCompareExchange(&g_log_initialized, 1, 0) == 0)
    {
        InitializeCriticalSection(&g_log_lock);
    }

    if (cfg)
    {
        g_log_level = cfg->log_level;
        g_trace_enabled = cfg->trace ? 1 : 0;
        Monet_StringCopyA(path, MONET_ARRAY_SIZE(path), cfg->log_file);
    }
    else
    {
        g_log_level = MONET_LOG_INFO;
        g_trace_enabled = 0;
        BuildDefaultLogPath(path, MONET_ARRAY_SIZE(path));
    }

    EnsureParentDirectory(path);
    if (g_log_file != INVALID_HANDLE_VALUE)
    {
        CloseHandle(g_log_file);
        g_log_file = INVALID_HANDLE_VALUE;
    }
    CloseTraceLogFiles();

    g_log_file = CreateFileA(path, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (g_log_file == INVALID_HANDLE_VALUE)
    {
        CreateDirectoryA("C:\\ProgramData\\MonetDB_OleDb", NULL);
        Monet_StringCopyA(path, MONET_ARRAY_SIZE(path), "C:\\ProgramData\\MonetDB_OleDb\\boot.log");
        g_log_file = CreateFileA(path, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    }

    Monet_StringCopyA(g_log_path, MONET_ARRAY_SIZE(g_log_path), path);
    if (g_trace_enabled)
    {
        OpenTraceLogFiles(path);
    }
}

void Log_Shutdown(void)
{
    if (g_log_file != INVALID_HANDLE_VALUE)
    {
        CloseHandle(g_log_file);
        g_log_file = INVALID_HANDLE_VALUE;
    }
    CloseTraceLogFiles();

    if (InterlockedCompareExchange(&g_log_initialized, 0, 1) == 1)
    {
        DeleteCriticalSection(&g_log_lock);
    }
}

void Log_WriteA(int level, const CHAR* scope, const CHAR* format, ...)
{
    CHAR timestamp[64];
    CHAR line[4096];
    CHAR body[3584];
    const CHAR* level_name = "INFO";
    va_list args;

    if (level > g_log_level && !(level == MONET_LOG_TRACE && g_trace_enabled))
    {
        return;
    }

    switch (level)
    {
    case MONET_LOG_ERROR:
        level_name = "ERROR";
        break;
    case MONET_LOG_DEBUG:
        level_name = "DEBUG";
        break;
    case MONET_LOG_TRACE:
        level_name = "TRACE";
        break;
    default:
        level_name = "INFO";
        break;
    }

    if (g_log_file == INVALID_HANDLE_VALUE)
    {
        Log_Init(NULL);
    }

    BuildTimestamp(timestamp, MONET_ARRAY_SIZE(timestamp));
    va_start(args, format);
    _vsnprintf(body, MONET_ARRAY_SIZE(body), format, args);
    va_end(args);
    body[MONET_ARRAY_SIZE(body) - 1] = '\0';
    _snprintf(
        line,
        MONET_ARRAY_SIZE(line),
        "%s [%s] [PID=%lu TID=%lu] %s%s%s\r\n",
        timestamp,
        level_name,
        (unsigned long)GetCurrentProcessId(),
        (unsigned long)GetCurrentThreadId(),
        scope ? scope : "",
        scope && scope[0] ? " " : "",
        body);
    line[MONET_ARRAY_SIZE(line) - 1] = '\0';

    EnterCriticalSection(&g_log_lock);
    Log_WriteUnlocked("", line);
    LeaveCriticalSection(&g_log_lock);
}

static void Log_WriteTraceChannel(HANDLE file, const CHAR* channel, const CHAR* scope, const CHAR* format, va_list args)
{
    CHAR timestamp[64];
    CHAR line[4096];
    CHAR body[3584];

    if (!Config_IsTraceEnabled())
    {
        return;
    }
    if (file == INVALID_HANDLE_VALUE)
    {
        return;
    }

    BuildTimestamp(timestamp, MONET_ARRAY_SIZE(timestamp));
    _vsnprintf(body, MONET_ARRAY_SIZE(body), format, args);
    body[MONET_ARRAY_SIZE(body) - 1] = '\0';
    _snprintf(
        line,
        MONET_ARRAY_SIZE(line),
        "%s [%s] [PID=%lu TID=%lu] [v=%s] %s%s%s\r\n",
        timestamp,
        channel ? channel : "TRACE",
        (unsigned long)GetCurrentProcessId(),
        (unsigned long)GetCurrentThreadId(),
        MONETDB_OLEDB_VERSION_A,
        scope ? scope : "",
        scope && scope[0] ? " " : "",
        body);
    line[MONET_ARRAY_SIZE(line) - 1] = '\0';

    EnterCriticalSection(&g_log_lock);
    Log_WriteHandleUnlocked(file, "", line);
    LeaveCriticalSection(&g_log_lock);
}

void Log_WriteQueryA(const CHAR* scope, const CHAR* format, ...)
{
    va_list args;

    if (!Config_IsTraceEnabled())
    {
        return;
    }
    if (g_log_file == INVALID_HANDLE_VALUE)
    {
        Log_Init(NULL);
    }

    va_start(args, format);
    Log_WriteTraceChannel(g_query_log_file, "QUERY", scope, format, args);
    va_end(args);
}

void Log_WriteResultA(const CHAR* scope, const CHAR* format, ...)
{
    va_list args;

    if (!Config_IsTraceEnabled())
    {
        return;
    }
    if (g_log_file == INVALID_HANDLE_VALUE)
    {
        Log_Init(NULL);
    }

    va_start(args, format);
    Log_WriteTraceChannel(g_result_log_file, "RESULT", scope, format, args);
    va_end(args);
}

void Bootstrap_LogA(const CHAR* format, ...)
{
    CHAR timestamp[64];
    CHAR body[3584];
    CHAR line[4096];
    HANDLE file = INVALID_HANDLE_VALUE;
    DWORD written = 0;
    va_list args;

    BuildTimestamp(timestamp, MONET_ARRAY_SIZE(timestamp));
    va_start(args, format);
    _vsnprintf(body, MONET_ARRAY_SIZE(body), format, args);
    va_end(args);
    body[MONET_ARRAY_SIZE(body) - 1] = '\0';

    if (!g_bootstrap_path[0])
    {
        BuildDefaultLogPath(g_bootstrap_path, MONET_ARRAY_SIZE(g_bootstrap_path));
    }
    EnsureParentDirectory(g_bootstrap_path);

    file = CreateFileA(g_bootstrap_path, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE)
    {
        CreateDirectoryA("C:\\ProgramData\\MonetDB_OleDb", NULL);
        Monet_StringCopyA(g_bootstrap_path, MONET_ARRAY_SIZE(g_bootstrap_path), "C:\\ProgramData\\MonetDB_OleDb\\boot.log");
        file = CreateFileA(g_bootstrap_path, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    }

    if (file == INVALID_HANDLE_VALUE)
    {
        return;
    }

    _snprintf(
        line,
        MONET_ARRAY_SIZE(line),
        "%s [BOOT] [PID=%lu TID=%lu] %s\r\n",
        timestamp,
        (unsigned long)GetCurrentProcessId(),
        (unsigned long)GetCurrentThreadId(),
        body);
    line[MONET_ARRAY_SIZE(line) - 1] = '\0';

    WriteFile(file, line, (DWORD)strlen(line), &written, NULL);
    CloseHandle(file);
}

HRESULT Variant_CopyBstrOrEmpty(VARIANT* value, const WCHAR* text, BOOL required)
{
    if (!value)
    {
        return E_POINTER;
    }

    VariantInit(value);
    if (!text || !text[0])
    {
        value->vt = VT_EMPTY;
        return required ? DB_S_ERRORSOCCURRED : S_OK;
    }

    value->vt = VT_BSTR;
    value->bstrVal = SysAllocString(text);
    if (!value->bstrVal)
    {
        return E_OUTOFMEMORY;
    }

    return S_OK;
}

HRESULT Variant_FromLong(VARIANT* value, LONG number)
{
    if (!value)
    {
        return E_POINTER;
    }

    VariantInit(value);
    value->vt = VT_I4;
    value->lVal = number;
    return S_OK;
}

HRESULT Variant_FromBool(VARIANT* value, BOOL flag)
{
    if (!value)
    {
        return E_POINTER;
    }

    VariantInit(value);
    value->vt = VT_BOOL;
    value->boolVal = flag ? VARIANT_TRUE : VARIANT_FALSE;
    return S_OK;
}
