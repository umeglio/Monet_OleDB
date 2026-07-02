#include "monetdb_oledb.h"

typedef struct MonetSchemaSupport
{
    const GUID* schema;
    ULONG restrictions;
} MonetSchemaSupport;

static const MonetSchemaSupport g_schema_support[MONETDB_SCHEMA_COUNT] =
{
    { &DBSCHEMA_CATALOGS, 0x1 },
    { &DBSCHEMA_SCHEMATA, 0x3 },
    { &DBSCHEMA_TABLES, 0xF },
    { &DBSCHEMA_VIEWS, 0x7 },
    { &DBSCHEMA_COLUMNS, 0xF },
    { &DBSCHEMA_VIEW_COLUMN_USAGE, 0x7 },
    { &DBSCHEMA_PROCEDURES, 0x7 },
    { &DBSCHEMA_PROCEDURE_PARAMETERS, 0xF },
    { &DBSCHEMA_PRIMARY_KEYS, 0x7 },
    { &DBSCHEMA_FOREIGN_KEYS, 0x7 },
    { &DBSCHEMA_INDEXES, 0x7 },
    { &DBSCHEMA_PROVIDER_TYPES, 0x1 },
    { &DBSCHEMA_TABLE_STATISTICS, 0x7 }
};

static BOOL Schema_VariantToAnsi(const VARIANT* value, CHAR* buffer, size_t cch_buffer)
{
    if (!value || !buffer || cch_buffer == 0)
    {
        return FALSE;
    }
    buffer[0] = '\0';

    switch (value->vt)
    {
    case VT_EMPTY:
    case VT_NULL:
        return FALSE;
    case VT_BSTR:
        Monet_WideToAnsi(buffer, cch_buffer, value->bstrVal);
        return buffer[0] != '\0';
    case VT_I2:
        _snprintf(buffer, cch_buffer, "%d", value->iVal);
        buffer[cch_buffer - 1] = '\0';
        return TRUE;
    case VT_I4:
        _snprintf(buffer, cch_buffer, "%ld", value->lVal);
        buffer[cch_buffer - 1] = '\0';
        return TRUE;
    default:
        return FALSE;
    }
}

static void Schema_Append(CHAR* sql, size_t cch_sql, const CHAR* text)
{
    size_t current = strlen(sql);
    if (current + strlen(text) + 1 < cch_sql)
    {
        strcat(sql, text);
    }
}

static void Schema_AppendWhereEquals(CHAR* sql, size_t cch_sql, BOOL* where_started, const CHAR* expression, const VARIANT* value)
{
    CHAR temp[MONETDB_MAX_NAME * 2];
    if (!Schema_VariantToAnsi(value, temp, MONET_ARRAY_SIZE(temp)))
    {
        return;
    }
    Schema_Append(sql, cch_sql, *where_started ? " AND " : " WHERE ");
    *where_started = TRUE;
    Schema_Append(sql, cch_sql, expression);
    Schema_Append(sql, cch_sql, " = '");
    Schema_Append(sql, cch_sql, temp);
    Schema_Append(sql, cch_sql, "'");
}

HRESULT Schema_GetSupported(ULONG* pc_schemas, GUID** prg_schemas, ULONG** prg_restrictions)
{
    GUID* schemas = NULL;
    ULONG* restrictions = NULL;
    ULONG i;

    if (!pc_schemas || !prg_schemas || !prg_restrictions)
    {
        return E_POINTER;
    }
    *pc_schemas = 0;
    *prg_schemas = NULL;
    *prg_restrictions = NULL;

    schemas = (GUID*)CoTaskMemAlloc(sizeof(GUID) * MONETDB_SCHEMA_COUNT);
    restrictions = (ULONG*)CoTaskMemAlloc(sizeof(ULONG) * MONETDB_SCHEMA_COUNT);
    if (!schemas || !restrictions)
    {
        CoTaskMemFree(schemas);
        CoTaskMemFree(restrictions);
        return E_OUTOFMEMORY;
    }

    for (i = 0; i < MONETDB_SCHEMA_COUNT; ++i)
    {
        schemas[i] = *g_schema_support[i].schema;
        restrictions[i] = g_schema_support[i].restrictions;
    }

    *pc_schemas = MONETDB_SCHEMA_COUNT;
    *prg_schemas = schemas;
    *prg_restrictions = restrictions;
    return S_OK;
}

HRESULT Schema_BuildSql(REFGUID schema, const MonetConfig* cfg, ULONG c_restrictions, const VARIANT restrictions[], CHAR* sql, size_t cch_sql)
{
    BOOL where_started = FALSE;

    if (!cfg || !sql || cch_sql == 0)
    {
        return E_POINTER;
    }
    sql[0] = '\0';

    if (IsEqualGUID(schema, &DBSCHEMA_CATALOGS))
    {
        _snprintf(sql, cch_sql, "SELECT '%s' AS TABLE_CATALOG", cfg->database);
    }
    else if (IsEqualGUID(schema, &DBSCHEMA_SCHEMATA))
    {
        _snprintf(sql, cch_sql,
            "SELECT '%s' AS CATALOG_NAME, s.name AS SCHEMA_NAME, CAST(NULL AS VARCHAR(128)) AS SCHEMA_OWNER "
            "FROM sys.schemas s", cfg->database);
        if (c_restrictions > 1)
        {
            Schema_AppendWhereEquals(sql, cch_sql, &where_started, "s.name", &restrictions[1]);
        }
    }
    else if (IsEqualGUID(schema, &DBSCHEMA_TABLES))
    {
        _snprintf(sql, cch_sql,
            "SELECT '%s' AS TABLE_CATALOG, "
            "s.name AS TABLE_SCHEMA, "
            "t.name AS TABLE_NAME, "
            "CASE WHEN t.type = 1 THEN 'VIEW' ELSE 'TABLE' END AS TABLE_TYPE, "
            "CAST(NULL AS VARCHAR(36)) AS TABLE_GUID, "
            "CAST(NULL AS VARCHAR(255)) AS DESCRIPTION, "
            "CAST(NULL AS INTEGER) AS TABLE_PROPID, "
            "CAST(NULL AS DATE) AS DATE_CREATED, "
            "CAST(NULL AS DATE) AS DATE_MODIFIED "
            "FROM sys.tables t "
            "JOIN sys.schemas s ON s.id = t.schema_id "
            "WHERE t.type IN (0, 1)", cfg->database);
        where_started = TRUE;
        if (c_restrictions > 1) Schema_AppendWhereEquals(sql, cch_sql, &where_started, "s.name", &restrictions[1]);
        if (c_restrictions > 2) Schema_AppendWhereEquals(sql, cch_sql, &where_started, "t.name", &restrictions[2]);
        if (c_restrictions > 3) Schema_AppendWhereEquals(sql, cch_sql, &where_started, "CASE WHEN t.type = 1 THEN 'VIEW' ELSE 'TABLE' END", &restrictions[3]);
    }
    else if (IsEqualGUID(schema, &DBSCHEMA_VIEWS))
    {
        _snprintf(sql, cch_sql,
            "SELECT '%s' AS TABLE_CATALOG, s.name AS TABLE_SCHEMA, t.name AS TABLE_NAME, 'VIEW' AS TABLE_TYPE "
            "FROM sys.tables t JOIN sys.schemas s ON s.id = t.schema_id WHERE t.type = 1", cfg->database);
        where_started = TRUE;
        if (c_restrictions > 1) Schema_AppendWhereEquals(sql, cch_sql, &where_started, "s.name", &restrictions[1]);
        if (c_restrictions > 2) Schema_AppendWhereEquals(sql, cch_sql, &where_started, "t.name", &restrictions[2]);
    }
    else if (IsEqualGUID(schema, &DBSCHEMA_COLUMNS))
    {
        _snprintf(sql, cch_sql,
            "SELECT '%s' AS TABLE_CATALOG, "
            "s.name AS TABLE_SCHEMA, "
            "t.name AS TABLE_NAME, "
            "c.name AS COLUMN_NAME, "
            "CAST(NULL AS VARCHAR(36)) AS COLUMN_GUID, "
            "CAST(NULL AS INTEGER) AS COLUMN_PROPID, "
            "CAST(c.number + 1 AS INTEGER) AS ORDINAL_POSITION, "
            "CASE WHEN c.\"default\" IS NULL THEN CAST(0 AS SMALLINT) ELSE CAST(1 AS SMALLINT) END AS COLUMN_HASDEFAULT, "
            "CAST(c.\"default\" AS VARCHAR(255)) AS COLUMN_DEFAULT, "
            "CASE WHEN c.\"null\" THEN CAST(96 AS INTEGER) ELSE CAST(0 AS INTEGER) END AS COLUMN_FLAGS, "
            "CASE WHEN c.\"null\" THEN CAST(1 AS SMALLINT) ELSE CAST(0 AS SMALLINT) END AS IS_NULLABLE, "
            "CASE "
                "WHEN lower(c.type) = 'tinyint' THEN 17 "
                "WHEN lower(c.type) = 'smallint' THEN 2 "
                "WHEN lower(c.type) IN ('int', 'integer') THEN 3 "
                "WHEN lower(c.type) = 'bigint' THEN 20 "
                "WHEN lower(c.type) = 'real' THEN 4 "
                "WHEN lower(c.type) IN ('double', 'float') THEN 5 "
                "WHEN lower(c.type) IN ('decimal', 'numeric') THEN 131 "
                "WHEN lower(c.type) IN ('char', 'varchar', 'clob') OR lower(c.type) LIKE '%%interval%%' THEN 129 "
                "WHEN lower(c.type) = 'date' THEN 133 "
                "WHEN lower(c.type) = 'time' THEN 134 "
                "WHEN lower(c.type) = 'timestamp' THEN 135 "
                "WHEN lower(c.type) = 'blob' THEN 128 "
                "WHEN lower(c.type) IN ('boolean', 'bool') THEN 11 "
                "ELSE 129 "
            "END AS DATA_TYPE, "
            "CAST(NULL AS VARCHAR(36)) AS TYPE_GUID, "
            "CASE "
                "WHEN lower(c.type) IN ('char', 'varchar', 'clob', 'blob') THEN c.type_digits "
                "WHEN lower(c.type) = 'tinyint' THEN 3 "
                "WHEN lower(c.type) = 'smallint' THEN 5 "
                "WHEN lower(c.type) IN ('int', 'integer') THEN 10 "
                "WHEN lower(c.type) = 'bigint' THEN 19 "
                "WHEN lower(c.type) = 'real' THEN 7 "
                "WHEN lower(c.type) IN ('double', 'float') THEN 15 "
                "WHEN lower(c.type) IN ('decimal', 'numeric') THEN c.type_digits "
                "WHEN lower(c.type) = 'date' THEN 10 "
                "WHEN lower(c.type) = 'time' THEN 8 "
                "WHEN lower(c.type) = 'timestamp' THEN 26 "
                "ELSE CAST(NULL AS INTEGER) "
            "END AS CHARACTER_MAXIMUM_LENGTH, "
            "CASE "
                "WHEN lower(c.type) IN ('char', 'varchar', 'clob', 'blob') THEN c.type_digits "
                "ELSE CAST(NULL AS INTEGER) "
            "END AS CHARACTER_OCTET_LENGTH, "
            "CASE "
                "WHEN lower(c.type) = 'tinyint' THEN 3 "
                "WHEN lower(c.type) = 'smallint' THEN 5 "
                "WHEN lower(c.type) IN ('int', 'integer') THEN 10 "
                "WHEN lower(c.type) = 'bigint' THEN 19 "
                "WHEN lower(c.type) = 'real' THEN 7 "
                "WHEN lower(c.type) IN ('double', 'float') THEN 15 "
                "WHEN lower(c.type) IN ('decimal', 'numeric') THEN c.type_digits "
                "ELSE CAST(NULL AS SMALLINT) "
            "END AS NUMERIC_PRECISION, "
            "CASE "
                "WHEN lower(c.type) IN ('decimal', 'numeric') THEN c.type_scale "
                "ELSE CAST(NULL AS SMALLINT) "
            "END AS NUMERIC_SCALE, "
            "CASE "
                "WHEN lower(c.type) IN ('time', 'timestamp') THEN c.type_scale "
                "ELSE CAST(NULL AS INTEGER) "
            "END AS DATETIME_PRECISION, "
            "CAST(NULL AS VARCHAR(255)) AS CHARACTER_SET_CATALOG, "
            "CAST(NULL AS VARCHAR(255)) AS CHARACTER_SET_SCHEMA, "
            "CAST(NULL AS VARCHAR(255)) AS CHARACTER_SET_NAME, "
            "CAST(NULL AS VARCHAR(255)) AS COLLATION_CATALOG, "
            "CAST(NULL AS VARCHAR(255)) AS COLLATION_SCHEMA, "
            "CAST(NULL AS VARCHAR(255)) AS COLLATION_NAME, "
            "CAST(NULL AS VARCHAR(255)) AS DOMAIN_CATALOG, "
            "CAST(NULL AS VARCHAR(255)) AS DOMAIN_SCHEMA, "
            "CAST(NULL AS VARCHAR(255)) AS DOMAIN_NAME, "
            "CAST(NULL AS VARCHAR(255)) AS DESCRIPTION "
            "FROM sys.columns c "
            "JOIN sys.tables t ON t.id = c.table_id "
            "JOIN sys.schemas s ON s.id = t.schema_id "
            "WHERE 1 = 1", cfg->database);
        where_started = TRUE;
        if (c_restrictions > 1) Schema_AppendWhereEquals(sql, cch_sql, &where_started, "s.name", &restrictions[1]);
        if (c_restrictions > 2) Schema_AppendWhereEquals(sql, cch_sql, &where_started, "t.name", &restrictions[2]);
        if (c_restrictions > 3) Schema_AppendWhereEquals(sql, cch_sql, &where_started, "c.name", &restrictions[3]);
        if (strlen(sql) + 80 < cch_sql)
        {
            strcat(sql, " ORDER BY TABLE_SCHEMA, TABLE_NAME, ORDINAL_POSITION");
        }
    }
    else if (IsEqualGUID(schema, &DBSCHEMA_VIEW_COLUMN_USAGE))
    {
        _snprintf(sql, cch_sql,
            "SELECT '%s' AS VIEW_CATALOG, s.name AS VIEW_SCHEMA, t.name AS VIEW_NAME, c.name AS COLUMN_NAME "
            "FROM sys.columns c "
            "JOIN sys.tables t ON t.id = c.table_id "
            "JOIN sys.schemas s ON s.id = t.schema_id "
            "WHERE t.type = 1", cfg->database);
        where_started = TRUE;
        if (c_restrictions > 1) Schema_AppendWhereEquals(sql, cch_sql, &where_started, "s.name", &restrictions[1]);
        if (c_restrictions > 2) Schema_AppendWhereEquals(sql, cch_sql, &where_started, "t.name", &restrictions[2]);
    }
    else if (IsEqualGUID(schema, &DBSCHEMA_PROCEDURES))
    {
        _snprintf(sql, cch_sql,
            "SELECT '%s' AS PROCEDURE_CATALOG, s.name AS PROCEDURE_SCHEMA, f.name AS PROCEDURE_NAME, "
            "CASE WHEN f.type = 2 THEN 'FUNCTION' ELSE 'PROCEDURE' END AS PROCEDURE_TYPE "
            "FROM sys.functions f JOIN sys.schemas s ON s.id = f.schema_id WHERE 1 = 1", cfg->database);
        where_started = TRUE;
        if (c_restrictions > 1) Schema_AppendWhereEquals(sql, cch_sql, &where_started, "s.name", &restrictions[1]);
        if (c_restrictions > 2) Schema_AppendWhereEquals(sql, cch_sql, &where_started, "f.name", &restrictions[2]);
    }
    else if (IsEqualGUID(schema, &DBSCHEMA_PROCEDURE_PARAMETERS))
    {
        _snprintf(sql, cch_sql,
            "SELECT '%s' AS PROCEDURE_CATALOG, s.name AS PROCEDURE_SCHEMA, f.name AS PROCEDURE_NAME, "
            "a.name AS PARAMETER_NAME, a.number AS ORDINAL_POSITION, ty.sqlname AS TYPE_NAME "
            "FROM sys.args a "
            "JOIN sys.functions f ON f.id = a.func_id "
            "JOIN sys.schemas s ON s.id = f.schema_id "
            "LEFT JOIN sys.types ty ON ty.id = a.type "
            "WHERE 1 = 1", cfg->database);
        where_started = TRUE;
        if (c_restrictions > 1) Schema_AppendWhereEquals(sql, cch_sql, &where_started, "s.name", &restrictions[1]);
        if (c_restrictions > 2) Schema_AppendWhereEquals(sql, cch_sql, &where_started, "f.name", &restrictions[2]);
        if (c_restrictions > 3) Schema_AppendWhereEquals(sql, cch_sql, &where_started, "a.name", &restrictions[3]);
    }
    else if (IsEqualGUID(schema, &DBSCHEMA_PRIMARY_KEYS))
    {
        _snprintf(sql, cch_sql,
            "SELECT '%s' AS \"TABLE_CATALOG\", "
            "s.name AS \"TABLE_SCHEMA\", "
            "t.name AS \"TABLE_NAME\", "
            "c.name AS \"COLUMN_NAME\", "
            "CAST(NULL AS VARCHAR(36)) AS \"COLUMN_GUID\", "
            "CAST(NULL AS INTEGER) AS \"COLUMN_PROPID\", "
            "CAST(o.nr + 1 AS INTEGER) AS \"ORDINAL\", "
            "k.name AS \"PK_NAME\" "
            "FROM sys.keys k "
            "JOIN sys.tables t ON t.id = k.table_id "
            "JOIN sys.schemas s ON s.id = t.schema_id "
            "JOIN sys.objects o ON o.id = k.id "
            "JOIN sys.columns c ON c.table_id = t.id AND c.number = o.nr "
            "WHERE k.type = 0", cfg->database);
        where_started = TRUE;
        if (c_restrictions > 1) Schema_AppendWhereEquals(sql, cch_sql, &where_started, "s.name", &restrictions[1]);
        if (c_restrictions > 2) Schema_AppendWhereEquals(sql, cch_sql, &where_started, "t.name", &restrictions[2]);
    }
    else if (IsEqualGUID(schema, &DBSCHEMA_FOREIGN_KEYS))
    {
        _snprintf(sql, cch_sql,
            "SELECT '%s' AS \"PK_TABLE_CATALOG\", "
            "pks.name AS \"PK_TABLE_SCHEMA\", "
            "pkt.name AS \"PK_TABLE_NAME\", "
            "CAST(NULL AS VARCHAR(255)) AS \"PK_COLUMN_NAME\", "
            "CAST(NULL AS VARCHAR(36)) AS \"PK_COLUMN_GUID\", "
            "CAST(NULL AS INTEGER) AS \"PK_COLUMN_PROPID\", "
            "'%s' AS \"FK_TABLE_CATALOG\", "
            "fks.name AS \"FK_TABLE_SCHEMA\", "
            "fkt.name AS \"FK_TABLE_NAME\", "
            "CAST(NULL AS VARCHAR(255)) AS \"FK_COLUMN_NAME\", "
            "CAST(NULL AS VARCHAR(36)) AS \"FK_COLUMN_GUID\", "
            "CAST(NULL AS INTEGER) AS \"FK_COLUMN_PROPID\", "
            "CAST(1 AS INTEGER) AS \"ORDINAL\", "
            "CAST(NULL AS VARCHAR(16)) AS \"UPDATE_RULE\", "
            "CAST(NULL AS VARCHAR(16)) AS \"DELETE_RULE\", "
            "CAST(NULL AS VARCHAR(255)) AS \"PK_NAME\", "
            "k.name AS \"FK_NAME\", "
            "CAST(NULL AS SMALLINT) AS \"DEFERRABILITY\" "
            "FROM sys.keys k "
            "JOIN sys.tables fkt ON fkt.id = k.table_id "
            "JOIN sys.schemas fks ON fks.id = fkt.schema_id "
            "LEFT JOIN sys.tables pkt ON pkt.id = k.rkey "
            "LEFT JOIN sys.schemas pks ON pks.id = pkt.schema_id "
            "WHERE k.type = 2", cfg->database, cfg->database);
        where_started = TRUE;
        if (c_restrictions > 1) Schema_AppendWhereEquals(sql, cch_sql, &where_started, "fks.name", &restrictions[1]);
        if (c_restrictions > 2) Schema_AppendWhereEquals(sql, cch_sql, &where_started, "fkt.name", &restrictions[2]);
    }
    else if (IsEqualGUID(schema, &DBSCHEMA_INDEXES))
    {
        _snprintf(sql, cch_sql,
            "SELECT '%s' AS \"TABLE_CATALOG\", "
            "s.name AS \"TABLE_SCHEMA\", "
            "t.name AS \"TABLE_NAME\", "
            "'%s' AS \"INDEX_CATALOG\", "
            "s.name AS \"INDEX_SCHEMA\", "
            "i.name AS \"INDEX_NAME\", "
            "CAST(0 AS SMALLINT) AS \"PRIMARY_KEY\", "
            "CAST(0 AS SMALLINT) AS \"UNIQUE\", "
            "CAST(0 AS SMALLINT) AS \"CLUSTERED\", "
            "CAST(NULL AS SMALLINT) AS \"TYPE\", "
            "CAST(NULL AS INTEGER) AS \"FILL_FACTOR\", "
            "CAST(NULL AS INTEGER) AS \"INITIAL_SIZE\", "
            "CAST(NULL AS INTEGER) AS \"NULLS\", "
            "CAST(NULL AS SMALLINT) AS \"SORT_BOOKMARKS\", "
            "CAST(NULL AS SMALLINT) AS \"AUTO_UPDATE\", "
            "CAST(NULL AS INTEGER) AS \"NULL_COLLATION\", "
            "CAST(o.nr + 1 AS INTEGER) AS \"ORDINAL_POSITION\", "
            "o.name AS \"COLUMN_NAME\", "
            "CAST(NULL AS VARCHAR(36)) AS \"COLUMN_GUID\", "
            "CAST(NULL AS INTEGER) AS \"COLUMN_PROPID\", "
            "CAST(NULL AS SMALLINT) AS \"COLLATION\", "
            "CAST(NULL AS BIGINT) AS \"CARDINALITY\", "
            "CAST(NULL AS INTEGER) AS \"PAGES\", "
            "CAST(NULL AS VARCHAR(255)) AS \"FILTER_CONDITION\", "
            "CAST(NULL AS SMALLINT) AS \"INTEGRATED\" "
            "FROM sys.idxs i "
            "JOIN sys.tables t ON t.id = i.table_id "
            "JOIN sys.schemas s ON s.id = t.schema_id "
            "LEFT JOIN sys.objects o ON o.id = i.id "
            "WHERE 1 = 1", cfg->database, cfg->database);
        where_started = TRUE;
        if (c_restrictions > 1) Schema_AppendWhereEquals(sql, cch_sql, &where_started, "s.name", &restrictions[1]);
        if (c_restrictions > 2) Schema_AppendWhereEquals(sql, cch_sql, &where_started, "t.name", &restrictions[2]);
    }
    else if (IsEqualGUID(schema, &DBSCHEMA_PROVIDER_TYPES))
    {
        _snprintf(sql, cch_sql,
            "SELECT "
            "t.sqlname AS TYPE_NAME, "
            "CASE "
                "WHEN lower(t.sqlname) = 'tinyint' THEN 17 "
                "WHEN lower(t.sqlname) = 'smallint' THEN 2 "
                "WHEN lower(t.sqlname) IN ('int', 'integer') THEN 3 "
                "WHEN lower(t.sqlname) = 'bigint' THEN 20 "
                "WHEN lower(t.sqlname) = 'real' THEN 4 "
                "WHEN lower(t.sqlname) IN ('double', 'float') THEN 5 "
                "WHEN lower(t.sqlname) IN ('decimal', 'numeric') THEN 131 "
                "WHEN lower(t.sqlname) IN ('char', 'varchar', 'clob') OR lower(t.sqlname) LIKE '%%interval%%' THEN 129 "
                "WHEN lower(t.sqlname) = 'date' THEN 133 "
                "WHEN lower(t.sqlname) = 'time' THEN 134 "
                "WHEN lower(t.sqlname) = 'timestamp' THEN 135 "
                "WHEN lower(t.sqlname) = 'blob' THEN 128 "
                "WHEN lower(t.sqlname) IN ('boolean', 'bool') THEN 11 "
                "ELSE 129 "
            "END AS DATA_TYPE, "
            "CASE "
                "WHEN lower(t.sqlname) IN ('char', 'varchar', 'clob', 'blob') THEN t.digits "
                "WHEN lower(t.sqlname) = 'tinyint' THEN 3 "
                "WHEN lower(t.sqlname) = 'smallint' THEN 5 "
                "WHEN lower(t.sqlname) IN ('int', 'integer') THEN 10 "
                "WHEN lower(t.sqlname) = 'bigint' THEN 19 "
                "WHEN lower(t.sqlname) = 'real' THEN 7 "
                "WHEN lower(t.sqlname) IN ('double', 'float') THEN 15 "
                "WHEN lower(t.sqlname) IN ('decimal', 'numeric') THEN t.digits "
                "WHEN lower(t.sqlname) = 'date' THEN 10 "
                "WHEN lower(t.sqlname) = 'time' THEN 8 "
                "WHEN lower(t.sqlname) = 'timestamp' THEN 26 "
                "ELSE CAST(NULL AS INTEGER) "
            "END AS COLUMN_SIZE, "
            "CASE "
                "WHEN lower(t.sqlname) IN ('char', 'varchar', 'clob', 'date', 'time', 'timestamp') OR lower(t.sqlname) LIKE '%%interval%%' THEN '''' "
                "ELSE CAST(NULL AS VARCHAR(8)) "
            "END AS LITERAL_PREFIX, "
            "CASE "
                "WHEN lower(t.sqlname) IN ('char', 'varchar', 'clob', 'date', 'time', 'timestamp') OR lower(t.sqlname) LIKE '%%interval%%' THEN '''' "
                "ELSE CAST(NULL AS VARCHAR(8)) "
            "END AS LITERAL_SUFFIX, "
            "CASE "
                "WHEN lower(t.sqlname) IN ('char', 'varchar') THEN 'length' "
                "WHEN lower(t.sqlname) IN ('decimal', 'numeric') THEN 'precision,scale' "
                "ELSE CAST(NULL AS VARCHAR(32)) "
            "END AS CREATE_PARAMS, "
            "CAST(1 AS SMALLINT) AS IS_NULLABLE, "
            "CASE "
                "WHEN lower(t.sqlname) IN ('char', 'varchar', 'clob') OR lower(t.sqlname) LIKE '%%interval%%' THEN CAST(1 AS SMALLINT) "
                "ELSE CAST(0 AS SMALLINT) "
            "END AS CASE_SENSITIVE, "
            "CASE "
                "WHEN lower(t.sqlname) = 'blob' THEN 1 "
                "ELSE 4 "
            "END AS SEARCHABLE, "
            "CASE "
                "WHEN lower(t.sqlname) = 'tinyint' THEN CAST(1 AS SMALLINT) "
                "WHEN lower(t.sqlname) IN ('smallint', 'int', 'integer', 'bigint', 'real', 'double', 'float', 'decimal', 'numeric') THEN CAST(0 AS SMALLINT) "
                "ELSE CAST(NULL AS SMALLINT) "
            "END AS UNSIGNED_ATTRIBUTE, "
            "CASE "
                "WHEN lower(t.sqlname) IN ('decimal', 'numeric') THEN CAST(1 AS SMALLINT) "
                "ELSE CAST(0 AS SMALLINT) "
            "END AS FIXED_PREC_SCALE, "
            "CAST(0 AS SMALLINT) AS AUTO_UNIQUE_VALUE, "
            "t.sqlname AS LOCAL_TYPE_NAME, "
            "CASE "
                "WHEN lower(t.sqlname) IN ('decimal', 'numeric') THEN CAST(0 AS SMALLINT) "
                "ELSE CAST(NULL AS SMALLINT) "
            "END AS MINIMUM_SCALE, "
            "CASE "
                "WHEN lower(t.sqlname) IN ('decimal', 'numeric') THEN t.scale "
                "ELSE CAST(NULL AS SMALLINT) "
            "END AS MAXIMUM_SCALE, "
            "CAST(NULL AS VARCHAR(36)) AS GUID, "
            "CAST(NULL AS VARCHAR(255)) AS TYPELIB, "
            "CAST(NULL AS VARCHAR(32)) AS VERSION, "
            "CASE "
                "WHEN lower(t.sqlname) IN ('clob', 'blob') THEN CAST(1 AS SMALLINT) "
                "ELSE CAST(0 AS SMALLINT) "
            "END AS IS_LONG, "
            "CASE "
                "WHEN lower(t.sqlname) = 'tinyint' THEN CAST(1 AS SMALLINT) "
                "WHEN lower(t.sqlname) = 'smallint' THEN CAST(1 AS SMALLINT) "
                "WHEN lower(t.sqlname) = 'int' THEN CAST(1 AS SMALLINT) "
                "WHEN lower(t.sqlname) = 'bigint' THEN CAST(1 AS SMALLINT) "
                "WHEN lower(t.sqlname) = 'real' THEN CAST(1 AS SMALLINT) "
                "WHEN lower(t.sqlname) = 'double' THEN CAST(1 AS SMALLINT) "
                "WHEN lower(t.sqlname) = 'decimal' THEN CAST(1 AS SMALLINT) "
                "WHEN lower(t.sqlname) = 'varchar' THEN CAST(1 AS SMALLINT) "
                "WHEN lower(t.sqlname) = 'date' THEN CAST(1 AS SMALLINT) "
                "WHEN lower(t.sqlname) = 'time' THEN CAST(1 AS SMALLINT) "
                "WHEN lower(t.sqlname) = 'timestamp' THEN CAST(1 AS SMALLINT) "
                "WHEN lower(t.sqlname) = 'blob' THEN CAST(1 AS SMALLINT) "
                "WHEN lower(t.sqlname) = 'boolean' THEN CAST(1 AS SMALLINT) "
                "ELSE CAST(0 AS SMALLINT) "
            "END AS BEST_MATCH, "
            "CASE "
                "WHEN lower(t.sqlname) IN ('varchar', 'clob', 'blob') OR lower(t.sqlname) LIKE '%%interval%%' THEN CAST(0 AS SMALLINT) "
                "ELSE CAST(1 AS SMALLINT) "
            "END AS IS_FIXEDLENGTH "
            "FROM sys.types t");
    }
    else if (IsEqualGUID(schema, &DBSCHEMA_TABLE_STATISTICS))
    {
        _snprintf(sql, cch_sql,
            "SELECT '%s' AS TABLE_CATALOG, s.name AS TABLE_SCHEMA, t.name AS TABLE_NAME, "
            "t.count AS CARDINALITY, t.width AS PAGE_COUNT "
            "FROM sys.tables t JOIN sys.schemas s ON s.id = t.schema_id WHERE 1 = 1", cfg->database);
        where_started = TRUE;
        if (c_restrictions > 1) Schema_AppendWhereEquals(sql, cch_sql, &where_started, "s.name", &restrictions[1]);
        if (c_restrictions > 2) Schema_AppendWhereEquals(sql, cch_sql, &where_started, "t.name", &restrictions[2]);
    }
    else
    {
        return DB_E_NOTSUPPORTED;
    }

    sql[cch_sql - 1] = '\0';
    return S_OK;
}

HRESULT Schema_CreateRowset(MonetSession* session, REFGUID schema, ULONG c_restrictions, const VARIANT restrictions[], REFIID riid, void** ppv)
{
    CHAR sql[4096];
    SQLHSTMT hstmt = SQL_NULL_HSTMT;
    HRESULT hr;

    if (!session || !ppv)
    {
        return E_POINTER;
    }

    hr = Schema_BuildSql(schema, &session->datasource->config, c_restrictions, restrictions, sql, MONET_ARRAY_SIZE(sql));
    if (FAILED(hr))
    {
        return hr;
    }

    Log_WriteA(MONET_LOG_DEBUG, "Schema::GetRowset", "schema query: %s", sql);
    hr = Odbc_ExecDirectA(session->datasource->hdbc, sql, &hstmt);
    if (FAILED(hr))
    {
        return hr;
    }

    return Rowset_Create(session, NULL, hstmt, schema, riid, ppv);
}
