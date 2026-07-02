#pragma once

#ifndef COBJMACROS
#define COBJMACROS
#endif

#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif

#include <windows.h>
#include <objbase.h>
#include <oleauto.h>
#include <oledb.h>
#include <oledberr.h>
#include <msdasc.h>
#include <transact.h>
#include <sql.h>
#include <sqlext.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <time.h>
#include <limits.h>

#define MONETDB_OLEDB_VERSION_A        "1.0.52-varchar2000"
#define MONETDB_OLEDB_PROVIDER_NAME_A  "MonetDB OLE DB Provider"
#define MONETDB_OLEDB_PROVIDER_NAME_W  L"MonetDB OLE DB Provider"
#define MONETDB_OLEDB_PROGID_A         "MonetDB.OleDb"
#define MONETDB_OLEDB_PROGID_W         L"MonetDB.OleDb"
#define MONETDB_OLEDB_PROGID_VER_A     "MonetDB.OleDb.1"
#define MONETDB_OLEDB_PROGID_VER_W     L"MonetDB.OleDb.1"
#define MONETDB_OLEDB_FRIENDLY_NAME_A  "MonetDB OLE DB Provider for SQL Server"
#define MONETDB_OLEDB_FRIENDLY_NAME_W  L"MonetDB OLE DB Provider for SQL Server"
#define MONETDB_OLEDB_INI_SECTION_A    "MonetDB"
#define MONETDB_MAX_SQL_TEXT           32768
#define MONETDB_MAX_NAME               256
#define MONETDB_DEFAULT_FETCH_ROWS     256
#define MONETDB_DEFAULT_FETCH_WINDOW_KB 1204
#define MONETDB_MIN_FETCH_ROWS         1
#define MONETDB_MAX_FETCH_ROWS         4096
#define MONETDB_MIN_FETCH_WINDOW_KB    64
#define MONETDB_MAX_FETCH_WINDOW_KB    16384
#define MONETDB_READ_CHUNK_BYTES       16384
#define MONETDB_MAX_FAST_STRING_CHARS  2000
#define MONETDB_SCHEMA_COUNT           13
#define MONETDB_TRACE_RESULT_ROWS      500

#define MONET_LOG_ERROR 1
#define MONET_LOG_INFO  2
#define MONET_LOG_DEBUG 3
#define MONET_LOG_TRACE 4

#define MONET_ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#define MONET_UNUSED(x) ((void)(x))

typedef struct MonetConfig MonetConfig;
typedef struct MonetDataSource MonetDataSource;
typedef struct MonetSession MonetSession;
typedef struct MonetCommand MonetCommand;
typedef struct MonetRowset MonetRowset;
typedef struct MonetAccessor MonetAccessor;
typedef struct MonetAccessorTable MonetAccessorTable;
typedef struct MonetCellValue MonetCellValue;
typedef struct MonetBufferedRow MonetBufferedRow;
typedef struct MonetColumnInfo MonetColumnInfo;
typedef union MonetCellNativeValue MonetCellNativeValue;

EXTERN_C const CLSID CLSID_MonetDBOleDbProvider;
EXTERN_C const IID IID_IMonetRowsetInternal;

struct MonetConfig
{
    CHAR ini_path[MAX_PATH];
    CHAR dsn[MONETDB_MAX_NAME];
    CHAR database[MONETDB_MAX_NAME];
    CHAR schema[MONETDB_MAX_NAME];
    CHAR user[MONETDB_MAX_NAME];
    CHAR password[MONETDB_MAX_NAME];
    LONG connection_timeout;
    LONG query_timeout;
    LONG read_only;
    LONG autocommit;
    LONG fetch_rows;
    LONG fetch_window_kb;
    LONG log_level;
    LONG trace;
    CHAR log_file[MAX_PATH];
};

struct MonetAccessor
{
    HACCESSOR handle;
    DBACCESSORFLAGS flags;
    DBREFCOUNT ref_count;
    DBCOUNTITEM binding_count;
    DBLENGTH row_size;
    DBBINDING* bindings;
};

struct MonetAccessorTable
{
    CRITICAL_SECTION lock;
    MonetAccessor** items;
    size_t count;
    size_t capacity;
    ULONG next_handle;
};

struct MonetColumnInfo
{
    CHAR name_a[MONETDB_MAX_NAME];
    WCHAR name_w[MONETDB_MAX_NAME];
    DBORDINAL ordinal;
    DBTYPE type;
    DBCOLUMNFLAGS flags;
    DBLENGTH column_size;
    BYTE precision;
    BYTE scale;
    SQLSMALLINT sql_type;
    SQLULEN sql_size;
    SQLSMALLINT sql_scale;
    SQLSMALLINT sql_nullable;
    SQLSMALLINT fetch_c_type;
    SQLLEN fetch_buffer_size;
};

union MonetCellNativeValue
{
    SQLCHAR bit_value;
    SQLCHAR utiny_value;
    SQLSMALLINT sshort_value;
    SQLUSMALLINT ushort_value;
    SQLINTEGER slong_value;
    SQLUINTEGER ulong_value;
    SQLBIGINT sbigint_value;
    SQLUBIGINT ubigint_value;
    SQLREAL real_value;
    SQLDOUBLE double_value;
    DATE_STRUCT date_value;
    TIME_STRUCT time_value;
    TIMESTAMP_STRUCT timestamp_value;
};

struct MonetCellValue
{
    BOOL is_null;
    DBLENGTH length;
    CHAR* text;
    WCHAR* wide_text;
    DBLENGTH wide_length;
    SQLSMALLINT storage_c_type;
    DBLENGTH native_length;
    MonetCellNativeValue native;
};

struct MonetBufferedRow
{
    struct MonetBufferedRow* next;
    DBREFCOUNT ref_count;
    DBORDINAL column_count;
    MonetCellValue* cells;
};

struct MonetDataSource
{
    IUnknown IUnknown_inner_iface;
    IDBInitialize IDBInitialize_iface;
    IDBProperties IDBProperties_iface;
    IDBCreateSession IDBCreateSession_iface;
    IPersist IPersist_iface;
    IDBInfo IDBInfo_iface;
    ISupportErrorInfo ISupportErrorInfo_iface;
    IUnknown* outer_unknown;
    LONG ref_count;
    CRITICAL_SECTION lock;
    MonetConfig config;
    BOOL initialized;
    BOOL config_loaded;
    SQLHDBC hdbc;
    WCHAR init_datasource[MONETDB_MAX_NAME];
    WCHAR init_catalog[MONETDB_MAX_NAME];
    WCHAR auth_user[MONETDB_MAX_NAME];
    WCHAR auth_password[MONETDB_MAX_NAME];
    WCHAR init_providerstring[MONETDB_MAX_SQL_TEXT];
    VARIANT_BOOL auth_persist_sensitive;
    ULONG init_lcid;
    ULONG init_mode;
    LONG init_oledbservices;
    ULONG init_prompt;
};

struct MonetSession
{
    IUnknown IUnknown_inner_iface;
    IOpenRowset IOpenRowset_iface;
    IGetDataSource IGetDataSource_iface;
    IDBCreateCommand IDBCreateCommand_iface;
    IDBSchemaRowset IDBSchemaRowset_iface;
    ISessionProperties ISessionProperties_iface;
    ITransactionJoin ITransactionJoin_iface;
    ITransactionLocal ITransactionLocal_iface;
    ISupportErrorInfo ISupportErrorInfo_iface;
    IUnknown* outer_unknown;
    LONG ref_count;
    MonetDataSource* datasource;
    LONG transaction_level;
};

struct MonetCommand
{
    ICommandText ICommandText_iface;
    ICommandProperties ICommandProperties_iface;
    IColumnsInfo IColumnsInfo_iface;
    IAccessor IAccessor_iface;
    IConvertType IConvertType_iface;
    ICommandPrepare ICommandPrepare_iface;
    ISupportErrorInfo ISupportErrorInfo_iface;
    LONG ref_count;
    MonetSession* session;
    GUID dialect;
    CHAR sql_text[MONETDB_MAX_SQL_TEXT];
    BOOL prepared;
    SQLHSTMT prepared_stmt;
    MonetAccessorTable accessors;
    MonetColumnInfo* columns;
    DBORDINAL column_count;
};

struct MonetRowset
{
    IRowset IRowset_iface;
    IRowsetChange IRowsetChange_iface;
    IAccessor IAccessor_iface;
    IColumnsInfo IColumnsInfo_iface;
    IRowsetInfo IRowsetInfo_iface;
    IConvertType IConvertType_iface;
    ISupportErrorInfo ISupportErrorInfo_iface;
    LONG ref_count;
    MonetSession* session;
    MonetCommand* command;
    SQLHSTMT hstmt;
    MonetAccessorTable accessors;
    MonetColumnInfo* columns;
    DBORDINAL column_count;
    BOOL end_of_rowset;
    ULONGLONG fetched_row_count;
    BOOL trace_row_limit_logged;
    DWORD updatability;
    CHAR base_schema[MONETDB_MAX_NAME];
    CHAR base_table[MONETDB_MAX_NAME];
    SQLHSTMT insert_hstmt;
    HACCESSOR insert_accessor;
    DBCOUNTITEM insert_param_count;
    DBORDINAL* insert_ordinals;
    CHAR* insert_batch_sql;
    size_t insert_batch_capacity;
    size_t insert_batch_used;
    HACCESSOR insert_batch_accessor;
    DBCOUNTITEM insert_batch_column_count;
    DBCOUNTITEM insert_batch_row_count;
    DBORDINAL* insert_batch_ordinals;
    ULONGLONG insert_batch_total_buffered;
    ULONGLONG insert_batch_total_flushed;
    ULONGLONG insert_batch_flush_count;
    MonetBufferedRow* available_head;
    MonetBufferedRow* available_tail;
    MonetBufferedRow* outstanding_rows;
};

HMODULE Monet_GetModuleHandle(void);
LONG Monet_ObjectAddRef(void);
LONG Monet_ObjectRelease(void);
LONG Monet_LockAddRef(void);
LONG Monet_LockRelease(void);

void Monet_StringCopyA(CHAR* dest, size_t cch_dest, const CHAR* src);
void Monet_StringCopyW(WCHAR* dest, size_t cch_dest, const WCHAR* src);
void Monet_AnsiToWide(WCHAR* dest, size_t cch_dest, const CHAR* src);
void Monet_WideToAnsi(CHAR* dest, size_t cch_dest, const WCHAR* src);
HRESULT Monet_AllocWideString(const WCHAR* src, OLECHAR** ppwz);
HRESULT Monet_AllocWideStringFromAnsi(const CHAR* src, OLECHAR** ppwz);
HRESULT Monet_FormatWideString(OLECHAR** ppwz, const WCHAR* format, ...);
void Monet_SafeReleaseIUnknown(IUnknown* punk);
BOOL Monet_IsEqualPropertySet(REFGUID a, REFGUID b);

HRESULT Config_InitializeDefaults(MonetConfig* cfg);
HRESULT Config_Load(MonetConfig* cfg, const CHAR* explicit_path);
BOOL Config_ResolveIniPath(CHAR* path, size_t cch_path);
BOOL Config_IsTraceEnabled(void);
void Log_Init(const MonetConfig* cfg);
void Log_Shutdown(void);
void Log_WriteA(int level, const CHAR* scope, const CHAR* format, ...);
void Log_WriteQueryA(const CHAR* scope, const CHAR* format, ...);
void Log_WriteResultA(const CHAR* scope, const CHAR* format, ...);
void Bootstrap_LogA(const CHAR* format, ...);

#define MONET_TRACE(scope, fmt, ...) \
    do { if (Config_IsTraceEnabled()) { Log_WriteA(MONET_LOG_TRACE, (scope), (fmt), __VA_ARGS__); } } while (0)

HRESULT Odbc_EnsureEnvironment(void);
void Odbc_ShutdownEnvironment(void);
HRESULT Odbc_OpenConnection(MonetDataSource* ds);
void Odbc_CloseConnection(MonetDataSource* ds);
HRESULT Odbc_ExecDirectA(SQLHDBC hdbc, const CHAR* sql, SQLHSTMT* phstmt);
HRESULT Odbc_PrepareA(SQLHDBC hdbc, const CHAR* sql, SQLHSTMT* phstmt);
void Odbc_CloseStatement(SQLHSTMT* phstmt);
HRESULT Odbc_DescribeColumns(SQLHSTMT hstmt, MonetColumnInfo** ppcolumns, DBORDINAL* pccolumns);
DBTYPE Odbc_MapSqlType(SQLSMALLINT sql_type);
HRESULT Odbc_GetErrorMessage(SQLSMALLINT handle_type, SQLHANDLE handle, HRESULT fallback, CHAR* buffer, size_t cch_buffer);

HRESULT AccessorTable_Init(MonetAccessorTable* table);
void AccessorTable_Destroy(MonetAccessorTable* table);
HRESULT AccessorTable_Create(MonetAccessorTable* table,
    DBACCESSORFLAGS flags,
    DBCOUNTITEM binding_count,
    const DBBINDING bindings[],
    DBLENGTH row_size,
    HACCESSOR* ph_accessor,
    DBBINDSTATUS rg_status[]);
HRESULT AccessorTable_AddRef(MonetAccessorTable* table, HACCESSOR h_accessor, DBREFCOUNT* pc_refcount);
HRESULT AccessorTable_Release(MonetAccessorTable* table, HACCESSOR h_accessor, DBREFCOUNT* pc_refcount);
HRESULT AccessorTable_GetBindings(MonetAccessorTable* table, HACCESSOR h_accessor, DBACCESSORFLAGS* pflags, DBCOUNTITEM* pcbindings, DBBINDING** pprgbindings);
MonetAccessor* AccessorTable_Find(MonetAccessorTable* table, HACCESSOR h_accessor);

HRESULT DataSource_CreateInstance(IUnknown* outer, REFIID riid, void** ppv);
HRESULT Session_Create(MonetDataSource* ds, IUnknown* outer, REFIID riid, void** ppv);
HRESULT Command_Create(MonetSession* session, REFIID riid, void** ppv);
HRESULT Rowset_Create(MonetSession* session, MonetCommand* command, SQLHSTMT hstmt, REFGUID schema_rowset, REFIID riid, void** ppv);
HRESULT Schema_CreateRowset(MonetSession* session, REFGUID schema, ULONG c_restrictions, const VARIANT restrictions[], REFIID riid, void** ppv);

HRESULT Rowset_FillBindings(MonetRowset* rowset, MonetBufferedRow* row, MonetAccessor* accessor, void* p_data);
void Rowset_FreeBufferedRow(MonetBufferedRow* row);

HRESULT Schema_GetSupported(ULONG* pc_schemas, GUID** prg_schemas, ULONG** prg_restrictions);
HRESULT Schema_BuildSql(REFGUID schema, const MonetConfig* cfg, ULONG c_restrictions, const VARIANT restrictions[], CHAR* sql, size_t cch_sql);

HRESULT Variant_CopyBstrOrEmpty(VARIANT* value, const WCHAR* text, BOOL required);
HRESULT Variant_FromLong(VARIANT* value, LONG number);
HRESULT Variant_FromBool(VARIANT* value, BOOL flag);
