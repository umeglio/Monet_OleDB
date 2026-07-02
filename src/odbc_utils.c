#include "monetdb_oledb.h"

static SQLHENV g_odbc_env = SQL_NULL_HENV;
static LONG g_odbc_initialized = 0;

static const CHAR* Odbc_SqlTypeName(SQLSMALLINT sql_type)
{
    switch (sql_type)
    {
    case SQL_CHAR: return "SQL_CHAR";
    case SQL_VARCHAR: return "SQL_VARCHAR";
    case SQL_LONGVARCHAR: return "SQL_LONGVARCHAR";
    case SQL_WCHAR: return "SQL_WCHAR";
    case SQL_WVARCHAR: return "SQL_WVARCHAR";
    case SQL_WLONGVARCHAR: return "SQL_WLONGVARCHAR";
    case SQL_DECIMAL: return "SQL_DECIMAL";
    case SQL_NUMERIC: return "SQL_NUMERIC";
    case SQL_SMALLINT: return "SQL_SMALLINT";
    case SQL_INTEGER: return "SQL_INTEGER";
    case SQL_REAL: return "SQL_REAL";
    case SQL_FLOAT: return "SQL_FLOAT";
    case SQL_DOUBLE: return "SQL_DOUBLE";
    case SQL_BIT: return "SQL_BIT";
    case SQL_TINYINT: return "SQL_TINYINT";
    case SQL_BIGINT: return "SQL_BIGINT";
    case SQL_BINARY: return "SQL_BINARY";
    case SQL_VARBINARY: return "SQL_VARBINARY";
    case SQL_LONGVARBINARY: return "SQL_LONGVARBINARY";
    case SQL_TYPE_DATE: return "SQL_TYPE_DATE";
    case SQL_TYPE_TIME: return "SQL_TYPE_TIME";
    case SQL_TYPE_TIMESTAMP: return "SQL_TYPE_TIMESTAMP";
    default: return "SQL_<other>";
    }
}

static const CHAR* Odbc_DbTypeName(DBTYPE type)
{
    switch (type)
    {
    case DBTYPE_BOOL: return "DBTYPE_BOOL";
    case DBTYPE_UI1: return "DBTYPE_UI1";
    case DBTYPE_I2: return "DBTYPE_I2";
    case DBTYPE_I4: return "DBTYPE_I4";
    case DBTYPE_I8: return "DBTYPE_I8";
    case DBTYPE_R4: return "DBTYPE_R4";
    case DBTYPE_R8: return "DBTYPE_R8";
    case DBTYPE_NUMERIC: return "DBTYPE_NUMERIC";
    case DBTYPE_STR: return "DBTYPE_STR";
    case DBTYPE_WSTR: return "DBTYPE_WSTR";
    case DBTYPE_BYTES: return "DBTYPE_BYTES";
    case DBTYPE_DBDATE: return "DBTYPE_DBDATE";
    case DBTYPE_DBTIME: return "DBTYPE_DBTIME";
    case DBTYPE_DBTIMESTAMP: return "DBTYPE_DBTIMESTAMP";
    default: return "DBTYPE_<other>";
    }
}

static DBLENGTH Odbc_NormalizeColumnSize(SQLSMALLINT sql_type, SQLULEN column_size)
{
    SQLULEN normalized = column_size;

    switch (sql_type)
    {
    case SQL_CHAR:
    case SQL_VARCHAR:
    case SQL_LONGVARCHAR:
        /*
         * MonetDB's ANSI character metadata can come back in 2-byte units.
         * Wide-character metadata (SQL_WVARCHAR, etc.) is already reported in
         * logical characters and must not be halved.
         */
        if (normalized > 0 && (normalized % 2U) == 0)
        {
            normalized /= 2U;
        }
        break;

    default:
        break;
    }

    return (DBLENGTH)normalized;
}

static SQLULEN Odbc_AdjustKnownZeroScaleNumericPrecision(const CHAR* column_name, SQLSMALLINT sql_type, SQLSMALLINT scale, SQLULEN column_size)
{
    if (!column_name ||
        (sql_type != SQL_DECIMAL && sql_type != SQL_NUMERIC) ||
        scale != 0)
    {
        return column_size;
    }

    /*
     * MonetDB ODBC may describe DECIMAL/NUMERIC result columns with the
     * current value display width. SQL Server linked servers compare this
     * runtime metadata with schema metadata and reject OPENQUERY targets.
     * Keep this correction scoped to the CMW zero-scale code fields.
     */
    MONET_UNUSED(column_size);

    if (_stricmp(column_name, "codpro") == 0)
    {
        return 12U;
    }
    if (_stricmp(column_name, "brcode") == 0 || _stricmp(column_name, "bcode") == 0)
    {
        return 14U;
    }

    return column_size;
}

static SQLULEN Odbc_NormalizeNumericPrecision(SQLSMALLINT sql_type, SQLSMALLINT scale, SQLULEN column_size)
{
    SQLULEN precision = column_size;
    MONET_UNUSED(scale);

    if (sql_type != SQL_DECIMAL && sql_type != SQL_NUMERIC)
    {
        return column_size;
    }

    /*
     * MonetDB ODBC sometimes reports DECIMAL/NUMERIC result metadata using
     * the current value display width at execution time. SQL Server linked
     * servers compare compile-time and run-time precision byte-for-byte and
     * raise Msg 7356 when runtime shrinks, e.g. DECIMAL(17, x) -> 10.
     *
     * Keep result metadata stable by never advertising DECIMAL/NUMERIC below
     * the precision used by the CMW monetary/quantity fields. This is safer
     * than trusting display width and less invasive than promoting all values
     * to NUMERIC(38, scale).
     */
    if (precision < 17U)
    {
        precision = 17U;
    }
    if (precision > 38U)
    {
        precision = 38U;
    }
    return precision;
}

static BOOL Odbc_GetFastFetchBinding(SQLSMALLINT sql_type, SQLULEN sql_size, DBLENGTH oledb_size, SQLSMALLINT* pc_type, SQLLEN* pbuffer_size)
{
    SQLSMALLINT c_type = 0;
    SQLLEN buffer_size = 0;
    SQLULEN string_chars = 0;

    if (!pc_type || !pbuffer_size)
    {
        return FALSE;
    }

    switch (sql_type)
    {
    case SQL_BIT:
        c_type = SQL_C_BIT;
        buffer_size = (SQLLEN)sizeof(SQLCHAR);
        break;

    case SQL_TINYINT:
        c_type = SQL_C_UTINYINT;
        buffer_size = (SQLLEN)sizeof(SQLCHAR);
        break;

    case SQL_SMALLINT:
        c_type = SQL_C_SSHORT;
        buffer_size = (SQLLEN)sizeof(SQLSMALLINT);
        break;

    case SQL_INTEGER:
        c_type = SQL_C_SLONG;
        buffer_size = (SQLLEN)sizeof(SQLINTEGER);
        break;

    case SQL_BIGINT:
        c_type = SQL_C_SBIGINT;
        buffer_size = (SQLLEN)sizeof(SQLBIGINT);
        break;

    case SQL_REAL:
        c_type = SQL_C_FLOAT;
        buffer_size = (SQLLEN)sizeof(SQLREAL);
        break;

    case SQL_CHAR:
    case SQL_VARCHAR:
        string_chars = (oledb_size > 0) ? (SQLULEN)oledb_size : sql_size;
        if (string_chars > 0 && string_chars <= MONETDB_MAX_FAST_STRING_CHARS)
        {
            c_type = SQL_C_CHAR;
            buffer_size = (SQLLEN)(string_chars + 1U);
        }
        break;

    case SQL_WCHAR:
    case SQL_WVARCHAR:
        string_chars = (oledb_size > 0) ? (SQLULEN)oledb_size : sql_size;
        if (string_chars > 0 && string_chars <= MONETDB_MAX_FAST_STRING_CHARS)
        {
            c_type = SQL_C_WCHAR;
            buffer_size = (SQLLEN)((string_chars + 1U) * sizeof(WCHAR));
        }
        break;

    case SQL_DOUBLE:
    case SQL_FLOAT:
        c_type = SQL_C_DOUBLE;
        buffer_size = (SQLLEN)sizeof(SQLDOUBLE);
        break;

    case SQL_DATE:
    case SQL_TYPE_DATE:
        c_type = SQL_C_TYPE_DATE;
        buffer_size = (SQLLEN)sizeof(DATE_STRUCT);
        break;

    case SQL_TIME:
    case SQL_TYPE_TIME:
        c_type = SQL_C_TYPE_TIME;
        buffer_size = (SQLLEN)sizeof(TIME_STRUCT);
        break;

    case SQL_TIMESTAMP:
    case SQL_TYPE_TIMESTAMP:
        c_type = SQL_C_TYPE_TIMESTAMP;
        buffer_size = (SQLLEN)sizeof(TIMESTAMP_STRUCT);
        break;

    default:
        break;
    }

    *pc_type = c_type;
    *pbuffer_size = buffer_size;
    return (c_type != 0 && buffer_size > 0);
}

static SQLULEN Odbc_ReadColumnAttributeUlen(SQLHSTMT hstmt, SQLUSMALLINT ordinal, SQLUSMALLINT field_id)
{
    SQLLEN numeric = 0;
    SQLRETURN rc = SQLColAttributeA(
        hstmt,
        ordinal,
        field_id,
        NULL,
        0,
        NULL,
        &numeric);

    if (!SQL_SUCCEEDED(rc) || numeric <= 0)
    {
        return 0;
    }

    return (SQLULEN)numeric;
}

static BOOL Odbc_ReadColumnAttributeLen(SQLHSTMT hstmt, SQLUSMALLINT ordinal, SQLUSMALLINT field_id, SQLLEN* pvalue)
{
    SQLLEN numeric = 0;
    SQLRETURN rc;

    if (!pvalue)
    {
        return FALSE;
    }

    rc = SQLColAttributeA(
        hstmt,
        ordinal,
        field_id,
        NULL,
        0,
        NULL,
        &numeric);

    if (!SQL_SUCCEEDED(rc))
    {
        return FALSE;
    }

    *pvalue = numeric;
    return TRUE;
}

static SQLULEN Odbc_ResolvePreparedColumnSize(SQLHSTMT hstmt, SQLUSMALLINT ordinal, SQLSMALLINT data_type, SQLULEN described_size)
{
    SQLULEN desc_length = described_size;
    SQLULEN attr_length = 0;
    SQLULEN display_size = 0;
    SQLULEN octet_length = 0;

    if (desc_length == 0)
    {
        attr_length = Odbc_ReadColumnAttributeUlen(hstmt, ordinal, SQL_DESC_LENGTH);
        desc_length = attr_length;
    }
    if (desc_length == 0)
    {
        display_size = Odbc_ReadColumnAttributeUlen(hstmt, ordinal, SQL_DESC_DISPLAY_SIZE);
        desc_length = display_size;
    }
    if (desc_length == 0)
    {
        octet_length = Odbc_ReadColumnAttributeUlen(hstmt, ordinal, SQL_DESC_OCTET_LENGTH);
        desc_length = octet_length;

        if (desc_length > 0 &&
            (data_type == SQL_WCHAR || data_type == SQL_WVARCHAR || data_type == SQL_WLONGVARCHAR))
        {
            desc_length /= sizeof(WCHAR);
        }
    }

    MONET_TRACE(
        "ODBC::DescribeColumns",
        "fallback col[%u] data_type=%s(%d) describe=%llu desc_length=%llu display=%llu octet=%llu resolved=%llu",
        (unsigned)ordinal,
        Odbc_SqlTypeName(data_type),
        (int)data_type,
        (unsigned long long)described_size,
        (unsigned long long)attr_length,
        (unsigned long long)display_size,
        (unsigned long long)octet_length,
        (unsigned long long)desc_length);

    return desc_length;
}

static HRESULT Odbc_HandleResult(SQLRETURN rc, SQLSMALLINT handle_type, SQLHANDLE handle, HRESULT fallback, const CHAR* scope)
{
    CHAR text[1024];

    if (SQL_SUCCEEDED(rc))
    {
        return S_OK;
    }

    Odbc_GetErrorMessage(handle_type, handle, fallback, text, MONET_ARRAY_SIZE(text));
    Log_WriteA(MONET_LOG_ERROR, scope, "%s", text);
    return fallback;
}

HRESULT Odbc_EnsureEnvironment(void)
{
    SQLRETURN rc;

    if (InterlockedCompareExchange(&g_odbc_initialized, 1, 0) == 0)
    {
        rc = SQLAllocHandle(SQL_HANDLE_ENV, SQL_NULL_HANDLE, &g_odbc_env);
        if (!SQL_SUCCEEDED(rc))
        {
            g_odbc_env = SQL_NULL_HENV;
            g_odbc_initialized = 0;
            return E_FAIL;
        }

        rc = SQLSetEnvAttr(g_odbc_env, SQL_ATTR_ODBC_VERSION, (SQLPOINTER)SQL_OV_ODBC3, 0);
        if (!SQL_SUCCEEDED(rc))
        {
            SQLFreeHandle(SQL_HANDLE_ENV, g_odbc_env);
            g_odbc_env = SQL_NULL_HENV;
            g_odbc_initialized = 0;
            return E_FAIL;
        }
    }

    return S_OK;
}

void Odbc_ShutdownEnvironment(void)
{
    if (InterlockedCompareExchange(&g_odbc_initialized, 0, 1) == 1)
    {
        if (g_odbc_env != SQL_NULL_HENV)
        {
            SQLFreeHandle(SQL_HANDLE_ENV, g_odbc_env);
            g_odbc_env = SQL_NULL_HENV;
        }
    }
}

HRESULT Odbc_OpenConnection(MonetDataSource* ds)
{
    SQLRETURN rc;
    CHAR datasource[MONETDB_MAX_NAME];
    CHAR user[MONETDB_MAX_NAME];
    CHAR password[MONETDB_MAX_NAME];

    if (!ds)
    {
        return E_POINTER;
    }

    if (ds->hdbc != SQL_NULL_HDBC)
    {
        return S_OK;
    }

    Odbc_EnsureEnvironment();
    rc = SQLAllocHandle(SQL_HANDLE_DBC, g_odbc_env, &ds->hdbc);
    if (!SQL_SUCCEEDED(rc))
    {
        ds->hdbc = SQL_NULL_HDBC;
        return E_FAIL;
    }

    Monet_WideToAnsi(datasource, MONET_ARRAY_SIZE(datasource), ds->init_datasource);
    Monet_WideToAnsi(user, MONET_ARRAY_SIZE(user), ds->auth_user);
    Monet_WideToAnsi(password, MONET_ARRAY_SIZE(password), ds->auth_password);

    SQLSetConnectAttr(ds->hdbc, SQL_ATTR_LOGIN_TIMEOUT, (SQLPOINTER)(INT_PTR)ds->config.connection_timeout, 0);
    SQLSetConnectAttr(ds->hdbc, SQL_ATTR_ACCESS_MODE, (SQLPOINTER)(INT_PTR)(ds->config.read_only ? SQL_MODE_READ_ONLY : SQL_MODE_READ_WRITE), 0);
    SQLSetConnectAttr(ds->hdbc, SQL_ATTR_AUTOCOMMIT, (SQLPOINTER)(INT_PTR)(ds->config.autocommit ? SQL_AUTOCOMMIT_ON : SQL_AUTOCOMMIT_OFF), 0);

    rc = SQLConnectA(
        ds->hdbc,
        (SQLCHAR*)datasource,
        SQL_NTS,
        user[0] ? (SQLCHAR*)user : NULL,
        user[0] ? SQL_NTS : 0,
        password[0] ? (SQLCHAR*)password : NULL,
        password[0] ? SQL_NTS : 0);
    if (!SQL_SUCCEEDED(rc))
    {
        Odbc_HandleResult(rc, SQL_HANDLE_DBC, ds->hdbc, DB_SEC_E_AUTH_FAILED, "ODBC::Connect");
        SQLFreeHandle(SQL_HANDLE_DBC, ds->hdbc);
        ds->hdbc = SQL_NULL_HDBC;
        return DB_SEC_E_AUTH_FAILED;
    }

    Log_WriteA(MONET_LOG_INFO, "ODBC::Connect", "Connessione ODBC aperta verso DSN='%s' catalog='%S'", datasource, ds->init_catalog);
    return S_OK;
}

void Odbc_CloseConnection(MonetDataSource* ds)
{
    if (!ds)
    {
        return;
    }

    if (ds->hdbc != SQL_NULL_HDBC)
    {
        SQLDisconnect(ds->hdbc);
        SQLFreeHandle(SQL_HANDLE_DBC, ds->hdbc);
        ds->hdbc = SQL_NULL_HDBC;
    }
}

HRESULT Odbc_ExecDirectA(SQLHDBC hdbc, const CHAR* sql, SQLHSTMT* phstmt)
{
    SQLRETURN rc;
    SQLHSTMT hstmt = SQL_NULL_HSTMT;

    if (!sql || !phstmt)
    {
        return E_POINTER;
    }

    *phstmt = SQL_NULL_HSTMT;
    rc = SQLAllocHandle(SQL_HANDLE_STMT, hdbc, &hstmt);
    if (!SQL_SUCCEEDED(rc))
    {
        return E_FAIL;
    }

    SQLSetStmtAttr(hstmt, SQL_ATTR_QUERY_TIMEOUT, (SQLPOINTER)(INT_PTR)120, 0);
    MONET_TRACE("ODBC::ExecDirect", "sql=\"%s\"", sql);
    Log_WriteQueryA("ODBC::ExecDirect", "sql=\"%s\"", sql);
    rc = SQLExecDirectA(hstmt, (SQLCHAR*)sql, SQL_NTS);
    if (!SQL_SUCCEEDED(rc))
    {
        Odbc_HandleResult(rc, SQL_HANDLE_STMT, hstmt, DB_E_ERRORSINCOMMAND, "ODBC::ExecDirect");
        SQLFreeHandle(SQL_HANDLE_STMT, hstmt);
        return DB_E_ERRORSINCOMMAND;
    }

    *phstmt = hstmt;
    return S_OK;
}

HRESULT Odbc_PrepareA(SQLHDBC hdbc, const CHAR* sql, SQLHSTMT* phstmt)
{
    SQLRETURN rc;
    SQLHSTMT hstmt = SQL_NULL_HSTMT;

    if (!sql || !phstmt)
    {
        return E_POINTER;
    }

    *phstmt = SQL_NULL_HSTMT;
    rc = SQLAllocHandle(SQL_HANDLE_STMT, hdbc, &hstmt);
    if (!SQL_SUCCEEDED(rc))
    {
        return E_FAIL;
    }

    MONET_TRACE("ODBC::Prepare", "sql=\"%s\"", sql);
    Log_WriteQueryA("ODBC::Prepare", "sql=\"%s\"", sql);
    rc = SQLPrepareA(hstmt, (SQLCHAR*)sql, SQL_NTS);
    if (!SQL_SUCCEEDED(rc))
    {
        Odbc_HandleResult(rc, SQL_HANDLE_STMT, hstmt, DB_E_ERRORSINCOMMAND, "ODBC::Prepare");
        SQLFreeHandle(SQL_HANDLE_STMT, hstmt);
        return DB_E_ERRORSINCOMMAND;
    }

    *phstmt = hstmt;
    return S_OK;
}

void Odbc_CloseStatement(SQLHSTMT* phstmt)
{
    if (phstmt && *phstmt != SQL_NULL_HSTMT)
    {
        SQLFreeHandle(SQL_HANDLE_STMT, *phstmt);
        *phstmt = SQL_NULL_HSTMT;
    }
}

DBTYPE Odbc_MapSqlType(SQLSMALLINT sql_type)
{
    switch (sql_type)
    {
    case SQL_TINYINT:
        return DBTYPE_UI1;
    case SQL_SMALLINT:
        return DBTYPE_I2;
    case SQL_INTEGER:
        return DBTYPE_I4;
    case SQL_BIGINT:
        return DBTYPE_I8;
    case SQL_REAL:
        return DBTYPE_R4;
    case SQL_DOUBLE:
    case SQL_FLOAT:
        return DBTYPE_R8;
    case SQL_DECIMAL:
    case SQL_NUMERIC:
        return DBTYPE_NUMERIC;
    case SQL_CHAR:
    case SQL_VARCHAR:
    case SQL_LONGVARCHAR:
        return DBTYPE_STR;
    case SQL_WCHAR:
    case SQL_WVARCHAR:
    case SQL_WLONGVARCHAR:
        return DBTYPE_WSTR;
    case SQL_DATE:
    case SQL_TYPE_DATE:
        return DBTYPE_DBDATE;
    case SQL_TIME:
    case SQL_TYPE_TIME:
        return DBTYPE_DBTIME;
    case SQL_TIMESTAMP:
    case SQL_TYPE_TIMESTAMP:
        return DBTYPE_DBTIMESTAMP;
    case SQL_BINARY:
    case SQL_VARBINARY:
    case SQL_LONGVARBINARY:
        return DBTYPE_BYTES;
    case SQL_BIT:
        return DBTYPE_BOOL;
    default:
        return DBTYPE_STR;
    }
}

HRESULT Odbc_DescribeColumns(SQLHSTMT hstmt, MonetColumnInfo** ppcolumns, DBORDINAL* pccolumns)
{
    SQLSMALLINT col_count = 0;
    SQLUSMALLINT i = 0;
    MonetColumnInfo* columns = NULL;
    SQLRETURN rc;

    if (!ppcolumns || !pccolumns)
    {
        return E_POINTER;
    }

    *ppcolumns = NULL;
    *pccolumns = 0;

    rc = SQLNumResultCols(hstmt, &col_count);
    if (!SQL_SUCCEEDED(rc))
    {
        return DB_E_ERRORSINCOMMAND;
    }

    if (col_count <= 0)
    {
        return S_OK;
    }

    columns = (MonetColumnInfo*)CoTaskMemAlloc(sizeof(MonetColumnInfo) * col_count);
    if (!columns)
    {
        return E_OUTOFMEMORY;
    }
    ZeroMemory(columns, sizeof(MonetColumnInfo) * col_count);

    for (i = 0; i < (SQLUSMALLINT)col_count; ++i)
    {
        SQLCHAR name[MONETDB_MAX_NAME];
        SQLSMALLINT name_len = 0;
        SQLSMALLINT data_type = 0;
        SQLULEN column_size = 0;
        SQLSMALLINT decimal_digits = 0;
        SQLSMALLINT nullable = 0;
        MonetColumnInfo* col = &columns[i];
        DBLENGTH oledb_size = 0;

        rc = SQLDescribeColA(
            hstmt,
            (SQLUSMALLINT)(i + 1),
            name,
            (SQLSMALLINT)MONET_ARRAY_SIZE(name),
            &name_len,
            &data_type,
            &column_size,
            &decimal_digits,
            &nullable);
        if (!SQL_SUCCEEDED(rc))
        {
            CoTaskMemFree(columns);
            return DB_E_ERRORSINCOMMAND;
        }

        column_size = Odbc_ResolvePreparedColumnSize(hstmt, (SQLUSMALLINT)(i + 1), data_type, column_size);

        if (data_type == SQL_DECIMAL || data_type == SQL_NUMERIC)
        {
            SQLLEN precision_attr = 0;
            SQLLEN scale_attr = 0;

            /*
             * MonetDB ODBC can report SQLDescribeCol column_size as the
             * current display width for DECIMAL/NUMERIC result columns
             * (for example 6 for values like 953140), while SQL Server
             * compares OPENQUERY runtime metadata with the declared numeric
             * precision from schema rowsets. Use descriptor precision/scale
             * when available so compile-time and runtime metadata match.
             */
            if (Odbc_ReadColumnAttributeLen(hstmt, (SQLUSMALLINT)(i + 1), SQL_DESC_PRECISION, &precision_attr) &&
                precision_attr > 0 &&
                (SQLULEN)precision_attr > column_size)
            {
                column_size = (SQLULEN)precision_attr;
            }

            if (Odbc_ReadColumnAttributeLen(hstmt, (SQLUSMALLINT)(i + 1), SQL_DESC_SCALE, &scale_attr) &&
                scale_attr >= 0 &&
                scale_attr <= 255)
            {
                decimal_digits = (SQLSMALLINT)scale_attr;
            }
        }

        name[MONET_ARRAY_SIZE(name) - 1] = '\0';
        column_size = Odbc_AdjustKnownZeroScaleNumericPrecision((const CHAR*)name, data_type, decimal_digits, column_size);
        column_size = Odbc_NormalizeNumericPrecision(data_type, decimal_digits, column_size);
        Monet_StringCopyA(col->name_a, MONET_ARRAY_SIZE(col->name_a), (const CHAR*)name);
        Monet_AnsiToWide(col->name_w, MONET_ARRAY_SIZE(col->name_w), col->name_a);
        col->ordinal = i + 1;
        col->type = Odbc_MapSqlType(data_type);
        col->flags = DBCOLUMNFLAGS_ISNULLABLE;
        if (nullable == SQL_NO_NULLS)
        {
            col->flags &= ~DBCOLUMNFLAGS_ISNULLABLE;
        }
        if (i == 0)
        {
            col->flags |= DBCOLUMNFLAGS_MAYBENULL;
        }
        oledb_size = Odbc_NormalizeColumnSize(data_type, column_size);
        col->column_size = oledb_size;
        col->precision = (BYTE)((oledb_size > 255) ? 38 : oledb_size);
        col->scale = (BYTE)((decimal_digits < 0) ? 0 : decimal_digits);
        col->sql_type = data_type;
        col->sql_size = column_size;
        col->sql_scale = decimal_digits;
        col->sql_nullable = nullable;
        col->fetch_c_type = 0;
        col->fetch_buffer_size = 0;
        Odbc_GetFastFetchBinding(data_type, column_size, oledb_size, &col->fetch_c_type, &col->fetch_buffer_size);

        MONET_TRACE(
            "ODBC::DescribeColumns",
            "col[%u] name='%s' sql_type=%s(%d) raw_size=%llu oledb_size=%ld precision=%u scale=%u nullable=%d mapped=%s fetch_c_type=%d fetch_cb=%ld",
            (unsigned)(i + 1),
            col->name_a,
            Odbc_SqlTypeName(data_type),
            (int)data_type,
            (unsigned long long)column_size,
            (long)oledb_size,
            (unsigned)col->precision,
            (unsigned)col->scale,
            (int)nullable,
            Odbc_DbTypeName(col->type),
            (int)col->fetch_c_type,
            (long)col->fetch_buffer_size);
    }

    *ppcolumns = columns;
    *pccolumns = (DBORDINAL)col_count;
    return S_OK;
}

HRESULT Odbc_GetErrorMessage(SQLSMALLINT handle_type, SQLHANDLE handle, HRESULT fallback, CHAR* buffer, size_t cch_buffer)
{
    SQLCHAR sql_state[8];
    SQLINTEGER native_error = 0;
    SQLCHAR message[512];
    SQLSMALLINT message_len = 0;
    SQLRETURN rc;

    if (!buffer || cch_buffer == 0)
    {
        return E_POINTER;
    }

    buffer[0] = '\0';
    rc = SQLGetDiagRecA(handle_type, handle, 1, sql_state, &native_error, message, (SQLSMALLINT)MONET_ARRAY_SIZE(message), &message_len);
    if (SQL_SUCCEEDED(rc))
    {
        _snprintf(buffer, cch_buffer, "HRESULT=0x%08lx SQLSTATE=%s Native=%ld Message=%s",
            (unsigned long)fallback,
            (const CHAR*)sql_state,
            (long)native_error,
            (const CHAR*)message);
        buffer[cch_buffer - 1] = '\0';
    }
    else
    {
        _snprintf(buffer, cch_buffer, "HRESULT=0x%08lx ODBC error senza dettagli diagnostici", (unsigned long)fallback);
        buffer[cch_buffer - 1] = '\0';
    }

    return S_OK;
}
