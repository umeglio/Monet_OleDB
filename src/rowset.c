#include "monetdb_oledb.h"
#include <ctype.h>

static MonetRowset* Rowset_FromRowset(IRowset* iface)
{
    return CONTAINING_RECORD(iface, MonetRowset, IRowset_iface);
}

static MonetRowset* Rowset_FromRowsetChange(IRowsetChange* iface)
{
    return CONTAINING_RECORD(iface, MonetRowset, IRowsetChange_iface);
}

static MonetRowset* Rowset_FromAccessor(IAccessor* iface)
{
    return CONTAINING_RECORD(iface, MonetRowset, IAccessor_iface);
}

static MonetRowset* Rowset_FromColumns(IColumnsInfo* iface)
{
    return CONTAINING_RECORD(iface, MonetRowset, IColumnsInfo_iface);
}

static MonetRowset* Rowset_FromInfo(IRowsetInfo* iface)
{
    return CONTAINING_RECORD(iface, MonetRowset, IRowsetInfo_iface);
}

static MonetRowset* Rowset_FromConvert(IConvertType* iface)
{
    return CONTAINING_RECORD(iface, MonetRowset, IConvertType_iface);
}

static MonetRowset* Rowset_FromSupportErrorInfo(ISupportErrorInfo* iface)
{
    return CONTAINING_RECORD(iface, MonetRowset, ISupportErrorInfo_iface);
}

static void* Rowset_BindingPointer(void* base, DBBYTEOFFSET offset)
{
    return ((BYTE*)base) + offset;
}

static void Rowset_SetStatus(const DBBINDING* binding, void* base, DBSTATUS status)
{
    if (binding->dwPart & DBPART_STATUS)
    {
        *((DBSTATUS*)Rowset_BindingPointer(base, binding->obStatus)) = status;
    }
}

static void Rowset_SetLength(const DBBINDING* binding, void* base, DBLENGTH length)
{
    if (binding->dwPart & DBPART_LENGTH)
    {
        *((DBLENGTH*)Rowset_BindingPointer(base, binding->obLength)) = length;
    }
}

static DBSTATUS Rowset_ReadInputStatus(const DBBINDING* binding, const void* base)
{
    if (!binding || !base)
    {
        return DBSTATUS_E_BADACCESSOR;
    }
    if (binding->dwPart & DBPART_STATUS)
    {
        return *((const DBSTATUS*)Rowset_BindingPointer((void*)base, binding->obStatus));
    }
    return DBSTATUS_S_OK;
}

static DBLENGTH Rowset_ReadInputLength(const DBBINDING* binding, const void* base)
{
    if (!binding || !base)
    {
        return 0;
    }
    if (binding->dwPart & DBPART_LENGTH)
    {
        return *((const DBLENGTH*)Rowset_BindingPointer((void*)base, binding->obLength));
    }
    return 0;
}

static const void* Rowset_ReadInputValuePointer(const DBBINDING* binding, const void* base)
{
    if (!binding || !base || !(binding->dwPart & DBPART_VALUE))
    {
        return NULL;
    }
    return Rowset_BindingPointer((void*)base, binding->obValue);
}

static const CHAR* Rowset_SkipWhitespaceA(const CHAR* text)
{
    while (text && *text && isspace((unsigned char)*text))
    {
        ++text;
    }
    return text;
}

static BOOL Rowset_ParseIdentifierA(const CHAR** ptext, CHAR* buffer, size_t cch_buffer)
{
    const CHAR* text = NULL;
    size_t used = 0;

    if (!ptext || !*ptext || !buffer || cch_buffer == 0)
    {
        return FALSE;
    }

    text = Rowset_SkipWhitespaceA(*ptext);
    buffer[0] = '\0';
    if (!*text)
    {
        return FALSE;
    }

    if (*text == '"')
    {
        ++text;
        while (*text && *text != '"')
        {
            if (used + 1 >= cch_buffer)
            {
                return FALSE;
            }
            buffer[used++] = *text++;
        }
        if (*text != '"')
        {
            return FALSE;
        }
        ++text;
    }
    else
    {
        while (*text && (isalnum((unsigned char)*text) || *text == '_' || *text == '$'))
        {
            if (used + 1 >= cch_buffer)
            {
                return FALSE;
            }
            buffer[used++] = *text++;
        }
        if (used == 0)
        {
            return FALSE;
        }
    }

    buffer[used] = '\0';
    *ptext = text;
    return TRUE;
}

static BOOL Rowset_IsIdentifierBoundaryA(CHAR ch)
{
    return ch == '\0' || isspace((unsigned char)ch) || ch == '(' || ch == ')' ||
        ch == ',' || ch == ';';
}

static const CHAR* Rowset_FindSelectFromA(const CHAR* text)
{
    const CHAR* start = text;
    int paren_depth = 0;
    CHAR quote = '\0';

    while (text && *text)
    {
        if (quote)
        {
            if (*text == quote)
            {
                quote = '\0';
            }
            ++text;
            continue;
        }

        if (*text == '\'' || *text == '"')
        {
            quote = *text++;
            continue;
        }

        if (*text == '(')
        {
            ++paren_depth;
            ++text;
            continue;
        }
        if (*text == ')' && paren_depth > 0)
        {
            --paren_depth;
            ++text;
            continue;
        }

        if (paren_depth == 0 &&
            _strnicmp(text, "from", 4) == 0 &&
            (text == start || Rowset_IsIdentifierBoundaryA(text[-1])) &&
            Rowset_IsIdentifierBoundaryA(text[4]))
        {
            return text;
        }
        ++text;
    }

    return NULL;
}

static BOOL Rowset_SelectListLooksInsertableA(const CHAR* select_list, const CHAR* from_clause)
{
    const CHAR* text = Rowset_SkipWhitespaceA(select_list);

    if (!select_list || !from_clause || from_clause <= select_list)
    {
        return FALSE;
    }

    if (*text == '*')
    {
        text = Rowset_SkipWhitespaceA(text + 1);
        return text == from_clause;
    }

    for (;;)
    {
        CHAR name[MONETDB_MAX_NAME];

        text = Rowset_SkipWhitespaceA(text);
        if (text >= from_clause)
        {
            return FALSE;
        }
        if (!Rowset_ParseIdentifierA(&text, name, MONET_ARRAY_SIZE(name)))
        {
            return FALSE;
        }

        text = Rowset_SkipWhitespaceA(text);
        if (text == from_clause)
        {
            return TRUE;
        }
        if (*text != ',')
        {
            return FALSE;
        }
        ++text;
    }
}

static BOOL Rowset_ParseSimpleSelectTargetSql(const CHAR* sql, CHAR* schema_name, size_t cch_schema, CHAR* table_name, size_t cch_table)
{
    const CHAR* text = sql;
    const CHAR* from_clause = NULL;
    CHAR first[MONETDB_MAX_NAME];
    CHAR second[MONETDB_MAX_NAME];
    BOOL have_second = FALSE;

    if (!sql || !schema_name || !table_name || cch_schema == 0 || cch_table == 0)
    {
        return FALSE;
    }

    schema_name[0] = '\0';
    table_name[0] = '\0';
    first[0] = '\0';
    second[0] = '\0';

    text = Rowset_SkipWhitespaceA(text);
    if (_strnicmp(text, "select", 6) != 0)
    {
        return FALSE;
    }

    from_clause = Rowset_FindSelectFromA(text + 6);
    if (!from_clause)
    {
        return FALSE;
    }
    if (!Rowset_SelectListLooksInsertableA(text + 6, from_clause))
    {
        return FALSE;
    }
    text = Rowset_SkipWhitespaceA(from_clause + 4);
    if (!Rowset_ParseIdentifierA(&text, first, MONET_ARRAY_SIZE(first)))
    {
        return FALSE;
    }
    text = Rowset_SkipWhitespaceA(text);
    if (*text == '.')
    {
        ++text;
        if (!Rowset_ParseIdentifierA(&text, second, MONET_ARRAY_SIZE(second)))
        {
            return FALSE;
        }
        have_second = TRUE;
    }
    text = Rowset_SkipWhitespaceA(text);
    if (*text == ',')
    {
        return FALSE;
    }
    if (_strnicmp(text, "join", 4) == 0 && Rowset_IsIdentifierBoundaryA(text[4]))
    {
        return FALSE;
    }
    if (*text == ';')
    {
        text = Rowset_SkipWhitespaceA(text + 1);
    }
    if (*text != '\0' &&
        !(_strnicmp(text, "where", 5) == 0 && Rowset_IsIdentifierBoundaryA(text[5])))
    {
        return FALSE;
    }

    if (have_second)
    {
        Monet_StringCopyA(schema_name, cch_schema, first);
        Monet_StringCopyA(table_name, cch_table, second);
    }
    else
    {
        Monet_StringCopyA(table_name, cch_table, first);
    }
    return table_name[0] != '\0';
}

static BOOL Rowset_IsInsertable(const MonetRowset* self)
{
    return self && (self->updatability & DBPROPVAL_UP_INSERT) != 0 && self->base_table[0] != '\0';
}

static HRESULT Rowset_AppendSqlText(CHAR* sql, size_t cch_sql, size_t* pused, const CHAR* text)
{
    size_t used = 0;
    size_t length = 0;

    if (!sql || !pused || !text || cch_sql == 0)
    {
        return E_POINTER;
    }

    used = *pused;
    length = strlen(text);
    if (used + length >= cch_sql)
    {
        return E_OUTOFMEMORY;
    }

    memcpy(sql + used, text, length);
    used += length;
    sql[used] = '\0';
    *pused = used;
    return S_OK;
}

static HRESULT Rowset_AppendSqlFormat(CHAR* sql, size_t cch_sql, size_t* pused, const CHAR* format, ...)
{
    va_list args;
    int written = 0;
    size_t used = 0;

    if (!sql || !pused || !format || cch_sql == 0)
    {
        return E_POINTER;
    }

    used = *pused;
    if (used >= cch_sql)
    {
        return E_OUTOFMEMORY;
    }

    va_start(args, format);
    written = _vsnprintf(sql + used, cch_sql - used, format, args);
    va_end(args);
    if (written < 0 || used + (size_t)written >= cch_sql)
    {
        sql[cch_sql - 1] = '\0';
        return E_OUTOFMEMORY;
    }

    *pused = used + (size_t)written;
    return S_OK;
}

static HRESULT Rowset_AppendQuotedIdentifier(CHAR* sql, size_t cch_sql, size_t* pused, const CHAR* identifier)
{
    HRESULT hr;
    const CHAR* text = identifier ? identifier : "";

    hr = Rowset_AppendSqlText(sql, cch_sql, pused, "\"");
    if (FAILED(hr))
    {
        return hr;
    }
    while (*text)
    {
        CHAR chunk[3];
        if (*text == '"')
        {
            chunk[0] = '"';
            chunk[1] = '"';
            chunk[2] = '\0';
        }
        else
        {
            chunk[0] = *text;
            chunk[1] = '\0';
        }
        hr = Rowset_AppendSqlText(sql, cch_sql, pused, chunk);
        if (FAILED(hr))
        {
            return hr;
        }
        ++text;
    }
    return Rowset_AppendSqlText(sql, cch_sql, pused, "\"");
}

static HRESULT Rowset_NormalizeAnsiUtf8Alloc(const CHAR* text, size_t length, CHAR** pp_normalized, size_t* pnormalized_len, BOOL* pconverted);

static HRESULT Rowset_AppendUtf8LiteralRaw(CHAR* sql, size_t cch_sql, size_t* pused, const CHAR* text, size_t length)
{
    HRESULT hr;
    size_t i;

    hr = Rowset_AppendSqlText(sql, cch_sql, pused, "'");
    if (FAILED(hr))
    {
        return hr;
    }
    for (i = 0; i < length; ++i)
    {
        CHAR chunk[2];
        BYTE ch = (BYTE)text[i];

        if (ch < 0x20U || ch == 0x7FU)
        {
            continue;
        }
        if (ch == '\'')
        {
            hr = Rowset_AppendSqlText(sql, cch_sql, pused, "''");
        }
        else if (ch == '\\')
        {
            hr = Rowset_AppendSqlText(sql, cch_sql, pused, "\\\\");
        }
        else
        {
            chunk[0] = (CHAR)ch;
            chunk[1] = '\0';
            hr = Rowset_AppendSqlText(sql, cch_sql, pused, chunk);
        }
        if (FAILED(hr))
        {
            return hr;
        }
    }
    return Rowset_AppendSqlText(sql, cch_sql, pused, "'");
}

static HRESULT Rowset_AppendAnsiLiteral(CHAR* sql, size_t cch_sql, size_t* pused, const CHAR* text, size_t length)
{
    CHAR* normalized = NULL;
    size_t normalized_len = 0;
    BOOL converted = FALSE;
    HRESULT hr;

    if (!text)
    {
        return Rowset_AppendSqlText(sql, cch_sql, pused, "NULL");
    }

    hr = Rowset_NormalizeAnsiUtf8Alloc(text, length, &normalized, &normalized_len, &converted);
    if (FAILED(hr))
    {
        return hr;
    }
    if (hr == S_OK)
    {
        MONET_TRACE(
            "Rowset::StringSanitize",
            "normalized ANSI literal converted=%d original_bytes=%llu normalized_bytes=%llu",
            converted ? 1 : 0,
            (unsigned long long)length,
            (unsigned long long)normalized_len);
        hr = Rowset_AppendUtf8LiteralRaw(sql, cch_sql, pused, normalized, normalized_len);
        CoTaskMemFree(normalized);
        return hr;
    }

    return Rowset_AppendUtf8LiteralRaw(sql, cch_sql, pused, text, length);
}

static BOOL Rowset_IsAllowedStringControl(DWORD ch)
{
    MONET_UNUSED(ch);
    return FALSE;
}

static BOOL Rowset_IsUtf8Continuation(BYTE ch)
{
    return (ch & 0xC0U) == 0x80U;
}

static BOOL Rowset_ReadValidUtf8Sequence(const CHAR* text, size_t length, size_t index, size_t* psequence_len)
{
    BYTE b0;

    if (!text || !psequence_len || index >= length)
    {
        return FALSE;
    }

    b0 = (BYTE)text[index];
    *psequence_len = 0;
    if (b0 < 0x80U)
    {
        if (b0 < 0x20U && !Rowset_IsAllowedStringControl((DWORD)b0))
        {
            return FALSE;
        }
        *psequence_len = 1;
        return TRUE;
    }
    if (b0 >= 0xC2U && b0 <= 0xDFU)
    {
        if (index + 1U < length && Rowset_IsUtf8Continuation((BYTE)text[index + 1U]))
        {
            *psequence_len = 2;
            return TRUE;
        }
        return FALSE;
    }
    if (b0 == 0xE0U)
    {
        if (index + 2U < length &&
            (BYTE)text[index + 1U] >= 0xA0U && (BYTE)text[index + 1U] <= 0xBFU &&
            Rowset_IsUtf8Continuation((BYTE)text[index + 2U]))
        {
            *psequence_len = 3;
            return TRUE;
        }
        return FALSE;
    }
    if ((b0 >= 0xE1U && b0 <= 0xECU) || (b0 >= 0xEEU && b0 <= 0xEFU))
    {
        if (index + 2U < length &&
            Rowset_IsUtf8Continuation((BYTE)text[index + 1U]) &&
            Rowset_IsUtf8Continuation((BYTE)text[index + 2U]))
        {
            *psequence_len = 3;
            return TRUE;
        }
        return FALSE;
    }
    if (b0 == 0xEDU)
    {
        if (index + 2U < length &&
            (BYTE)text[index + 1U] >= 0x80U && (BYTE)text[index + 1U] <= 0x9FU &&
            Rowset_IsUtf8Continuation((BYTE)text[index + 2U]))
        {
            *psequence_len = 3;
            return TRUE;
        }
        return FALSE;
    }
    if (b0 == 0xF0U)
    {
        if (index + 3U < length &&
            (BYTE)text[index + 1U] >= 0x90U && (BYTE)text[index + 1U] <= 0xBFU &&
            Rowset_IsUtf8Continuation((BYTE)text[index + 2U]) &&
            Rowset_IsUtf8Continuation((BYTE)text[index + 3U]))
        {
            *psequence_len = 4;
            return TRUE;
        }
        return FALSE;
    }
    if (b0 >= 0xF1U && b0 <= 0xF3U)
    {
        if (index + 3U < length &&
            Rowset_IsUtf8Continuation((BYTE)text[index + 1U]) &&
            Rowset_IsUtf8Continuation((BYTE)text[index + 2U]) &&
            Rowset_IsUtf8Continuation((BYTE)text[index + 3U]))
        {
            *psequence_len = 4;
            return TRUE;
        }
        return FALSE;
    }
    if (b0 == 0xF4U)
    {
        if (index + 3U < length &&
            (BYTE)text[index + 1U] >= 0x80U && (BYTE)text[index + 1U] <= 0x8FU &&
            Rowset_IsUtf8Continuation((BYTE)text[index + 2U]) &&
            Rowset_IsUtf8Continuation((BYTE)text[index + 3U]))
        {
            *psequence_len = 4;
            return TRUE;
        }
        return FALSE;
    }

    return FALSE;
}

static HRESULT Rowset_SanitizeUtf8Alloc(const CHAR* text, size_t length, CHAR** pp_sanitized, size_t* psanitized_len, size_t* premoved)
{
    size_t i;
    size_t removed = 0;
    size_t out_len = 0;
    CHAR* output = NULL;

    if (!pp_sanitized || !psanitized_len || !premoved)
    {
        return E_POINTER;
    }
    *pp_sanitized = NULL;
    *psanitized_len = length;
    *premoved = 0;
    if (!text || length == 0)
    {
        return S_FALSE;
    }

    for (i = 0; i < length;)
    {
        size_t seq_len = 0;
        if (Rowset_ReadValidUtf8Sequence(text, length, i, &seq_len))
        {
            out_len += seq_len;
            i += seq_len;
        }
        else
        {
            ++removed;
            ++i;
        }
    }

    if (removed == 0)
    {
        return S_FALSE;
    }

    output = (CHAR*)CoTaskMemAlloc(out_len + 1U);
    if (!output)
    {
        return E_OUTOFMEMORY;
    }

    out_len = 0;
    for (i = 0; i < length;)
    {
        size_t seq_len = 0;
        if (Rowset_ReadValidUtf8Sequence(text, length, i, &seq_len))
        {
            memcpy(output + out_len, text + i, seq_len);
            out_len += seq_len;
            i += seq_len;
        }
        else
        {
            ++i;
        }
    }
    output[out_len] = '\0';

    *pp_sanitized = output;
    *psanitized_len = out_len;
    *premoved = removed;
    return S_OK;
}

static HRESULT Rowset_SanitizeWideAlloc(const WCHAR* text, size_t length_chars, WCHAR** pp_sanitized, size_t* psanitized_chars, size_t* premoved)
{
    size_t i;
    size_t removed = 0;
    size_t out_chars = 0;
    WCHAR* output = NULL;

    if (!pp_sanitized || !psanitized_chars || !premoved)
    {
        return E_POINTER;
    }
    *pp_sanitized = NULL;
    *psanitized_chars = length_chars;
    *premoved = 0;
    if (!text || length_chars == 0)
    {
        return S_FALSE;
    }

    for (i = 0; i < length_chars;)
    {
        WCHAR ch = text[i];
        if (ch < 0x20 && !Rowset_IsAllowedStringControl((DWORD)ch))
        {
            ++removed;
            ++i;
        }
        else if (ch >= 0xD800 && ch <= 0xDBFF)
        {
            if (i + 1U < length_chars && text[i + 1U] >= 0xDC00 && text[i + 1U] <= 0xDFFF)
            {
                out_chars += 2U;
                i += 2U;
            }
            else
            {
                ++removed;
                ++i;
            }
        }
        else if (ch >= 0xDC00 && ch <= 0xDFFF)
        {
            ++removed;
            ++i;
        }
        else
        {
            ++out_chars;
            ++i;
        }
    }

    if (removed == 0)
    {
        return S_FALSE;
    }

    output = (WCHAR*)CoTaskMemAlloc((out_chars + 1U) * sizeof(WCHAR));
    if (!output)
    {
        return E_OUTOFMEMORY;
    }

    out_chars = 0;
    for (i = 0; i < length_chars;)
    {
        WCHAR ch = text[i];
        if (ch < 0x20 && !Rowset_IsAllowedStringControl((DWORD)ch))
        {
            ++i;
        }
        else if (ch >= 0xD800 && ch <= 0xDBFF)
        {
            if (i + 1U < length_chars && text[i + 1U] >= 0xDC00 && text[i + 1U] <= 0xDFFF)
            {
                output[out_chars++] = text[i++];
                output[out_chars++] = text[i++];
            }
            else
            {
                ++i;
            }
        }
        else if (ch >= 0xDC00 && ch <= 0xDFFF)
        {
            ++i;
        }
        else
        {
            output[out_chars++] = text[i++];
        }
    }
    output[out_chars] = L'\0';

    *pp_sanitized = output;
    *psanitized_chars = out_chars;
    *premoved = removed;
    return S_OK;
}

static HRESULT Rowset_WideToUtf8Alloc(const WCHAR* text, size_t length_chars, CHAR** pp_utf8, size_t* putf8_len)
{
    int bytes;
    CHAR* utf8 = NULL;

    if (!pp_utf8 || !putf8_len)
    {
        return E_POINTER;
    }
    *pp_utf8 = NULL;
    *putf8_len = 0;
    if (!text || length_chars == 0)
    {
        utf8 = (CHAR*)CoTaskMemAlloc(1U);
        if (!utf8)
        {
            return E_OUTOFMEMORY;
        }
        utf8[0] = '\0';
        *pp_utf8 = utf8;
        return S_OK;
    }
    if (length_chars > (size_t)INT_MAX)
    {
        return DB_E_CANTCONVERTVALUE;
    }

    bytes = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text, (int)length_chars, NULL, 0, NULL, NULL);
    if (bytes <= 0)
    {
        return DB_E_CANTCONVERTVALUE;
    }

    utf8 = (CHAR*)CoTaskMemAlloc((size_t)bytes + 1U);
    if (!utf8)
    {
        return E_OUTOFMEMORY;
    }
    if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text, (int)length_chars, utf8, bytes, NULL, NULL) <= 0)
    {
        CoTaskMemFree(utf8);
        return DB_E_CANTCONVERTVALUE;
    }
    utf8[bytes] = '\0';
    *pp_utf8 = utf8;
    *putf8_len = (size_t)bytes;
    return S_OK;
}

static BOOL Rowset_IsValidUtf8Buffer(const CHAR* text, size_t length)
{
    if (!text || length == 0)
    {
        return TRUE;
    }
    if (length > (size_t)INT_MAX)
    {
        return FALSE;
    }
    return MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text, (int)length, NULL, 0) > 0;
}

static HRESULT Rowset_ConvertAcpToUtf8Alloc(const CHAR* text, size_t length, CHAR** pp_utf8, size_t* putf8_len)
{
    WCHAR* wide = NULL;
    WCHAR* sanitized = NULL;
    const WCHAR* source = NULL;
    size_t source_chars = 0;
    size_t sanitized_chars = 0;
    size_t removed = 0;
    int wide_chars;
    HRESULT hr;

    if (!pp_utf8 || !putf8_len)
    {
        return E_POINTER;
    }
    *pp_utf8 = NULL;
    *putf8_len = 0;
    if (!text || length == 0)
    {
        return Rowset_WideToUtf8Alloc(L"", 0, pp_utf8, putf8_len);
    }
    if (length > (size_t)INT_MAX)
    {
        return DB_E_CANTCONVERTVALUE;
    }

    wide_chars = MultiByteToWideChar(CP_ACP, MB_ERR_INVALID_CHARS, text, (int)length, NULL, 0);
    if (wide_chars <= 0)
    {
        wide_chars = MultiByteToWideChar(CP_ACP, 0, text, (int)length, NULL, 0);
    }
    if (wide_chars <= 0)
    {
        return DB_E_CANTCONVERTVALUE;
    }

    wide = (WCHAR*)CoTaskMemAlloc(((size_t)wide_chars + 1U) * sizeof(WCHAR));
    if (!wide)
    {
        return E_OUTOFMEMORY;
    }
    if (MultiByteToWideChar(CP_ACP, 0, text, (int)length, wide, wide_chars) <= 0)
    {
        CoTaskMemFree(wide);
        return DB_E_CANTCONVERTVALUE;
    }
    wide[wide_chars] = L'\0';

    source = wide;
    source_chars = (size_t)wide_chars;
    hr = Rowset_SanitizeWideAlloc(wide, (size_t)wide_chars, &sanitized, &sanitized_chars, &removed);
    if (FAILED(hr))
    {
        CoTaskMemFree(wide);
        return hr;
    }
    if (hr == S_OK)
    {
        source = sanitized;
        source_chars = sanitized_chars;
    }

    hr = Rowset_WideToUtf8Alloc(source, source_chars, pp_utf8, putf8_len);
    if (sanitized)
    {
        CoTaskMemFree(sanitized);
    }
    CoTaskMemFree(wide);
    return hr;
}

static HRESULT Rowset_NormalizeAnsiUtf8Alloc(const CHAR* text, size_t length, CHAR** pp_normalized, size_t* pnormalized_len, BOOL* pconverted)
{
    CHAR* sanitized = NULL;
    size_t sanitized_len = 0;
    size_t removed = 0;
    HRESULT hr;

    if (!pp_normalized || !pnormalized_len || !pconverted)
    {
        return E_POINTER;
    }
    *pp_normalized = NULL;
    *pnormalized_len = length;
    *pconverted = FALSE;
    if (!text || length == 0)
    {
        return S_FALSE;
    }

    if (!Rowset_IsValidUtf8Buffer(text, length))
    {
        *pconverted = TRUE;
        return Rowset_ConvertAcpToUtf8Alloc(text, length, pp_normalized, pnormalized_len);
    }

    hr = Rowset_SanitizeUtf8Alloc(text, length, &sanitized, &sanitized_len, &removed);
    if (FAILED(hr))
    {
        return hr;
    }
    if (hr == S_OK)
    {
        *pp_normalized = sanitized;
        *pnormalized_len = sanitized_len;
        return S_OK;
    }

    return S_FALSE;
}

static HRESULT Rowset_AppendWideLiteral(CHAR* sql, size_t cch_sql, size_t* pused, const WCHAR* text, size_t length_chars)
{
    CHAR* ansi = NULL;
    WCHAR* sanitized = NULL;
    size_t sanitized_chars = length_chars;
    size_t removed = 0;
    const WCHAR* source = text;
    int bytes = 0;
    HRESULT hr;

    if (!text)
    {
        return Rowset_AppendSqlText(sql, cch_sql, pused, "NULL");
    }

    hr = Rowset_SanitizeWideAlloc(text, length_chars, &sanitized, &sanitized_chars, &removed);
    if (FAILED(hr))
    {
        return hr;
    }
    if (hr == S_OK)
    {
        source = sanitized;
        length_chars = sanitized_chars;
    }

    bytes = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, source, (int)length_chars, NULL, 0, NULL, NULL);
    if (bytes <= 0 && length_chars > 0)
    {
        if (sanitized)
        {
            CoTaskMemFree(sanitized);
        }
        return DB_E_CANTCONVERTVALUE;
    }

    ansi = (CHAR*)CoTaskMemAlloc((size_t)bytes + 1U);
    if (!ansi)
    {
        if (sanitized)
        {
            CoTaskMemFree(sanitized);
        }
        return E_OUTOFMEMORY;
    }

    if (bytes > 0)
    {
        if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, source, (int)length_chars, ansi, bytes, NULL, NULL) <= 0)
        {
            CoTaskMemFree(ansi);
            if (sanitized)
            {
                CoTaskMemFree(sanitized);
            }
            return DB_E_CANTCONVERTVALUE;
        }
    }
    ansi[bytes] = '\0';
    hr = Rowset_AppendAnsiLiteral(sql, cch_sql, pused, ansi, (size_t)bytes);
    CoTaskMemFree(ansi);
    if (sanitized)
    {
        CoTaskMemFree(sanitized);
    }
    return hr;
}

static HRESULT Rowset_AppendBstrLiteral(CHAR* sql, size_t cch_sql, size_t* pused, BSTR text)
{
    if (!text)
    {
        return Rowset_AppendSqlText(sql, cch_sql, pused, "NULL");
    }
    return Rowset_AppendWideLiteral(sql, cch_sql, pused, text, (size_t)SysStringLen(text));
}

static HRESULT Rowset_AppendNumericLiteral(CHAR* sql, size_t cch_sql, size_t* pused, const DB_NUMERIC* value)
{
    unsigned char digits[128];
    size_t digit_count = 1;
    size_t i;
    HRESULT hr;

    if (!value)
    {
        return E_POINTER;
    }

    ZeroMemory(digits, sizeof(digits));
    digits[0] = 0;
    for (i = 16; i-- > 0;)
    {
        unsigned int carry = value->val[i];
        size_t j;

        for (j = 0; j < digit_count; ++j)
        {
            unsigned int temp = (unsigned int)(digits[j] * 256U) + carry;
            digits[j] = (unsigned char)(temp % 10U);
            carry = temp / 10U;
        }
        while (carry > 0)
        {
            if (digit_count >= MONET_ARRAY_SIZE(digits))
            {
                return E_OUTOFMEMORY;
            }
            digits[digit_count++] = (unsigned char)(carry % 10U);
            carry /= 10U;
        }
    }

    if (!value->sign)
    {
        hr = Rowset_AppendSqlText(sql, cch_sql, pused, "-");
        if (FAILED(hr))
        {
            return hr;
        }
    }

    if (value->scale >= digit_count)
    {
        hr = Rowset_AppendSqlText(sql, cch_sql, pused, "0.");
        if (FAILED(hr))
        {
            return hr;
        }
        for (i = 0; i < value->scale - digit_count; ++i)
        {
            hr = Rowset_AppendSqlText(sql, cch_sql, pused, "0");
            if (FAILED(hr))
            {
                return hr;
            }
        }
        for (i = digit_count; i-- > 0;)
        {
            CHAR chunk[2] = { (CHAR)('0' + digits[i]), '\0' };
            hr = Rowset_AppendSqlText(sql, cch_sql, pused, chunk);
            if (FAILED(hr))
            {
                return hr;
            }
        }
        return S_OK;
    }

    for (i = digit_count; i-- > 0;)
    {
        CHAR chunk[2] = { (CHAR)('0' + digits[i]), '\0' };
        if (value->scale > 0 && i + 1 == value->scale)
        {
            hr = Rowset_AppendSqlText(sql, cch_sql, pused, ".");
            if (FAILED(hr))
            {
                return hr;
            }
        }
        hr = Rowset_AppendSqlText(sql, cch_sql, pused, chunk);
        if (FAILED(hr))
        {
            return hr;
        }
    }

    return S_OK;
}

static HRESULT Rowset_AppendVariantLiteralSql(const VARIANT* value, CHAR* sql, size_t cch_sql, size_t* pused)
{
    VARTYPE vt;

    if (!value)
    {
        return Rowset_AppendSqlText(sql, cch_sql, pused, "NULL");
    }

    vt = V_VT(value);
    if (vt == VT_EMPTY || vt == VT_NULL)
    {
        return Rowset_AppendSqlText(sql, cch_sql, pused, "NULL");
    }

    switch (vt)
    {
    case VT_I1:
        return Rowset_AppendSqlFormat(sql, cch_sql, pused, "%d", (int)V_I1(value));

    case VT_UI1:
        return Rowset_AppendSqlFormat(sql, cch_sql, pused, "%u", (unsigned int)V_UI1(value));

    case VT_I2:
        return Rowset_AppendSqlFormat(sql, cch_sql, pused, "%d", (int)V_I2(value));

    case VT_UI2:
        return Rowset_AppendSqlFormat(sql, cch_sql, pused, "%u", (unsigned int)V_UI2(value));

    case VT_I4:
    case VT_INT:
        return Rowset_AppendSqlFormat(sql, cch_sql, pused, "%ld", (long)V_I4(value));

    case VT_UI4:
    case VT_UINT:
        return Rowset_AppendSqlFormat(sql, cch_sql, pused, "%lu", (unsigned long)V_UI4(value));

    case VT_I8:
        return Rowset_AppendSqlFormat(sql, cch_sql, pused, "%lld", (long long)V_I8(value));

    case VT_UI8:
        return Rowset_AppendSqlFormat(sql, cch_sql, pused, "%llu", (unsigned long long)V_UI8(value));

    case VT_R4:
        return Rowset_AppendSqlFormat(sql, cch_sql, pused, "%.9g", (double)V_R4(value));

    case VT_R8:
        return Rowset_AppendSqlFormat(sql, cch_sql, pused, "%.17g", (double)V_R8(value));

    case VT_BOOL:
        return Rowset_AppendSqlText(sql, cch_sql, pused, (V_BOOL(value) == VARIANT_FALSE) ? "FALSE" : "TRUE");

    case VT_BSTR:
        return Rowset_AppendBstrLiteral(sql, cch_sql, pused, V_BSTR(value));

    case VT_DATE:
        return Rowset_AppendSqlFormat(sql, cch_sql, pused, "%.15g", V_DATE(value));

    default:
        return DB_E_NOTSUPPORTED;
    }
}

static BOOL Rowset_BindingParticipatesInInsert(const DBBINDING* binding, const void* base, DBORDINAL column_count)
{
    DBSTATUS status;

    if (!binding || !base)
    {
        return FALSE;
    }
    if (binding->iOrdinal == 0 || binding->iOrdinal > column_count || !(binding->dwPart & DBPART_VALUE))
    {
        return FALSE;
    }

    status = Rowset_ReadInputStatus(binding, base);
    return status != DBSTATUS_S_IGNORE;
}

static HRESULT Rowset_AppendBindingValueSql(const DBBINDING* binding, const MonetColumnInfo* col, const void* base, CHAR* sql, size_t cch_sql, size_t* pused)
{
    DBSTATUS status;
    DBLENGTH length;
    const void* value_ptr;
    DBTYPE base_type;
    BOOL byref;

    if (!binding || !base || !sql || !pused)
    {
        return E_POINTER;
    }

    status = Rowset_ReadInputStatus(binding, base);
    if (status == DBSTATUS_S_ISNULL)
    {
        return Rowset_AppendSqlText(sql, cch_sql, pused, "NULL");
    }
    if (status == DBSTATUS_S_DEFAULT)
    {
        return Rowset_AppendSqlText(sql, cch_sql, pused, "DEFAULT");
    }
    if (!(status == DBSTATUS_S_OK || status == DBSTATUS_S_TRUNCATED))
    {
        return DB_E_ERRORSOCCURRED;
    }

    value_ptr = Rowset_ReadInputValuePointer(binding, base);
    if (!value_ptr)
    {
        return DB_E_ERRORSOCCURRED;
    }

    byref = (binding->wType & DBTYPE_BYREF) != 0;
    base_type = (DBTYPE)(binding->wType & ~(DBTYPE_BYREF));
    length = Rowset_ReadInputLength(binding, base);
    if (byref)
    {
        value_ptr = *((const void* const*)value_ptr);
        if (!value_ptr)
        {
            return Rowset_AppendSqlText(sql, cch_sql, pused, "NULL");
        }
    }

    switch (base_type)
    {
    case DBTYPE_I1:
        return Rowset_AppendSqlFormat(sql, cch_sql, pused, "%d", (int)(*((const signed char*)value_ptr)));

    case DBTYPE_STR:
    {
        CHAR* normalized = NULL;
        size_t normalized_len = 0;
        BOOL converted = FALSE;
        HRESULT hr;

        if (length <= 0)
        {
            length = (DBLENGTH)strlen((const CHAR*)value_ptr);
        }
        hr = Rowset_NormalizeAnsiUtf8Alloc((const CHAR*)value_ptr, (size_t)length, &normalized, &normalized_len, &converted);
        if (FAILED(hr))
        {
            return hr;
        }
        if (hr == S_OK)
        {
            MONET_TRACE(
                "Rowset::InsertRow",
                "normalized string column='%s' converted=%d original_bytes=%llu normalized_bytes=%llu",
                col ? col->name_a : "",
                converted ? 1 : 0,
                (unsigned long long)length,
                (unsigned long long)normalized_len);
            hr = Rowset_AppendUtf8LiteralRaw(sql, cch_sql, pused, normalized, normalized_len);
            CoTaskMemFree(normalized);
            return hr;
        }
        return Rowset_AppendAnsiLiteral(sql, cch_sql, pused, (const CHAR*)value_ptr, (size_t)length);
    }

    case DBTYPE_WSTR:
    {
        WCHAR* sanitized = NULL;
        size_t sanitized_chars = 0;
        size_t removed = 0;
        HRESULT hr;

        if (length <= 0)
        {
            length = (DBLENGTH)(wcslen((const WCHAR*)value_ptr) * sizeof(WCHAR));
        }
        hr = Rowset_SanitizeWideAlloc((const WCHAR*)value_ptr, (size_t)(length / sizeof(WCHAR)), &sanitized, &sanitized_chars, &removed);
        if (FAILED(hr))
        {
            return hr;
        }
        if (hr == S_OK)
        {
            MONET_TRACE(
                "Rowset::InsertRow",
                "sanitized wide string column='%s' removed=%llu",
                col ? col->name_a : "",
                (unsigned long long)removed);
            hr = Rowset_AppendWideLiteral(sql, cch_sql, pused, sanitized, sanitized_chars);
            CoTaskMemFree(sanitized);
            return hr;
        }
        return Rowset_AppendWideLiteral(sql, cch_sql, pused, (const WCHAR*)value_ptr, (size_t)(length / sizeof(WCHAR)));
    }

    case DBTYPE_BSTR:
    {
        BSTR bstr = byref ? (BSTR)value_ptr : *((const BSTR*)value_ptr);
        WCHAR* sanitized = NULL;
        size_t sanitized_chars = 0;
        size_t removed = 0;
        HRESULT hr;

        if (!bstr)
        {
            return Rowset_AppendSqlText(sql, cch_sql, pused, "NULL");
        }
        hr = Rowset_SanitizeWideAlloc(bstr, (size_t)SysStringLen(bstr), &sanitized, &sanitized_chars, &removed);
        if (FAILED(hr))
        {
            return hr;
        }
        if (hr == S_OK)
        {
            MONET_TRACE(
                "Rowset::InsertRow",
                "sanitized bstr column='%s' removed=%llu",
                col ? col->name_a : "",
                (unsigned long long)removed);
            hr = Rowset_AppendWideLiteral(sql, cch_sql, pused, sanitized, sanitized_chars);
            CoTaskMemFree(sanitized);
            return hr;
        }
        return Rowset_AppendBstrLiteral(sql, cch_sql, pused, bstr);
    }

    case DBTYPE_BOOL:
        return Rowset_AppendSqlText(sql, cch_sql, pused, (*((const VARIANT_BOOL*)value_ptr) == VARIANT_FALSE) ? "FALSE" : "TRUE");

    case DBTYPE_UI1:
        return Rowset_AppendSqlFormat(sql, cch_sql, pused, "%u", (unsigned int)(*((const BYTE*)value_ptr)));

    case DBTYPE_I2:
        return Rowset_AppendSqlFormat(sql, cch_sql, pused, "%d", (int)(*((const SHORT*)value_ptr)));

    case DBTYPE_UI2:
        return Rowset_AppendSqlFormat(sql, cch_sql, pused, "%u", (unsigned int)(*((const USHORT*)value_ptr)));

    case DBTYPE_I4:
        return Rowset_AppendSqlFormat(sql, cch_sql, pused, "%ld", (long)(*((const LONG*)value_ptr)));

    case DBTYPE_UI4:
        return Rowset_AppendSqlFormat(sql, cch_sql, pused, "%lu", (unsigned long)(*((const ULONG*)value_ptr)));

    case DBTYPE_I8:
        return Rowset_AppendSqlFormat(sql, cch_sql, pused, "%lld", (long long)(*((const LONGLONG*)value_ptr)));

    case DBTYPE_UI8:
        return Rowset_AppendSqlFormat(sql, cch_sql, pused, "%llu", (unsigned long long)(*((const ULONGLONG*)value_ptr)));

    case DBTYPE_R4:
        return Rowset_AppendSqlFormat(sql, cch_sql, pused, "%.9g", (double)(*((const FLOAT*)value_ptr)));

    case DBTYPE_R8:
        return Rowset_AppendSqlFormat(sql, cch_sql, pused, "%.17g", (double)(*((const DOUBLE*)value_ptr)));

    case DBTYPE_NUMERIC:
    case DBTYPE_DECIMAL:
        return Rowset_AppendNumericLiteral(sql, cch_sql, pused, (const DB_NUMERIC*)value_ptr);

    case DBTYPE_DBDATE:
    {
        const DBDATE* date_value = (const DBDATE*)value_ptr;
        return Rowset_AppendSqlFormat(sql, cch_sql, pused, "DATE '%04d-%02u-%02u'", (int)date_value->year, (unsigned)date_value->month, (unsigned)date_value->day);
    }

    case DBTYPE_DBTIME:
    {
        const DBTIME* time_value = (const DBTIME*)value_ptr;
        return Rowset_AppendSqlFormat(sql, cch_sql, pused, "TIME '%02u:%02u:%02u'", (unsigned)time_value->hour, (unsigned)time_value->minute, (unsigned)time_value->second);
    }

    case DBTYPE_DBTIMESTAMP:
    {
        const DBTIMESTAMP* ts = (const DBTIMESTAMP*)value_ptr;
        return Rowset_AppendSqlFormat(
            sql,
            cch_sql,
            pused,
            "TIMESTAMP '%04d-%02u-%02u %02u:%02u:%02u.%06lu'",
            (int)ts->year,
            (unsigned)ts->month,
            (unsigned)ts->day,
            (unsigned)ts->hour,
            (unsigned)ts->minute,
            (unsigned)ts->second,
            (unsigned long)(ts->fraction / 1000UL));
    }

    case DBTYPE_DATE:
        return Rowset_AppendSqlFormat(sql, cch_sql, pused, "%.15g", *((const DATE*)value_ptr));

    case DBTYPE_VARIANT:
        return Rowset_AppendVariantLiteralSql(byref ? (const VARIANT*)value_ptr : (const VARIANT*)value_ptr, sql, cch_sql, pused);

    default:
        return DB_E_NOTSUPPORTED;
    }
}

static HRESULT Rowset_EnsureDynamicCapacity(CHAR** pbuffer, size_t* pcapacity, size_t needed)
{
    CHAR* grown;
    size_t capacity;

    if (!pbuffer || !pcapacity)
    {
        return E_POINTER;
    }
    if (needed <= *pcapacity)
    {
        return S_OK;
    }

    capacity = *pcapacity ? *pcapacity : 8192U;
    while (capacity < needed)
    {
        if (capacity > ((size_t)-1) / 2U)
        {
            return E_OUTOFMEMORY;
        }
        capacity *= 2U;
    }

    grown = (CHAR*)CoTaskMemRealloc(*pbuffer, capacity);
    if (!grown)
    {
        return E_OUTOFMEMORY;
    }
    *pbuffer = grown;
    *pcapacity = capacity;
    return S_OK;
}

static HRESULT Rowset_AppendDynamicBytes(CHAR** pbuffer, size_t* pcapacity, size_t* pused, const CHAR* text, size_t length)
{
    HRESULT hr;

    if (!pbuffer || !pcapacity || !pused || (!text && length > 0))
    {
        return E_POINTER;
    }

    hr = Rowset_EnsureDynamicCapacity(pbuffer, pcapacity, *pused + length + 1U);
    if (FAILED(hr))
    {
        return hr;
    }
    if (length > 0)
    {
        memcpy(*pbuffer + *pused, text, length);
        *pused += length;
    }
    (*pbuffer)[*pused] = '\0';
    return S_OK;
}

static HRESULT Rowset_AppendDynamicText(CHAR** pbuffer, size_t* pcapacity, size_t* pused, const CHAR* text)
{
    return Rowset_AppendDynamicBytes(pbuffer, pcapacity, pused, text ? text : "", text ? strlen(text) : 0U);
}

static void Rowset_FreeInsertShape(DBORDINAL* ordinals)
{
    if (ordinals)
    {
        CoTaskMemFree(ordinals);
    }
}

static BOOL Rowset_InsertShapeEquals(const DBORDINAL* left, DBCOUNTITEM left_count, const DBORDINAL* right, DBCOUNTITEM right_count)
{
    DBCOUNTITEM i;

    if (left_count != right_count || !left || !right)
    {
        return FALSE;
    }
    for (i = 0; i < left_count; ++i)
    {
        if (left[i] != right[i])
        {
            return FALSE;
        }
    }
    return TRUE;
}

static HRESULT Rowset_BuildInsertShape(MonetRowset* self, MonetAccessor* accessor, const void* p_data, DBORDINAL** pp_ordinals, DBCOUNTITEM* pc_ordinals)
{
    DBORDINAL* ordinals;
    DBCOUNTITEM i;
    DBCOUNTITEM count = 0;

    if (!self || !accessor || !p_data || !pp_ordinals || !pc_ordinals)
    {
        return E_POINTER;
    }

    *pp_ordinals = NULL;
    *pc_ordinals = 0;
    if (accessor->binding_count == 0)
    {
        return DB_E_ERRORSOCCURRED;
    }

    ordinals = (DBORDINAL*)CoTaskMemAlloc(sizeof(DBORDINAL) * (size_t)accessor->binding_count);
    if (!ordinals)
    {
        return E_OUTOFMEMORY;
    }

    for (i = 0; i < accessor->binding_count; ++i)
    {
        const DBBINDING* binding = &accessor->bindings[i];
        if (Rowset_BindingParticipatesInInsert(binding, p_data, self->column_count))
        {
            ordinals[count++] = binding->iOrdinal;
        }
    }

    if (count == 0)
    {
        CoTaskMemFree(ordinals);
        return DB_E_ERRORSOCCURRED;
    }

    *pp_ordinals = ordinals;
    *pc_ordinals = count;
    return S_OK;
}

static HRESULT Rowset_BuildInsertPrefixSql(MonetRowset* self, const CHAR* schema_name, const DBORDINAL* ordinals, DBCOUNTITEM ordinal_count, CHAR* sql, size_t cch_sql, size_t* pused)
{
    DBCOUNTITEM i;
    HRESULT hr;

    if (!self || !ordinals || !sql || !pused)
    {
        return E_POINTER;
    }

    *pused = 0;
    sql[0] = '\0';
    hr = Rowset_AppendSqlText(sql, cch_sql, pused, "INSERT INTO ");
    if (FAILED(hr))
    {
        return hr;
    }
    if (schema_name && schema_name[0])
    {
        hr = Rowset_AppendQuotedIdentifier(sql, cch_sql, pused, schema_name);
        if (FAILED(hr))
        {
            return hr;
        }
        hr = Rowset_AppendSqlText(sql, cch_sql, pused, ".");
        if (FAILED(hr))
        {
            return hr;
        }
    }
    hr = Rowset_AppendQuotedIdentifier(sql, cch_sql, pused, self->base_table);
    if (FAILED(hr))
    {
        return hr;
    }
    hr = Rowset_AppendSqlText(sql, cch_sql, pused, " (");
    if (FAILED(hr))
    {
        return hr;
    }

    for (i = 0; i < ordinal_count; ++i)
    {
        if (i > 0)
        {
            hr = Rowset_AppendSqlText(sql, cch_sql, pused, ", ");
            if (FAILED(hr))
            {
                return hr;
            }
        }
        hr = Rowset_AppendQuotedIdentifier(sql, cch_sql, pused, self->columns[ordinals[i] - 1].name_a);
        if (FAILED(hr))
        {
            return hr;
        }
    }

    return Rowset_AppendSqlText(sql, cch_sql, pused, ") VALUES ");
}

static HRESULT Rowset_TryBuildInsertValuesSql(MonetRowset* self, MonetAccessor* accessor, const void* p_data, CHAR* sql, size_t cch_sql, size_t* pused)
{
    DBCOUNTITEM i;
    BOOL first = TRUE;
    HRESULT hr;

    if (!self || !accessor || !p_data || !sql || !pused)
    {
        return E_POINTER;
    }

    *pused = 0;
    sql[0] = '\0';
    hr = Rowset_AppendSqlText(sql, cch_sql, pused, "(");
    if (FAILED(hr))
    {
        return hr;
    }

    for (i = 0; i < accessor->binding_count; ++i)
    {
        const DBBINDING* binding = &accessor->bindings[i];

        if (!Rowset_BindingParticipatesInInsert(binding, p_data, self->column_count))
        {
            continue;
        }
        if (!first)
        {
            hr = Rowset_AppendSqlText(sql, cch_sql, pused, ", ");
            if (FAILED(hr))
            {
                return hr;
            }
        }
        hr = Rowset_AppendBindingValueSql(binding, &self->columns[binding->iOrdinal - 1], p_data, sql, cch_sql, pused);
        if (FAILED(hr))
        {
            return hr;
        }
        first = FALSE;
    }

    return Rowset_AppendSqlText(sql, cch_sql, pused, ")");
}

static HRESULT Rowset_BuildInsertValuesSql(MonetRowset* self, MonetAccessor* accessor, const void* p_data, CHAR** pp_values, size_t* pvalue_len)
{
    CHAR* values = NULL;
    size_t capacity = 8192U;
    size_t used = 0;
    HRESULT hr;

    if (!pp_values || !pvalue_len)
    {
        return E_POINTER;
    }
    *pp_values = NULL;
    *pvalue_len = 0;

    for (;;)
    {
        values = (CHAR*)CoTaskMemAlloc(capacity);
        if (!values)
        {
            return E_OUTOFMEMORY;
        }

        used = 0;
        hr = Rowset_TryBuildInsertValuesSql(self, accessor, p_data, values, capacity, &used);
        if (hr != E_OUTOFMEMORY)
        {
            break;
        }

        CoTaskMemFree(values);
        values = NULL;
        if (capacity > ((size_t)-1) / 2U)
        {
            return E_OUTOFMEMORY;
        }
        capacity *= 2U;
    }

    if (FAILED(hr))
    {
        CoTaskMemFree(values);
        return hr;
    }

    *pp_values = values;
    *pvalue_len = used;
    return S_OK;
}

static void Rowset_ResetInsertBatch(MonetRowset* self)
{
    if (!self)
    {
        return;
    }

    if (self->insert_batch_sql)
    {
        CoTaskMemFree(self->insert_batch_sql);
        self->insert_batch_sql = NULL;
    }
    if (self->insert_batch_ordinals)
    {
        CoTaskMemFree(self->insert_batch_ordinals);
        self->insert_batch_ordinals = NULL;
    }
    self->insert_batch_capacity = 0;
    self->insert_batch_used = 0;
    self->insert_batch_accessor = 0;
    self->insert_batch_column_count = 0;
    self->insert_batch_row_count = 0;
}

static HRESULT Rowset_FlushInsertBatch(MonetRowset* self)
{
    SQLHSTMT hstmt = SQL_NULL_HSTMT;
    DBCOUNTITEM batch_rows;
    size_t batch_bytes;
    SQLLEN odbc_rows = -1;
    SQLRETURN rowcount_rc = SQL_ERROR;
    HRESULT hr;

    if (!self || !self->insert_batch_sql || self->insert_batch_row_count == 0)
    {
        Rowset_ResetInsertBatch(self);
        return S_OK;
    }

    batch_rows = self->insert_batch_row_count;
    batch_bytes = self->insert_batch_used;

    MONET_TRACE(
        "Rowset::InsertBatch",
        "flush table='%s' rows=%llu bytes=%llu total_buffered=%llu total_flushed=%llu",
        self->base_table,
        (unsigned long long)batch_rows,
        (unsigned long long)batch_bytes,
        (unsigned long long)self->insert_batch_total_buffered,
        (unsigned long long)self->insert_batch_total_flushed);
    hr = Odbc_ExecDirectA(self->session->datasource->hdbc, self->insert_batch_sql, &hstmt);
    if (hstmt != SQL_NULL_HSTMT)
    {
        rowcount_rc = SQLRowCount(hstmt, &odbc_rows);
    }
    Odbc_CloseStatement(&hstmt);
    if (SUCCEEDED(hr))
    {
        self->insert_batch_total_flushed += (ULONGLONG)batch_rows;
        ++self->insert_batch_flush_count;
        Log_WriteA(
            MONET_LOG_INFO,
            "Rowset::InsertBatch",
            "flush OK table='%s' rows=%llu total_inserted=%llu flushes=%llu odbc_rowcount=%lld rowcount_rc=%d",
            self->base_table,
            (unsigned long long)batch_rows,
            (unsigned long long)self->insert_batch_total_flushed,
            (unsigned long long)self->insert_batch_flush_count,
            (long long)odbc_rows,
            (int)rowcount_rc);
    }
    else
    {
        Log_WriteA(
            MONET_LOG_ERROR,
            "Rowset::InsertBatch",
            "flush FALLITO table='%s' rows=%llu total_buffered=%llu total_inserted=%llu hr=0x%08lx odbc_rowcount=%lld rowcount_rc=%d",
            self->base_table,
            (unsigned long long)batch_rows,
            (unsigned long long)self->insert_batch_total_buffered,
            (unsigned long long)self->insert_batch_total_flushed,
            (unsigned long)hr,
            (long long)odbc_rows,
            (int)rowcount_rc);
    }
    Rowset_ResetInsertBatch(self);
    return hr;
}

static size_t Rowset_InsertBatchMaxBytes(const MonetRowset* self)
{
    LONG kb = MONETDB_DEFAULT_FETCH_WINDOW_KB;

    if (self && self->session && self->session->datasource)
    {
        kb = self->session->datasource->config.fetch_window_kb;
    }
    if (kb < MONETDB_MIN_FETCH_WINDOW_KB)
    {
        kb = MONETDB_MIN_FETCH_WINDOW_KB;
    }
    if (kb > MONETDB_MAX_FETCH_WINDOW_KB)
    {
        kb = MONETDB_MAX_FETCH_WINDOW_KB;
    }
    return (size_t)kb * 1024U;
}

static DBCOUNTITEM Rowset_InsertBatchTargetRows(const MonetRowset* self)
{
    LONG rows = MONETDB_DEFAULT_FETCH_ROWS;

    if (self && self->session && self->session->datasource)
    {
        rows = self->session->datasource->config.fetch_rows;
    }
    if (rows < MONETDB_MIN_FETCH_ROWS)
    {
        rows = MONETDB_MIN_FETCH_ROWS;
    }
    if (rows > MONETDB_MAX_FETCH_ROWS)
    {
        rows = MONETDB_MAX_FETCH_ROWS;
    }
    return (DBCOUNTITEM)rows;
}

static HRESULT Rowset_StartInsertBatch(MonetRowset* self, HACCESSOR h_accessor, const CHAR* schema_name, DBORDINAL* ordinals, DBCOUNTITEM ordinal_count)
{
    CHAR prefix[MONETDB_MAX_SQL_TEXT];
    size_t prefix_used = 0;
    HRESULT hr;

    if (!self || !ordinals)
    {
        return E_POINTER;
    }

    hr = Rowset_BuildInsertPrefixSql(self, schema_name, ordinals, ordinal_count, prefix, MONET_ARRAY_SIZE(prefix), &prefix_used);
    if (FAILED(hr))
    {
        return hr;
    }

    Rowset_ResetInsertBatch(self);
    hr = Rowset_AppendDynamicBytes(&self->insert_batch_sql, &self->insert_batch_capacity, &self->insert_batch_used, prefix, prefix_used);
    if (FAILED(hr))
    {
        return hr;
    }

    self->insert_batch_ordinals = ordinals;
    self->insert_batch_column_count = ordinal_count;
    self->insert_batch_accessor = h_accessor;
    return S_OK;
}

static HRESULT Rowset_BufferInsertRow(MonetRowset* self, HACCESSOR h_accessor, MonetAccessor* accessor, const void* p_data, const CHAR* schema_name)
{
    DBORDINAL* ordinals = NULL;
    DBCOUNTITEM ordinal_count = 0;
    CHAR* values_sql = NULL;
    size_t values_len = 0;
    size_t max_bytes;
    DBCOUNTITEM target_rows;
    HRESULT hr;

    hr = Rowset_BuildInsertShape(self, accessor, p_data, &ordinals, &ordinal_count);
    if (FAILED(hr))
    {
        return hr;
    }

    if (self->insert_batch_row_count > 0 &&
        (self->insert_batch_accessor != h_accessor ||
            !Rowset_InsertShapeEquals(self->insert_batch_ordinals, self->insert_batch_column_count, ordinals, ordinal_count)))
    {
        hr = Rowset_FlushInsertBatch(self);
        if (FAILED(hr))
        {
            Rowset_FreeInsertShape(ordinals);
            return hr;
        }
    }

    hr = Rowset_BuildInsertValuesSql(self, accessor, p_data, &values_sql, &values_len);
    if (FAILED(hr))
    {
        Rowset_FreeInsertShape(ordinals);
        return hr;
    }

    max_bytes = Rowset_InsertBatchMaxBytes(self);
    if (self->insert_batch_row_count > 0 &&
        self->insert_batch_used + values_len + 2U >= max_bytes)
    {
        hr = Rowset_FlushInsertBatch(self);
        if (FAILED(hr))
        {
            CoTaskMemFree(values_sql);
            Rowset_FreeInsertShape(ordinals);
            return hr;
        }
    }

    if (self->insert_batch_row_count == 0)
    {
        hr = Rowset_StartInsertBatch(self, h_accessor, schema_name, ordinals, ordinal_count);
        if (FAILED(hr))
        {
            CoTaskMemFree(values_sql);
            Rowset_FreeInsertShape(ordinals);
            return hr;
        }
        ordinals = NULL;
    }
    else
    {
        Rowset_FreeInsertShape(ordinals);
        ordinals = NULL;
        hr = Rowset_AppendDynamicText(&self->insert_batch_sql, &self->insert_batch_capacity, &self->insert_batch_used, ", ");
        if (FAILED(hr))
        {
            CoTaskMemFree(values_sql);
            return hr;
        }
    }

    hr = Rowset_AppendDynamicBytes(&self->insert_batch_sql, &self->insert_batch_capacity, &self->insert_batch_used, values_sql, values_len);
    CoTaskMemFree(values_sql);
    if (FAILED(hr))
    {
        return hr;
    }

    ++self->insert_batch_row_count;
    ++self->insert_batch_total_buffered;
    target_rows = Rowset_InsertBatchTargetRows(self);
    MONET_TRACE(
        "Rowset::InsertBatch",
        "buffered table='%s' rows=%llu target=%llu bytes=%llu total_buffered=%llu total_flushed=%llu",
        self->base_table,
        (unsigned long long)self->insert_batch_row_count,
        (unsigned long long)target_rows,
        (unsigned long long)self->insert_batch_used,
        (unsigned long long)self->insert_batch_total_buffered,
        (unsigned long long)self->insert_batch_total_flushed);

    if (self->insert_batch_row_count >= target_rows)
    {
        return Rowset_FlushInsertBatch(self);
    }

    return S_OK;
}

static void Rowset_ClosePreparedInsert(MonetRowset* self)
{
    if (!self)
    {
        return;
    }

    Odbc_CloseStatement(&self->insert_hstmt);
    if (self->insert_ordinals)
    {
        CoTaskMemFree(self->insert_ordinals);
        self->insert_ordinals = NULL;
    }
    self->insert_accessor = 0;
    self->insert_param_count = 0;
}

static BOOL Rowset_PreparedInsertShapeMatches(const MonetRowset* self, HACCESSOR h_accessor, const DBORDINAL* ordinals, DBCOUNTITEM param_count)
{
    DBCOUNTITEM i;

    if (!self || self->insert_hstmt == SQL_NULL_HSTMT || self->insert_accessor != h_accessor ||
        self->insert_param_count != param_count || !self->insert_ordinals || !ordinals)
    {
        return FALSE;
    }

    for (i = 0; i < param_count; ++i)
    {
        if (self->insert_ordinals[i] != ordinals[i])
        {
            return FALSE;
        }
    }

    return TRUE;
}

static HRESULT Rowset_BuildPreparedInsertSql(
    MonetRowset* self,
    MonetAccessor* accessor,
    const void* p_data,
    const CHAR* schema_name,
    CHAR* sql,
    size_t cch_sql,
    DBORDINAL** pp_ordinals,
    DBCOUNTITEM* pc_params)
{
    size_t used = 0;
    DBCOUNTITEM i;
    DBCOUNTITEM param_count = 0;
    BOOL first = TRUE;
    DBORDINAL* ordinals = NULL;
    HRESULT hr;

    if (!self || !accessor || !p_data || !sql || !pp_ordinals || !pc_params)
    {
        return E_POINTER;
    }

    *pp_ordinals = NULL;
    *pc_params = 0;
    sql[0] = '\0';

    if (accessor->binding_count > 0xFFFFu)
    {
        return S_FALSE;
    }

    ordinals = (DBORDINAL*)CoTaskMemAlloc(sizeof(DBORDINAL) * (size_t)accessor->binding_count);
    if (!ordinals)
    {
        return E_OUTOFMEMORY;
    }

    hr = Rowset_AppendSqlText(sql, cch_sql, &used, "INSERT INTO ");
    if (FAILED(hr))
    {
        CoTaskMemFree(ordinals);
        return hr;
    }
    if (schema_name && schema_name[0])
    {
        hr = Rowset_AppendQuotedIdentifier(sql, cch_sql, &used, schema_name);
        if (FAILED(hr))
        {
            CoTaskMemFree(ordinals);
            return hr;
        }
        hr = Rowset_AppendSqlText(sql, cch_sql, &used, ".");
        if (FAILED(hr))
        {
            CoTaskMemFree(ordinals);
            return hr;
        }
    }
    hr = Rowset_AppendQuotedIdentifier(sql, cch_sql, &used, self->base_table);
    if (FAILED(hr))
    {
        CoTaskMemFree(ordinals);
        return hr;
    }
    hr = Rowset_AppendSqlText(sql, cch_sql, &used, " (");
    if (FAILED(hr))
    {
        CoTaskMemFree(ordinals);
        return hr;
    }

    for (i = 0; i < accessor->binding_count; ++i)
    {
        const DBBINDING* binding = &accessor->bindings[i];
        DBSTATUS status = Rowset_ReadInputStatus(binding, p_data);

        if (!Rowset_BindingParticipatesInInsert(binding, p_data, self->column_count))
        {
            continue;
        }
        if (status == DBSTATUS_S_DEFAULT)
        {
            CoTaskMemFree(ordinals);
            return S_FALSE;
        }

        if (!first)
        {
            hr = Rowset_AppendSqlText(sql, cch_sql, &used, ", ");
            if (FAILED(hr))
            {
                CoTaskMemFree(ordinals);
                return hr;
            }
        }
        hr = Rowset_AppendQuotedIdentifier(sql, cch_sql, &used, self->columns[binding->iOrdinal - 1].name_a);
        if (FAILED(hr))
        {
            CoTaskMemFree(ordinals);
            return hr;
        }
        ordinals[param_count++] = binding->iOrdinal;
        first = FALSE;
    }

    if (param_count == 0)
    {
        CoTaskMemFree(ordinals);
        return DB_E_ERRORSOCCURRED;
    }

    hr = Rowset_AppendSqlText(sql, cch_sql, &used, ") VALUES (");
    if (FAILED(hr))
    {
        CoTaskMemFree(ordinals);
        return hr;
    }
    for (i = 0; i < param_count; ++i)
    {
        if (i > 0)
        {
            hr = Rowset_AppendSqlText(sql, cch_sql, &used, ", ");
            if (FAILED(hr))
            {
                CoTaskMemFree(ordinals);
                return hr;
            }
        }
        hr = Rowset_AppendSqlText(sql, cch_sql, &used, "?");
        if (FAILED(hr))
        {
            CoTaskMemFree(ordinals);
            return hr;
        }
    }
    hr = Rowset_AppendSqlText(sql, cch_sql, &used, ")");
    if (FAILED(hr))
    {
        CoTaskMemFree(ordinals);
        return hr;
    }

    *pp_ordinals = ordinals;
    *pc_params = param_count;
    return S_OK;
}

static SQLULEN Rowset_InsertParamColumnSize(const MonetColumnInfo* col, const DBBINDING* binding)
{
    if (col && col->sql_size > 0)
    {
        return col->sql_size;
    }
    if (col && col->column_size > 0)
    {
        return (SQLULEN)col->column_size;
    }
    if (binding && binding->bPrecision > 0)
    {
        return (SQLULEN)binding->bPrecision;
    }
    return 0;
}

static SQLSMALLINT Rowset_InsertParamScale(const MonetColumnInfo* col, const DBBINDING* binding)
{
    if (col && col->sql_scale > 0)
    {
        return col->sql_scale;
    }
    if (binding)
    {
        return (SQLSMALLINT)binding->bScale;
    }
    return 0;
}

static HRESULT Rowset_GetPreparedInsertParameter(
    const DBBINDING* binding,
    const MonetColumnInfo* col,
    const void* base,
    SQLSMALLINT* pc_type,
    SQLSMALLINT* psql_type,
    SQLULEN* pcolumn_size,
    SQLSMALLINT* pscale,
    SQLPOINTER* pp_value,
    SQLLEN* pbuffer_len,
    SQLLEN* pindicator,
    SQLPOINTER* powned_buffer,
    CHAR* scratch,
    size_t cch_scratch)
{
    DBSTATUS status;
    DBLENGTH length;
    const void* value_ptr;
    DBTYPE base_type;
    BOOL byref;

    if (!binding || !col || !base || !pc_type || !psql_type || !pcolumn_size || !pscale || !pp_value || !pbuffer_len || !pindicator || !powned_buffer)
    {
        return E_POINTER;
    }

    status = Rowset_ReadInputStatus(binding, base);
    if (status == DBSTATUS_S_DEFAULT)
    {
        return S_FALSE;
    }

    *psql_type = col->sql_type;
    *pcolumn_size = Rowset_InsertParamColumnSize(col, binding);
    *pscale = Rowset_InsertParamScale(col, binding);
    *pp_value = NULL;
    *pbuffer_len = 0;
    *pindicator = 0;
    *powned_buffer = NULL;

    base_type = (DBTYPE)(binding->wType & ~(DBTYPE_BYREF));
    switch (base_type)
    {
    case DBTYPE_STR:
        *pc_type = SQL_C_CHAR;
        break;
    case DBTYPE_WSTR:
    case DBTYPE_BSTR:
        *pc_type = SQL_C_WCHAR;
        break;
    case DBTYPE_BOOL:
        *pc_type = SQL_C_BIT;
        break;
    case DBTYPE_I1:
        *pc_type = SQL_C_STINYINT;
        break;
    case DBTYPE_UI1:
        *pc_type = SQL_C_UTINYINT;
        break;
    case DBTYPE_I2:
        *pc_type = SQL_C_SSHORT;
        break;
    case DBTYPE_UI2:
        *pc_type = SQL_C_USHORT;
        break;
    case DBTYPE_I4:
        *pc_type = SQL_C_SLONG;
        break;
    case DBTYPE_UI4:
        *pc_type = SQL_C_ULONG;
        break;
    case DBTYPE_I8:
        *pc_type = SQL_C_SBIGINT;
        break;
    case DBTYPE_UI8:
        *pc_type = SQL_C_UBIGINT;
        break;
    case DBTYPE_R4:
        *pc_type = SQL_C_FLOAT;
        break;
    case DBTYPE_R8:
        *pc_type = SQL_C_DOUBLE;
        break;
    case DBTYPE_NUMERIC:
    case DBTYPE_DECIMAL:
        *pc_type = SQL_C_CHAR;
        break;
    case DBTYPE_DBDATE:
        *pc_type = SQL_C_TYPE_DATE;
        *psql_type = SQL_TYPE_DATE;
        break;
    case DBTYPE_DBTIME:
        *pc_type = SQL_C_TYPE_TIME;
        *psql_type = SQL_TYPE_TIME;
        break;
    case DBTYPE_DBTIMESTAMP:
        *pc_type = SQL_C_TYPE_TIMESTAMP;
        *psql_type = SQL_TYPE_TIMESTAMP;
        break;
    default:
        return S_FALSE;
    }

    if (status == DBSTATUS_S_ISNULL)
    {
        *pindicator = SQL_NULL_DATA;
        return S_OK;
    }
    if (!(status == DBSTATUS_S_OK || status == DBSTATUS_S_TRUNCATED))
    {
        return DB_E_ERRORSOCCURRED;
    }

    value_ptr = Rowset_ReadInputValuePointer(binding, base);
    if (!value_ptr)
    {
        return DB_E_ERRORSOCCURRED;
    }

    byref = (binding->wType & DBTYPE_BYREF) != 0;
    length = Rowset_ReadInputLength(binding, base);
    if (byref)
    {
        value_ptr = *((const void* const*)value_ptr);
        if (!value_ptr)
        {
            *pindicator = SQL_NULL_DATA;
            return S_OK;
        }
    }

    if (base_type == DBTYPE_STR)
    {
        CHAR* normalized = NULL;
        size_t normalized_len = 0;
        BOOL converted = FALSE;
        HRESULT hr;

        if (length <= 0)
        {
            length = (DBLENGTH)strlen((const CHAR*)value_ptr);
        }
        hr = Rowset_NormalizeAnsiUtf8Alloc((const CHAR*)value_ptr, (size_t)length, &normalized, &normalized_len, &converted);
        if (FAILED(hr))
        {
            return hr;
        }
        if (hr == S_OK)
        {
            MONET_TRACE(
                "Rowset::InsertRow",
                "normalized string column='%s' converted=%d original_bytes=%llu normalized_bytes=%llu",
                col->name_a,
                converted ? 1 : 0,
                (unsigned long long)length,
                (unsigned long long)normalized_len);
            *pp_value = (SQLPOINTER)normalized;
            *pbuffer_len = (SQLLEN)normalized_len;
            *pindicator = (SQLLEN)normalized_len;
            *powned_buffer = (SQLPOINTER)normalized;
        }
        else
        {
            *pp_value = (SQLPOINTER)value_ptr;
            *pbuffer_len = (SQLLEN)length;
            *pindicator = (SQLLEN)length;
        }
        return S_OK;
    }
    if (base_type == DBTYPE_WSTR)
    {
        WCHAR* sanitized = NULL;
        size_t sanitized_chars = 0;
        size_t removed = 0;
        HRESULT hr;

        if (length <= 0)
        {
            length = (DBLENGTH)(wcslen((const WCHAR*)value_ptr) * sizeof(WCHAR));
        }
        hr = Rowset_SanitizeWideAlloc((const WCHAR*)value_ptr, (size_t)(length / sizeof(WCHAR)), &sanitized, &sanitized_chars, &removed);
        if (FAILED(hr))
        {
            return hr;
        }
        if (hr == S_OK)
        {
            MONET_TRACE(
                "Rowset::InsertRow",
                "sanitized wide string column='%s' removed=%llu",
                col->name_a,
                (unsigned long long)removed);
            *pp_value = (SQLPOINTER)sanitized;
            *pbuffer_len = (SQLLEN)(sanitized_chars * sizeof(WCHAR));
            *pindicator = (SQLLEN)(sanitized_chars * sizeof(WCHAR));
            *powned_buffer = (SQLPOINTER)sanitized;
        }
        else
        {
            *pp_value = (SQLPOINTER)value_ptr;
            *pbuffer_len = (SQLLEN)length;
            *pindicator = (SQLLEN)length;
        }
        return S_OK;
    }
    if (base_type == DBTYPE_BSTR)
    {
        BSTR bstr = byref ? (BSTR)value_ptr : *((const BSTR*)value_ptr);
        WCHAR* sanitized = NULL;
        size_t sanitized_chars = 0;
        size_t removed = 0;
        HRESULT hr;

        if (!bstr)
        {
            *pindicator = SQL_NULL_DATA;
            return S_OK;
        }
        hr = Rowset_SanitizeWideAlloc(bstr, (size_t)SysStringLen(bstr), &sanitized, &sanitized_chars, &removed);
        if (FAILED(hr))
        {
            return hr;
        }
        if (hr == S_OK)
        {
            MONET_TRACE(
                "Rowset::InsertRow",
                "sanitized bstr column='%s' removed=%llu",
                col->name_a,
                (unsigned long long)removed);
            *pp_value = (SQLPOINTER)sanitized;
            *pbuffer_len = (SQLLEN)(sanitized_chars * sizeof(WCHAR));
            *pindicator = (SQLLEN)(sanitized_chars * sizeof(WCHAR));
            *powned_buffer = (SQLPOINTER)sanitized;
        }
        else
        {
            length = (DBLENGTH)(SysStringLen(bstr) * sizeof(WCHAR));
            *pp_value = (SQLPOINTER)bstr;
            *pbuffer_len = (SQLLEN)length;
            *pindicator = (SQLLEN)length;
        }
        return S_OK;
    }
    if (base_type == DBTYPE_NUMERIC || base_type == DBTYPE_DECIMAL)
    {
        size_t used = 0;
        HRESULT hr;

        if (!scratch || cch_scratch == 0)
        {
            return E_POINTER;
        }
        scratch[0] = '\0';
        hr = Rowset_AppendNumericLiteral(scratch, cch_scratch, &used, (const DB_NUMERIC*)value_ptr);
        if (FAILED(hr))
        {
            return hr;
        }
        *pp_value = (SQLPOINTER)scratch;
        *pbuffer_len = (SQLLEN)used;
        *pindicator = (SQLLEN)used;
        return S_OK;
    }

    *pp_value = (SQLPOINTER)value_ptr;
    return S_OK;
}

static HRESULT Rowset_EnsurePreparedInsert(
    MonetRowset* self,
    HACCESSOR h_accessor,
    MonetAccessor* accessor,
    const void* p_data,
    const CHAR* schema_name)
{
    CHAR sql[MONETDB_MAX_SQL_TEXT];
    DBORDINAL* ordinals = NULL;
    DBCOUNTITEM param_count = 0;
    SQLHSTMT hstmt = SQL_NULL_HSTMT;
    HRESULT hr;

    hr = Rowset_BuildPreparedInsertSql(self, accessor, p_data, schema_name, sql, MONET_ARRAY_SIZE(sql), &ordinals, &param_count);
    if (hr != S_OK)
    {
        return hr;
    }

    if (Rowset_PreparedInsertShapeMatches(self, h_accessor, ordinals, param_count))
    {
        CoTaskMemFree(ordinals);
        return S_OK;
    }

    Rowset_ClosePreparedInsert(self);
    hr = Odbc_PrepareA(self->session->datasource->hdbc, sql, &hstmt);
    if (FAILED(hr))
    {
        CoTaskMemFree(ordinals);
        return S_FALSE;
    }

    self->insert_hstmt = hstmt;
    self->insert_accessor = h_accessor;
    self->insert_param_count = param_count;
    self->insert_ordinals = ordinals;

    MONET_TRACE(
        "Rowset::InsertRow",
        "prepared fast insert table='%s.%s' params=%llu",
        schema_name ? schema_name : "",
        self->base_table,
        (unsigned long long)param_count);
    return S_OK;
}

static void Rowset_FreeInsertOwnedBuffers(SQLPOINTER* buffers, DBCOUNTITEM count)
{
    DBCOUNTITEM i;

    if (!buffers)
    {
        return;
    }
    for (i = 0; i < count; ++i)
    {
        if (buffers[i])
        {
            CoTaskMemFree(buffers[i]);
        }
    }
    CoTaskMemFree(buffers);
}

static HRESULT Rowset_ExecutePreparedInsert(MonetRowset* self, MonetAccessor* accessor, const void* p_data)
{
    SQLLEN* indicators = NULL;
    SQLPOINTER* owned_buffers = NULL;
    CHAR* scratch_buffers = NULL;
    DBCOUNTITEM i;
    DBCOUNTITEM param_index = 0;
    SQLRETURN rc;

    if (!self || !accessor || !p_data || self->insert_hstmt == SQL_NULL_HSTMT)
    {
        return E_POINTER;
    }

    indicators = (SQLLEN*)CoTaskMemAlloc(sizeof(SQLLEN) * (size_t)self->insert_param_count);
    if (!indicators)
    {
        return E_OUTOFMEMORY;
    }
    owned_buffers = (SQLPOINTER*)CoTaskMemAlloc(sizeof(SQLPOINTER) * (size_t)self->insert_param_count);
    if (!owned_buffers)
    {
        CoTaskMemFree(indicators);
        return E_OUTOFMEMORY;
    }
    ZeroMemory(owned_buffers, sizeof(SQLPOINTER) * (size_t)self->insert_param_count);
    scratch_buffers = (CHAR*)CoTaskMemAlloc((size_t)self->insert_param_count * 128U);
    if (!scratch_buffers)
    {
        Rowset_FreeInsertOwnedBuffers(owned_buffers, self->insert_param_count);
        CoTaskMemFree(indicators);
        return E_OUTOFMEMORY;
    }

    SQLFreeStmt(self->insert_hstmt, SQL_RESET_PARAMS);
    for (i = 0; i < accessor->binding_count; ++i)
    {
        const DBBINDING* binding = &accessor->bindings[i];
        const MonetColumnInfo* col;
        SQLSMALLINT c_type = 0;
        SQLSMALLINT sql_type = 0;
        SQLULEN column_size = 0;
        SQLSMALLINT scale = 0;
        SQLPOINTER value = NULL;
        SQLLEN buffer_len = 0;
        HRESULT hr;

        if (!Rowset_BindingParticipatesInInsert(binding, p_data, self->column_count))
        {
            continue;
        }
        if (param_index >= self->insert_param_count || self->insert_ordinals[param_index] != binding->iOrdinal)
        {
            CoTaskMemFree(scratch_buffers);
            Rowset_FreeInsertOwnedBuffers(owned_buffers, self->insert_param_count);
            CoTaskMemFree(indicators);
            return S_FALSE;
        }

        col = &self->columns[binding->iOrdinal - 1];
        hr = Rowset_GetPreparedInsertParameter(
            binding,
            col,
            p_data,
            &c_type,
            &sql_type,
            &column_size,
            &scale,
            &value,
            &buffer_len,
            &indicators[param_index],
            &owned_buffers[param_index],
            scratch_buffers + ((size_t)param_index * 128U),
            128U);
        if (hr != S_OK)
        {
            CoTaskMemFree(scratch_buffers);
            Rowset_FreeInsertOwnedBuffers(owned_buffers, self->insert_param_count);
            CoTaskMemFree(indicators);
            return hr;
        }

        rc = SQLBindParameter(
            self->insert_hstmt,
            (SQLUSMALLINT)(param_index + 1),
            SQL_PARAM_INPUT,
            c_type,
            sql_type,
            column_size,
            scale,
            value,
            buffer_len,
            &indicators[param_index]);
        if (!SQL_SUCCEEDED(rc))
        {
            CHAR message[1024];
            Odbc_GetErrorMessage(SQL_HANDLE_STMT, self->insert_hstmt, DB_E_ERRORSINCOMMAND, message, MONET_ARRAY_SIZE(message));
            Log_WriteA(MONET_LOG_ERROR, "Rowset::InsertRow", "SQLBindParameter ordinal=%lu %s", (unsigned long)binding->iOrdinal, message);
            CoTaskMemFree(scratch_buffers);
            Rowset_FreeInsertOwnedBuffers(owned_buffers, self->insert_param_count);
            CoTaskMemFree(indicators);
            return DB_E_ERRORSINCOMMAND;
        }

        ++param_index;
    }

    if (param_index != self->insert_param_count)
    {
        CoTaskMemFree(scratch_buffers);
        Rowset_FreeInsertOwnedBuffers(owned_buffers, self->insert_param_count);
        CoTaskMemFree(indicators);
        return S_FALSE;
    }

    Log_WriteQueryA(
        "ODBC::ExecutePrepared",
        "prepared insert table=\"%s\" params=%llu",
        self->base_table,
        (unsigned long long)self->insert_param_count);
    rc = SQLExecute(self->insert_hstmt);
    if (!SQL_SUCCEEDED(rc))
    {
        CHAR message[1024];
        Odbc_GetErrorMessage(SQL_HANDLE_STMT, self->insert_hstmt, DB_E_ERRORSINCOMMAND, message, MONET_ARRAY_SIZE(message));
        Log_WriteA(MONET_LOG_ERROR, "ODBC::ExecutePrepared", "%s", message);
        SQLFreeStmt(self->insert_hstmt, SQL_CLOSE);
        CoTaskMemFree(scratch_buffers);
        Rowset_FreeInsertOwnedBuffers(owned_buffers, self->insert_param_count);
        CoTaskMemFree(indicators);
        return DB_E_ERRORSINCOMMAND;
    }

    SQLFreeStmt(self->insert_hstmt, SQL_CLOSE);
    CoTaskMemFree(scratch_buffers);
    Rowset_FreeInsertOwnedBuffers(owned_buffers, self->insert_param_count);
    CoTaskMemFree(indicators);
    return S_OK;
}

static void Rowset_DetectUpdatability(MonetRowset* self)
{
    if (!self)
    {
        return;
    }

    self->updatability = 0;
    self->base_schema[0] = '\0';
    self->base_table[0] = '\0';

    if (self->command && self->command->sql_text[0] &&
        Rowset_ParseSimpleSelectTargetSql(
            self->command->sql_text,
            self->base_schema,
            MONET_ARRAY_SIZE(self->base_schema),
            self->base_table,
            MONET_ARRAY_SIZE(self->base_table)))
    {
        self->updatability = DBPROPVAL_UP_INSERT;
        MONET_TRACE(
            "Rowset::Create",
            "insertable rowset schema='%s' table='%s' updatability=0x%08lx",
            self->base_schema,
            self->base_table,
            (unsigned long)self->updatability);
    }
}

static HRESULT Rowset_CopyAnsi(const CHAR* text, DBLENGTH text_len, BYTE* dest, DBLENGTH max_len, DBSTATUS* pstatus)
{
    DBLENGTH copy_len = text_len;

    if (max_len == 0)
    {
        *pstatus = DBSTATUS_E_CANTCONVERTVALUE;
        return DB_E_ERRORSOCCURRED;
    }

    if (copy_len >= max_len)
    {
        copy_len = max_len - 1;
        *pstatus = DBSTATUS_S_TRUNCATED;
    }
    else
    {
        *pstatus = DBSTATUS_S_OK;
    }

    memcpy(dest, text, copy_len);
    dest[copy_len] = '\0';
    return S_OK;
}

static HRESULT Rowset_CopyWideBuffer(const WCHAR* text, DBLENGTH text_bytes, WCHAR* dest, DBLENGTH max_bytes, DBSTATUS* pstatus)
{
    DBLENGTH copy_bytes = text_bytes;

    if (max_bytes < (DBLENGTH)sizeof(WCHAR))
    {
        *pstatus = DBSTATUS_E_CANTCONVERTVALUE;
        return DB_E_ERRORSOCCURRED;
    }

    if (!text)
    {
        dest[0] = L'\0';
        *pstatus = DBSTATUS_S_OK;
        return S_OK;
    }

    if (copy_bytes + (DBLENGTH)sizeof(WCHAR) > max_bytes)
    {
        copy_bytes = max_bytes - sizeof(WCHAR);
        copy_bytes -= (copy_bytes % sizeof(WCHAR));
        *pstatus = DBSTATUS_S_TRUNCATED;
    }
    else
    {
        *pstatus = DBSTATUS_S_OK;
    }

    if (copy_bytes > 0)
    {
        memcpy(dest, text, (size_t)copy_bytes);
    }
    dest[copy_bytes / sizeof(WCHAR)] = L'\0';
    return S_OK;
}

static HRESULT Rowset_SetAnsiCacheText(MonetCellValue* cell, const CHAR* text)
{
    size_t length = 0;
    CHAR* buffer = NULL;

    if (!cell)
    {
        return E_POINTER;
    }

    if (cell->text)
    {
        return S_OK;
    }

    length = text ? strlen(text) : 0;
    buffer = (CHAR*)CoTaskMemAlloc(length + 1);
    if (!buffer)
    {
        return E_OUTOFMEMORY;
    }

    if (length > 0)
    {
        memcpy(buffer, text, length);
    }
    buffer[length] = '\0';
    cell->text = buffer;
    cell->length = (DBLENGTH)length;
    return S_OK;
}

static HRESULT Rowset_SetWideCacheText(MonetCellValue* cell, const WCHAR* text, DBLENGTH text_bytes)
{
    WCHAR* buffer = NULL;
    size_t cch = 0;

    if (!cell)
    {
        return E_POINTER;
    }
    if (cell->wide_text)
    {
        return S_OK;
    }

    if (text && text_bytes > 0)
    {
        cch = (size_t)(text_bytes / sizeof(WCHAR));
    }
    else if (text)
    {
        cch = wcslen(text);
        text_bytes = (DBLENGTH)(cch * sizeof(WCHAR));
    }

    buffer = (WCHAR*)CoTaskMemAlloc(((size_t)cch + 1U) * sizeof(WCHAR));
    if (!buffer)
    {
        return E_OUTOFMEMORY;
    }

    if (cch > 0 && text)
    {
        memcpy(buffer, text, (size_t)text_bytes);
    }
    buffer[cch] = L'\0';
    cell->wide_text = buffer;
    cell->wide_length = (DBLENGTH)(cch * sizeof(WCHAR));
    return S_OK;
}

static HRESULT Rowset_EnsureAnsiCache(MonetCellValue* cell, const MonetColumnInfo* col)
{
    CHAR buffer[96];
    int written = 0;

    MONET_UNUSED(col);
    if (!cell)
    {
        return E_POINTER;
    }
    if (cell->text)
    {
        return S_OK;
    }
    if (cell->is_null)
    {
        return Rowset_SetAnsiCacheText(cell, "");
    }
    if (cell->storage_c_type == 0)
    {
        return Rowset_SetAnsiCacheText(cell, "");
    }

    switch (cell->storage_c_type)
    {
    case SQL_C_BIT:
        return Rowset_SetAnsiCacheText(cell, cell->native.bit_value ? "1" : "0");

    case SQL_C_UTINYINT:
        written = _snprintf(buffer, MONET_ARRAY_SIZE(buffer), "%u", (unsigned)cell->native.utiny_value);
        break;

    case SQL_C_SSHORT:
        written = _snprintf(buffer, MONET_ARRAY_SIZE(buffer), "%d", (int)cell->native.sshort_value);
        break;

    case SQL_C_SLONG:
        written = _snprintf(buffer, MONET_ARRAY_SIZE(buffer), "%ld", (long)cell->native.slong_value);
        break;

    case SQL_C_SBIGINT:
        written = _snprintf(buffer, MONET_ARRAY_SIZE(buffer), "%lld", (long long)cell->native.sbigint_value);
        break;

    case SQL_C_FLOAT:
        written = _snprintf(buffer, MONET_ARRAY_SIZE(buffer), "%.9g", (double)cell->native.real_value);
        break;

    case SQL_C_DOUBLE:
        written = _snprintf(buffer, MONET_ARRAY_SIZE(buffer), "%.17g", (double)cell->native.double_value);
        break;

    case SQL_C_TYPE_DATE:
        written = _snprintf(
            buffer,
            MONET_ARRAY_SIZE(buffer),
            "%04d-%02u-%02u",
            (int)cell->native.date_value.year,
            (unsigned)cell->native.date_value.month,
            (unsigned)cell->native.date_value.day);
        break;

    case SQL_C_TYPE_TIME:
        written = _snprintf(
            buffer,
            MONET_ARRAY_SIZE(buffer),
            "%02u:%02u:%02u",
            (unsigned)cell->native.time_value.hour,
            (unsigned)cell->native.time_value.minute,
            (unsigned)cell->native.time_value.second);
        break;

    case SQL_C_TYPE_TIMESTAMP:
    {
        CHAR fraction[16];
        int fraction_length = 0;
        written = _snprintf(
            buffer,
            MONET_ARRAY_SIZE(buffer),
            "%04d-%02u-%02u %02u:%02u:%02u",
            (int)cell->native.timestamp_value.year,
            (unsigned)cell->native.timestamp_value.month,
            (unsigned)cell->native.timestamp_value.day,
            (unsigned)cell->native.timestamp_value.hour,
            (unsigned)cell->native.timestamp_value.minute,
            (unsigned)cell->native.timestamp_value.second);
        if (written < 0 || written >= (int)MONET_ARRAY_SIZE(buffer))
        {
            return DB_E_ERRORSOCCURRED;
        }
        if (cell->native.timestamp_value.fraction > 0)
        {
            _snprintf(fraction, MONET_ARRAY_SIZE(fraction), "%09lu", (unsigned long)cell->native.timestamp_value.fraction);
            fraction[MONET_ARRAY_SIZE(fraction) - 1] = '\0';
            fraction_length = (int)strlen(fraction);
            while (fraction_length > 0 && fraction[fraction_length - 1] == '0')
            {
                fraction[--fraction_length] = '\0';
            }
            if (fraction_length > 0)
            {
                if ((size_t)written + 1U + (size_t)fraction_length >= MONET_ARRAY_SIZE(buffer))
                {
                    return DB_E_ERRORSOCCURRED;
                }
                buffer[written++] = '.';
                memcpy(buffer + written, fraction, (size_t)fraction_length);
                written += fraction_length;
                buffer[written] = '\0';
            }
        }
        return Rowset_SetAnsiCacheText(cell, buffer);
    }

    case SQL_C_WCHAR:
    {
        int needed = 0;
        CHAR* ansi = NULL;

        if (!cell->wide_text)
        {
            return DB_E_ERRORSOCCURRED;
        }
        needed = WideCharToMultiByte(CP_ACP, 0, cell->wide_text, -1, NULL, 0, NULL, NULL);
        if (needed <= 0)
        {
            return DB_E_ERRORSOCCURRED;
        }
        ansi = (CHAR*)CoTaskMemAlloc((size_t)needed);
        if (!ansi)
        {
            return E_OUTOFMEMORY;
        }
        if (WideCharToMultiByte(CP_ACP, 0, cell->wide_text, -1, ansi, needed, NULL, NULL) <= 0)
        {
            CoTaskMemFree(ansi);
            return DB_E_ERRORSOCCURRED;
        }
        cell->text = ansi;
        cell->length = (DBLENGTH)strlen(ansi);
        return S_OK;
    }

    default:
        return DB_E_ERRORSOCCURRED;
    }

    if (written < 0 || written >= (int)MONET_ARRAY_SIZE(buffer))
    {
        return DB_E_ERRORSOCCURRED;
    }

    buffer[written] = '\0';
    return Rowset_SetAnsiCacheText(cell, buffer);
}

static HRESULT Rowset_EnsureWideCache(MonetCellValue* cell, const MonetColumnInfo* col)
{
    int required_wchars = 0;
    HRESULT hr = S_OK;

    if (!cell)
    {
        return E_POINTER;
    }
    if (cell->wide_text)
    {
        return S_OK;
    }
    if (!cell->text)
    {
        hr = Rowset_EnsureAnsiCache(cell, col);
        if (FAILED(hr))
        {
            return hr;
        }
    }

    required_wchars = MultiByteToWideChar(CP_ACP, 0, cell->text ? cell->text : "", -1, NULL, 0);
    if (required_wchars <= 0)
    {
        return DB_E_ERRORSOCCURRED;
    }

    cell->wide_text = (WCHAR*)CoTaskMemAlloc(sizeof(WCHAR) * required_wchars);
    if (!cell->wide_text)
    {
        return E_OUTOFMEMORY;
    }

    if (MultiByteToWideChar(CP_ACP, 0, cell->text ? cell->text : "", -1, cell->wide_text, required_wchars) <= 0)
    {
        CoTaskMemFree(cell->wide_text);
        cell->wide_text = NULL;
        return DB_E_ERRORSOCCURRED;
    }

    cell->wide_length = (DBLENGTH)((required_wchars - 1) * sizeof(WCHAR));
    return S_OK;
}

static BOOL Rowset_ParseBool(const CHAR* text, VARIANT_BOOL* value)
{
    if (!_stricmp(text, "1") || !_stricmp(text, "true") || !_stricmp(text, "t") || !_stricmp(text, "yes"))
    {
        *value = VARIANT_TRUE;
        return TRUE;
    }
    if (!_stricmp(text, "0") || !_stricmp(text, "false") || !_stricmp(text, "f") || !_stricmp(text, "no"))
    {
        *value = VARIANT_FALSE;
        return TRUE;
    }
    return FALSE;
}

static HRESULT Rowset_CopyNumeric(const CHAR* text, BYTE precision, BYTE scale, DB_NUMERIC* numeric)
{
    CHAR digits[64];
    size_t digit_count = 0;
    int sign = 1;
    const CHAR* p = text;
    BOOL seen_dot = FALSE;
    int fractional_count = 0;
    unsigned long long value64 = 0ULL;
    size_t i;

    ZeroMemory(numeric, sizeof(*numeric));
    numeric->precision = precision ? precision : 38;
    numeric->scale = scale;
    numeric->sign = 1;

    if (*p == '-')
    {
        sign = -1;
        ++p;
    }
    else if (*p == '+')
    {
        ++p;
    }

    while (*p)
    {
        if (*p == '.')
        {
            seen_dot = TRUE;
            ++p;
            continue;
        }
        if (!isdigit((unsigned char)*p))
        {
            break;
        }
        if (digit_count < MONET_ARRAY_SIZE(digits) - 1)
        {
            digits[digit_count++] = *p;
        }
        if (seen_dot)
        {
            ++fractional_count;
        }
        ++p;
    }
    digits[digit_count] = '\0';

    while (fractional_count < scale && digit_count < MONET_ARRAY_SIZE(digits) - 1)
    {
        digits[digit_count++] = '0';
        digits[digit_count] = '\0';
        ++fractional_count;
    }

    while (fractional_count > scale && digit_count > 0)
    {
        --digit_count;
        digits[digit_count] = '\0';
        --fractional_count;
    }

    value64 = _strtoui64(digits[0] ? digits : "0", NULL, 10);
    for (i = 0; i < sizeof(numeric->val); ++i)
    {
        numeric->val[i] = (BYTE)((value64 >> (i * 8)) & 0xFF);
    }
    if (sign < 0)
    {
        numeric->sign = 0;
    }
    return S_OK;
}

static HRESULT Rowset_ParseDate(const CHAR* text, DBDATE* date)
{
    int year = 0, month = 0, day = 0;
    if (sscanf(text, "%d-%d-%d", &year, &month, &day) != 3)
    {
        return DB_E_ERRORSOCCURRED;
    }
    date->year = (SHORT)year;
    date->month = (USHORT)month;
    date->day = (USHORT)day;
    return S_OK;
}

static HRESULT Rowset_ParseTime(const CHAR* text, DBTIME* time_value)
{
    int hour = 0, minute = 0, second = 0;
    if (sscanf(text, "%d:%d:%d", &hour, &minute, &second) != 3)
    {
        return DB_E_ERRORSOCCURRED;
    }
    time_value->hour = (USHORT)hour;
    time_value->minute = (USHORT)minute;
    time_value->second = (USHORT)second;
    return S_OK;
}

static HRESULT Rowset_ParseTimestamp(const CHAR* text, DBTIMESTAMP* ts)
{
    int year = 0, month = 0, day = 0, hour = 0, minute = 0, second = 0, frac = 0;
    if (sscanf(text, "%d-%d-%d %d:%d:%d.%d", &year, &month, &day, &hour, &minute, &second, &frac) < 6)
    {
        if (sscanf(text, "%d-%d-%dT%d:%d:%d.%d", &year, &month, &day, &hour, &minute, &second, &frac) < 6)
        {
            return DB_E_ERRORSOCCURRED;
        }
    }
    ts->year = (SHORT)year;
    ts->month = (USHORT)month;
    ts->day = (USHORT)day;
    ts->hour = (USHORT)hour;
    ts->minute = (USHORT)minute;
    ts->second = (USHORT)second;
    ts->fraction = (ULONG)frac;
    return S_OK;
}

static HRESULT Rowset_ParseVariantDate(const CHAR* text, DATE* value)
{
    SYSTEMTIME st;
    int year = 0, month = 0, day = 0;
    int hour = 0, minute = 0, second = 0;
    int matched = 0;

    if (!text || !value)
    {
        return E_POINTER;
    }

    ZeroMemory(&st, sizeof(st));
    matched = sscanf(text, "%d-%d-%d %d:%d:%d", &year, &month, &day, &hour, &minute, &second);
    if (matched < 3)
    {
        matched = sscanf(text, "%d-%d-%dT%d:%d:%d", &year, &month, &day, &hour, &minute, &second);
    }
    if (matched < 3)
    {
        return DB_E_ERRORSOCCURRED;
    }

    st.wYear = (WORD)year;
    st.wMonth = (WORD)month;
    st.wDay = (WORD)day;
    st.wHour = (WORD)((matched >= 6) ? hour : 0);
    st.wMinute = (WORD)((matched >= 6) ? minute : 0);
    st.wSecond = (WORD)((matched >= 6) ? second : 0);

    if (!SystemTimeToVariantTime(&st, value))
    {
        return DB_E_ERRORSOCCURRED;
    }

    return S_OK;
}

static HRESULT Rowset_ParseGuid(const CHAR* text, GUID* guid)
{
    WCHAR wide[64];

    if (!text || !guid)
    {
        return E_POINTER;
    }

    Monet_AnsiToWide(wide, MONET_ARRAY_SIZE(wide), text);
    return SUCCEEDED(CLSIDFromString(wide, guid)) ? S_OK : DB_E_ERRORSOCCURRED;
}

static const CHAR* Rowset_DbTypeName(DBTYPE type)
{
    switch (type)
    {
    case DBTYPE_BYREF | DBTYPE_STR: return "DBTYPE_BYREF|DBTYPE_STR";
    case DBTYPE_BYREF | DBTYPE_WSTR: return "DBTYPE_BYREF|DBTYPE_WSTR";
    case DBTYPE_BYREF | DBTYPE_BYTES: return "DBTYPE_BYREF|DBTYPE_BYTES";
    case DBTYPE_BOOL: return "DBTYPE_BOOL";
    case DBTYPE_UI1: return "DBTYPE_UI1";
    case DBTYPE_I2: return "DBTYPE_I2";
    case DBTYPE_UI2: return "DBTYPE_UI2";
    case DBTYPE_I4: return "DBTYPE_I4";
    case DBTYPE_UI4: return "DBTYPE_UI4";
    case DBTYPE_I8: return "DBTYPE_I8";
    case DBTYPE_UI8: return "DBTYPE_UI8";
    case DBTYPE_R4: return "DBTYPE_R4";
    case DBTYPE_R8: return "DBTYPE_R8";
    case DBTYPE_DATE: return "DBTYPE_DATE";
    case DBTYPE_GUID: return "DBTYPE_GUID";
    case DBTYPE_BYTES: return "DBTYPE_BYTES";
    case DBTYPE_STR: return "DBTYPE_STR";
    case DBTYPE_WSTR: return "DBTYPE_WSTR";
    case DBTYPE_NUMERIC: return "DBTYPE_NUMERIC";
    case DBTYPE_DBDATE: return "DBTYPE_DBDATE";
    case DBTYPE_DBTIME: return "DBTYPE_DBTIME";
    case DBTYPE_DBTIMESTAMP: return "DBTYPE_DBTIMESTAMP";
    default: return "DBTYPE_<other>";
    }
}

static void Rowset_LogConvertFailure(const MonetColumnInfo* col, const DBBINDING* binding, const MonetCellValue* cell)
{
    Log_WriteA(
        MONET_LOG_ERROR,
        "Rowset::GetData",
        "Conversione fallita colonna='%s' src='%s' dst=%s(0x%04X) value='%s'",
        col ? col->name_a : "<unknown>",
        (col ? Rowset_DbTypeName(col->type) : "<unknown>"),
        Rowset_DbTypeName(binding ? binding->wType : 0),
        (unsigned int)(binding ? binding->wType : 0),
        (cell && !cell->is_null && cell->text) ? cell->text : "<null>");
}

static void Rowset_SetSchemaColumnMeta(MonetColumnInfo* col, DBTYPE type, DBLENGTH column_size, BYTE precision, BYTE scale, DBCOLUMNFLAGS add_flags, DBCOLUMNFLAGS clear_flags)
{
    if (!col)
    {
        return;
    }

    col->type = type;
    col->column_size = column_size;
    col->precision = precision;
    col->scale = scale;
    col->flags &= ~clear_flags;
    col->flags |= add_flags;
}

static void Rowset_AdjustSchemaColumnMeta(REFGUID schema_rowset, MonetColumnInfo* col)
{
    if (!schema_rowset || !col)
    {
        return;
    }

    if (IsEqualGUID(schema_rowset, &DBSCHEMA_TABLES))
    {
        if (!_stricmp(col->name_a, "TABLE_CATALOG") ||
            !_stricmp(col->name_a, "TABLE_SCHEMA") ||
            !_stricmp(col->name_a, "TABLE_NAME") ||
            !_stricmp(col->name_a, "TABLE_TYPE") ||
            !_stricmp(col->name_a, "DESCRIPTION"))
        {
            Rowset_SetSchemaColumnMeta(col, DBTYPE_WSTR, col->column_size, 0, 0, 0, DBCOLUMNFLAGS_ISFIXEDLENGTH);
        }
        else if (!_stricmp(col->name_a, "TABLE_GUID"))
        {
            Rowset_SetSchemaColumnMeta(col, DBTYPE_GUID, sizeof(GUID), 0, 0, DBCOLUMNFLAGS_ISFIXEDLENGTH, 0);
        }
        else if (!_stricmp(col->name_a, "TABLE_PROPID"))
        {
            Rowset_SetSchemaColumnMeta(col, DBTYPE_UI4, 10, 10, 0, DBCOLUMNFLAGS_ISFIXEDLENGTH, 0);
        }
        else if (!_stricmp(col->name_a, "DATE_CREATED") || !_stricmp(col->name_a, "DATE_MODIFIED"))
        {
            Rowset_SetSchemaColumnMeta(col, DBTYPE_DATE, sizeof(DATE), 0, 0, DBCOLUMNFLAGS_ISFIXEDLENGTH, 0);
        }
    }
    else if (IsEqualGUID(schema_rowset, &DBSCHEMA_COLUMNS))
    {
        if (!_stricmp(col->name_a, "TABLE_CATALOG") ||
            !_stricmp(col->name_a, "TABLE_SCHEMA") ||
            !_stricmp(col->name_a, "TABLE_NAME") ||
            !_stricmp(col->name_a, "COLUMN_NAME") ||
            !_stricmp(col->name_a, "COLUMN_DEFAULT") ||
            !_stricmp(col->name_a, "CHARACTER_SET_CATALOG") ||
            !_stricmp(col->name_a, "CHARACTER_SET_SCHEMA") ||
            !_stricmp(col->name_a, "CHARACTER_SET_NAME") ||
            !_stricmp(col->name_a, "COLLATION_CATALOG") ||
            !_stricmp(col->name_a, "COLLATION_SCHEMA") ||
            !_stricmp(col->name_a, "COLLATION_NAME") ||
            !_stricmp(col->name_a, "DOMAIN_CATALOG") ||
            !_stricmp(col->name_a, "DOMAIN_SCHEMA") ||
            !_stricmp(col->name_a, "DOMAIN_NAME") ||
            !_stricmp(col->name_a, "DESCRIPTION"))
        {
            Rowset_SetSchemaColumnMeta(col, DBTYPE_WSTR, col->column_size, 0, 0, 0, DBCOLUMNFLAGS_ISFIXEDLENGTH);
        }
        else if (!_stricmp(col->name_a, "COLUMN_GUID") || !_stricmp(col->name_a, "TYPE_GUID"))
        {
            Rowset_SetSchemaColumnMeta(col, DBTYPE_GUID, sizeof(GUID), 0, 0, DBCOLUMNFLAGS_ISFIXEDLENGTH, 0);
        }
        else if (!_stricmp(col->name_a, "COLUMN_PROPID") ||
                 !_stricmp(col->name_a, "ORDINAL_POSITION") ||
                 !_stricmp(col->name_a, "COLUMN_FLAGS") ||
                 !_stricmp(col->name_a, "CHARACTER_MAXIMUM_LENGTH") ||
                 !_stricmp(col->name_a, "CHARACTER_OCTET_LENGTH") ||
                 !_stricmp(col->name_a, "DATETIME_PRECISION"))
        {
            Rowset_SetSchemaColumnMeta(col, DBTYPE_UI4, 10, 10, 0, DBCOLUMNFLAGS_ISFIXEDLENGTH, 0);
        }
        else if (!_stricmp(col->name_a, "COLUMN_HASDEFAULT") || !_stricmp(col->name_a, "IS_NULLABLE"))
        {
            Rowset_SetSchemaColumnMeta(col, DBTYPE_BOOL, sizeof(VARIANT_BOOL), 0, 0, DBCOLUMNFLAGS_ISFIXEDLENGTH, 0);
        }
        else if (!_stricmp(col->name_a, "DATA_TYPE") || !_stricmp(col->name_a, "NUMERIC_PRECISION"))
        {
            Rowset_SetSchemaColumnMeta(col, DBTYPE_UI2, 5, 5, 0, DBCOLUMNFLAGS_ISFIXEDLENGTH, 0);
        }
        else if (!_stricmp(col->name_a, "NUMERIC_SCALE"))
        {
            Rowset_SetSchemaColumnMeta(col, DBTYPE_I2, 5, 5, 0, DBCOLUMNFLAGS_ISFIXEDLENGTH, 0);
        }
    }
    else if (IsEqualGUID(schema_rowset, &DBSCHEMA_PROVIDER_TYPES))
    {
        if (!_stricmp(col->name_a, "TYPE_NAME") ||
            !_stricmp(col->name_a, "LITERAL_PREFIX") ||
            !_stricmp(col->name_a, "LITERAL_SUFFIX") ||
            !_stricmp(col->name_a, "CREATE_PARAMS") ||
            !_stricmp(col->name_a, "LOCAL_TYPE_NAME") ||
            !_stricmp(col->name_a, "TYPELIB") ||
            !_stricmp(col->name_a, "VERSION"))
        {
            Rowset_SetSchemaColumnMeta(col, DBTYPE_WSTR, col->column_size, 0, 0, 0, DBCOLUMNFLAGS_ISFIXEDLENGTH);
        }
        else if (!_stricmp(col->name_a, "DATA_TYPE"))
        {
            Rowset_SetSchemaColumnMeta(col, DBTYPE_UI2, 5, 5, 0, DBCOLUMNFLAGS_ISFIXEDLENGTH, 0);
        }
        else if (!_stricmp(col->name_a, "COLUMN_SIZE") || !_stricmp(col->name_a, "SEARCHABLE"))
        {
            Rowset_SetSchemaColumnMeta(col, DBTYPE_UI4, 10, 10, 0, DBCOLUMNFLAGS_ISFIXEDLENGTH, 0);
        }
        else if (!_stricmp(col->name_a, "IS_NULLABLE") ||
                 !_stricmp(col->name_a, "CASE_SENSITIVE") ||
                 !_stricmp(col->name_a, "UNSIGNED_ATTRIBUTE") ||
                 !_stricmp(col->name_a, "FIXED_PREC_SCALE") ||
                 !_stricmp(col->name_a, "AUTO_UNIQUE_VALUE") ||
                 !_stricmp(col->name_a, "IS_LONG") ||
                 !_stricmp(col->name_a, "BEST_MATCH") ||
                 !_stricmp(col->name_a, "IS_FIXEDLENGTH"))
        {
            Rowset_SetSchemaColumnMeta(col, DBTYPE_BOOL, sizeof(VARIANT_BOOL), 0, 0, DBCOLUMNFLAGS_ISFIXEDLENGTH, 0);
        }
        else if (!_stricmp(col->name_a, "MINIMUM_SCALE") || !_stricmp(col->name_a, "MAXIMUM_SCALE"))
        {
            Rowset_SetSchemaColumnMeta(col, DBTYPE_I2, 5, 5, 0, DBCOLUMNFLAGS_ISFIXEDLENGTH, 0);
        }
        else if (!_stricmp(col->name_a, "GUID"))
        {
            Rowset_SetSchemaColumnMeta(col, DBTYPE_GUID, sizeof(GUID), 0, 0, DBCOLUMNFLAGS_ISFIXEDLENGTH, 0);
        }
    }
    else if (IsEqualGUID(schema_rowset, &DBSCHEMA_PRIMARY_KEYS))
    {
        if (!_stricmp(col->name_a, "TABLE_CATALOG") ||
            !_stricmp(col->name_a, "TABLE_SCHEMA") ||
            !_stricmp(col->name_a, "TABLE_NAME") ||
            !_stricmp(col->name_a, "COLUMN_NAME") ||
            !_stricmp(col->name_a, "PK_NAME"))
        {
            Rowset_SetSchemaColumnMeta(col, DBTYPE_WSTR, col->column_size, 0, 0, 0, DBCOLUMNFLAGS_ISFIXEDLENGTH);
        }
        else if (!_stricmp(col->name_a, "COLUMN_GUID"))
        {
            Rowset_SetSchemaColumnMeta(col, DBTYPE_GUID, sizeof(GUID), 0, 0, DBCOLUMNFLAGS_ISFIXEDLENGTH, 0);
        }
        else if (!_stricmp(col->name_a, "COLUMN_PROPID") || !_stricmp(col->name_a, "ORDINAL"))
        {
            Rowset_SetSchemaColumnMeta(col, DBTYPE_UI4, 10, 10, 0, DBCOLUMNFLAGS_ISFIXEDLENGTH, 0);
        }
    }
    else if (IsEqualGUID(schema_rowset, &DBSCHEMA_FOREIGN_KEYS))
    {
        if (!_stricmp(col->name_a, "PK_TABLE_CATALOG") ||
            !_stricmp(col->name_a, "PK_TABLE_SCHEMA") ||
            !_stricmp(col->name_a, "PK_TABLE_NAME") ||
            !_stricmp(col->name_a, "PK_COLUMN_NAME") ||
            !_stricmp(col->name_a, "FK_TABLE_CATALOG") ||
            !_stricmp(col->name_a, "FK_TABLE_SCHEMA") ||
            !_stricmp(col->name_a, "FK_TABLE_NAME") ||
            !_stricmp(col->name_a, "FK_COLUMN_NAME") ||
            !_stricmp(col->name_a, "UPDATE_RULE") ||
            !_stricmp(col->name_a, "DELETE_RULE") ||
            !_stricmp(col->name_a, "PK_NAME") ||
            !_stricmp(col->name_a, "FK_NAME"))
        {
            Rowset_SetSchemaColumnMeta(col, DBTYPE_WSTR, col->column_size, 0, 0, 0, DBCOLUMNFLAGS_ISFIXEDLENGTH);
        }
        else if (!_stricmp(col->name_a, "PK_COLUMN_GUID") || !_stricmp(col->name_a, "FK_COLUMN_GUID"))
        {
            Rowset_SetSchemaColumnMeta(col, DBTYPE_GUID, sizeof(GUID), 0, 0, DBCOLUMNFLAGS_ISFIXEDLENGTH, 0);
        }
        else if (!_stricmp(col->name_a, "PK_COLUMN_PROPID") ||
                 !_stricmp(col->name_a, "FK_COLUMN_PROPID") ||
                 !_stricmp(col->name_a, "ORDINAL"))
        {
            Rowset_SetSchemaColumnMeta(col, DBTYPE_UI4, 10, 10, 0, DBCOLUMNFLAGS_ISFIXEDLENGTH, 0);
        }
        else if (!_stricmp(col->name_a, "DEFERRABILITY"))
        {
            Rowset_SetSchemaColumnMeta(col, DBTYPE_I2, 5, 5, 0, DBCOLUMNFLAGS_ISFIXEDLENGTH, 0);
        }
    }
    else if (IsEqualGUID(schema_rowset, &DBSCHEMA_INDEXES))
    {
        if (!_stricmp(col->name_a, "TABLE_CATALOG") ||
            !_stricmp(col->name_a, "TABLE_SCHEMA") ||
            !_stricmp(col->name_a, "TABLE_NAME") ||
            !_stricmp(col->name_a, "INDEX_CATALOG") ||
            !_stricmp(col->name_a, "INDEX_SCHEMA") ||
            !_stricmp(col->name_a, "INDEX_NAME") ||
            !_stricmp(col->name_a, "COLUMN_NAME") ||
            !_stricmp(col->name_a, "FILTER_CONDITION"))
        {
            Rowset_SetSchemaColumnMeta(col, DBTYPE_WSTR, col->column_size, 0, 0, 0, DBCOLUMNFLAGS_ISFIXEDLENGTH);
        }
        else if (!_stricmp(col->name_a, "PRIMARY_KEY") ||
                 !_stricmp(col->name_a, "UNIQUE") ||
                 !_stricmp(col->name_a, "CLUSTERED") ||
                 !_stricmp(col->name_a, "SORT_BOOKMARKS") ||
                 !_stricmp(col->name_a, "AUTO_UPDATE") ||
                 !_stricmp(col->name_a, "INTEGRATED"))
        {
            Rowset_SetSchemaColumnMeta(col, DBTYPE_BOOL, sizeof(VARIANT_BOOL), 0, 0, DBCOLUMNFLAGS_ISFIXEDLENGTH, 0);
        }
        else if (!_stricmp(col->name_a, "TYPE"))
        {
            Rowset_SetSchemaColumnMeta(col, DBTYPE_UI2, 5, 5, 0, DBCOLUMNFLAGS_ISFIXEDLENGTH, 0);
        }
        else if (!_stricmp(col->name_a, "FILL_FACTOR") ||
                 !_stricmp(col->name_a, "INITIAL_SIZE") ||
                 !_stricmp(col->name_a, "NULLS") ||
                 !_stricmp(col->name_a, "NULL_COLLATION") ||
                 !_stricmp(col->name_a, "PAGES"))
        {
            Rowset_SetSchemaColumnMeta(col, DBTYPE_I4, 10, 10, 0, DBCOLUMNFLAGS_ISFIXEDLENGTH, 0);
        }
        else if (!_stricmp(col->name_a, "ORDINAL_POSITION") || !_stricmp(col->name_a, "COLUMN_PROPID"))
        {
            Rowset_SetSchemaColumnMeta(col, DBTYPE_UI4, 10, 10, 0, DBCOLUMNFLAGS_ISFIXEDLENGTH, 0);
        }
        else if (!_stricmp(col->name_a, "COLUMN_GUID"))
        {
            Rowset_SetSchemaColumnMeta(col, DBTYPE_GUID, sizeof(GUID), 0, 0, DBCOLUMNFLAGS_ISFIXEDLENGTH, 0);
        }
        else if (!_stricmp(col->name_a, "COLLATION"))
        {
            Rowset_SetSchemaColumnMeta(col, DBTYPE_I2, 5, 5, 0, DBCOLUMNFLAGS_ISFIXEDLENGTH, 0);
        }
        else if (!_stricmp(col->name_a, "CARDINALITY"))
        {
            Rowset_SetSchemaColumnMeta(col, DBTYPE_UI8, 20, 20, 0, DBCOLUMNFLAGS_ISFIXEDLENGTH, 0);
        }
    }
}

static void Rowset_AdjustSchemaMetadata(REFGUID schema_rowset, MonetColumnInfo* columns, DBORDINAL column_count)
{
    DBORDINAL i;

    if (!schema_rowset || !columns)
    {
        return;
    }

    for (i = 0; i < column_count; ++i)
    {
        Rowset_AdjustSchemaColumnMeta(schema_rowset, &columns[i]);
    }
}

static HRESULT Rowset_CloneColumns(const MonetColumnInfo* source, DBORDINAL column_count, MonetColumnInfo** ppcolumns)
{
    MonetColumnInfo* copy = NULL;

    if (!ppcolumns)
    {
        return E_POINTER;
    }

    *ppcolumns = NULL;
    if (!source || column_count == 0)
    {
        return S_OK;
    }

    copy = (MonetColumnInfo*)CoTaskMemAlloc(sizeof(MonetColumnInfo) * column_count);
    if (!copy)
    {
        return E_OUTOFMEMORY;
    }

    memcpy(copy, source, sizeof(MonetColumnInfo) * column_count);
    *ppcolumns = copy;
    return S_OK;
}

static HRESULT Rowset_SystemTimeToVariantDate(const SYSTEMTIME* st, DATE* value)
{
    if (!st || !value)
    {
        return E_POINTER;
    }

    return SystemTimeToVariantTime((LPSYSTEMTIME)st, value) ? S_OK : DB_E_ERRORSOCCURRED;
}

static HRESULT Rowset_CopyDateStructToVariantDate(const DATE_STRUCT* date_value, DATE* value)
{
    SYSTEMTIME st;

    if (!date_value || !value)
    {
        return E_POINTER;
    }

    ZeroMemory(&st, sizeof(st));
    st.wYear = (WORD)date_value->year;
    st.wMonth = (WORD)date_value->month;
    st.wDay = (WORD)date_value->day;
    return Rowset_SystemTimeToVariantDate(&st, value);
}

static HRESULT Rowset_CopyTimestampStructToVariantDate(const TIMESTAMP_STRUCT* ts, DATE* value)
{
    SYSTEMTIME st;

    if (!ts || !value)
    {
        return E_POINTER;
    }

    ZeroMemory(&st, sizeof(st));
    st.wYear = (WORD)ts->year;
    st.wMonth = (WORD)ts->month;
    st.wDay = (WORD)ts->day;
    st.wHour = (WORD)ts->hour;
    st.wMinute = (WORD)ts->minute;
    st.wSecond = (WORD)ts->second;
    st.wMilliseconds = (WORD)(ts->fraction / 1000000UL);
    return Rowset_SystemTimeToVariantDate(&st, value);
}

static HRESULT Rowset_ConvertNativeValue(MonetCellValue* cell, const MonetColumnInfo* col, const DBBINDING* binding, void* base, DBSTATUS* pstatus)
{
    BYTE* value_ptr = (BYTE*)Rowset_BindingPointer(base, binding->obValue);
    DBTYPE base_type = (DBTYPE)(binding->wType & ~DBTYPE_BYREF);

    MONET_UNUSED(col);
    if (!cell || cell->storage_c_type == 0)
    {
        return S_FALSE;
    }

    switch (base_type)
    {
    case DBTYPE_BOOL:
        if (cell->storage_c_type == SQL_C_BIT)
        {
            *((VARIANT_BOOL*)value_ptr) = cell->native.bit_value ? VARIANT_TRUE : VARIANT_FALSE;
            Rowset_SetLength(binding, base, sizeof(VARIANT_BOOL));
            *pstatus = DBSTATUS_S_OK;
            return S_OK;
        }
        break;

    case DBTYPE_UI1:
        if (cell->storage_c_type == SQL_C_UTINYINT || cell->storage_c_type == SQL_C_BIT)
        {
            *((BYTE*)value_ptr) = (cell->storage_c_type == SQL_C_UTINYINT) ? (BYTE)cell->native.utiny_value : (BYTE)(cell->native.bit_value ? 1 : 0);
            Rowset_SetLength(binding, base, sizeof(BYTE));
            *pstatus = DBSTATUS_S_OK;
            return S_OK;
        }
        break;

    case DBTYPE_I2:
        if (cell->storage_c_type == SQL_C_SSHORT || cell->storage_c_type == SQL_C_UTINYINT)
        {
            *((SHORT*)value_ptr) = (cell->storage_c_type == SQL_C_SSHORT) ? (SHORT)cell->native.sshort_value : (SHORT)cell->native.utiny_value;
            Rowset_SetLength(binding, base, sizeof(SHORT));
            *pstatus = DBSTATUS_S_OK;
            return S_OK;
        }
        break;

    case DBTYPE_UI2:
        if (cell->storage_c_type == SQL_C_UTINYINT)
        {
            *((USHORT*)value_ptr) = (USHORT)cell->native.utiny_value;
            Rowset_SetLength(binding, base, sizeof(USHORT));
            *pstatus = DBSTATUS_S_OK;
            return S_OK;
        }
        break;

    case DBTYPE_I4:
        if (cell->storage_c_type == SQL_C_SLONG)
        {
            *((LONG*)value_ptr) = (LONG)cell->native.slong_value;
            Rowset_SetLength(binding, base, sizeof(LONG));
            *pstatus = DBSTATUS_S_OK;
            return S_OK;
        }
        if (cell->storage_c_type == SQL_C_SSHORT)
        {
            *((LONG*)value_ptr) = (LONG)cell->native.sshort_value;
            Rowset_SetLength(binding, base, sizeof(LONG));
            *pstatus = DBSTATUS_S_OK;
            return S_OK;
        }
        if (cell->storage_c_type == SQL_C_UTINYINT)
        {
            *((LONG*)value_ptr) = (LONG)cell->native.utiny_value;
            Rowset_SetLength(binding, base, sizeof(LONG));
            *pstatus = DBSTATUS_S_OK;
            return S_OK;
        }
        break;

    case DBTYPE_UI4:
        if (cell->storage_c_type == SQL_C_UTINYINT)
        {
            *((ULONG*)value_ptr) = (ULONG)cell->native.utiny_value;
            Rowset_SetLength(binding, base, sizeof(ULONG));
            *pstatus = DBSTATUS_S_OK;
            return S_OK;
        }
        break;

    case DBTYPE_I8:
        if (cell->storage_c_type == SQL_C_SBIGINT)
        {
            *((LONGLONG*)value_ptr) = (LONGLONG)cell->native.sbigint_value;
            Rowset_SetLength(binding, base, sizeof(LONGLONG));
            *pstatus = DBSTATUS_S_OK;
            return S_OK;
        }
        if (cell->storage_c_type == SQL_C_SLONG)
        {
            *((LONGLONG*)value_ptr) = (LONGLONG)cell->native.slong_value;
            Rowset_SetLength(binding, base, sizeof(LONGLONG));
            *pstatus = DBSTATUS_S_OK;
            return S_OK;
        }
        if (cell->storage_c_type == SQL_C_SSHORT)
        {
            *((LONGLONG*)value_ptr) = (LONGLONG)cell->native.sshort_value;
            Rowset_SetLength(binding, base, sizeof(LONGLONG));
            *pstatus = DBSTATUS_S_OK;
            return S_OK;
        }
        if (cell->storage_c_type == SQL_C_UTINYINT)
        {
            *((LONGLONG*)value_ptr) = (LONGLONG)cell->native.utiny_value;
            Rowset_SetLength(binding, base, sizeof(LONGLONG));
            *pstatus = DBSTATUS_S_OK;
            return S_OK;
        }
        break;

    case DBTYPE_UI8:
        if (cell->storage_c_type == SQL_C_UTINYINT)
        {
            *((ULONGLONG*)value_ptr) = (ULONGLONG)cell->native.utiny_value;
            Rowset_SetLength(binding, base, sizeof(ULONGLONG));
            *pstatus = DBSTATUS_S_OK;
            return S_OK;
        }
        break;

    case DBTYPE_R4:
        if (cell->storage_c_type == SQL_C_FLOAT)
        {
            *((FLOAT*)value_ptr) = (FLOAT)cell->native.real_value;
            Rowset_SetLength(binding, base, sizeof(FLOAT));
            *pstatus = DBSTATUS_S_OK;
            return S_OK;
        }
        break;

    case DBTYPE_R8:
        if (cell->storage_c_type == SQL_C_DOUBLE)
        {
            *((DOUBLE*)value_ptr) = (DOUBLE)cell->native.double_value;
            Rowset_SetLength(binding, base, sizeof(DOUBLE));
            *pstatus = DBSTATUS_S_OK;
            return S_OK;
        }
        if (cell->storage_c_type == SQL_C_FLOAT)
        {
            *((DOUBLE*)value_ptr) = (DOUBLE)cell->native.real_value;
            Rowset_SetLength(binding, base, sizeof(DOUBLE));
            *pstatus = DBSTATUS_S_OK;
            return S_OK;
        }
        break;

    case DBTYPE_DBDATE:
        if (cell->storage_c_type == SQL_C_TYPE_DATE)
        {
            DBDATE* out = (DBDATE*)value_ptr;
            out->year = (SHORT)cell->native.date_value.year;
            out->month = (USHORT)cell->native.date_value.month;
            out->day = (USHORT)cell->native.date_value.day;
            Rowset_SetLength(binding, base, sizeof(DBDATE));
            *pstatus = DBSTATUS_S_OK;
            return S_OK;
        }
        if (cell->storage_c_type == SQL_C_TYPE_TIMESTAMP)
        {
            DBDATE* out = (DBDATE*)value_ptr;
            out->year = (SHORT)cell->native.timestamp_value.year;
            out->month = (USHORT)cell->native.timestamp_value.month;
            out->day = (USHORT)cell->native.timestamp_value.day;
            Rowset_SetLength(binding, base, sizeof(DBDATE));
            *pstatus = DBSTATUS_S_OK;
            return S_OK;
        }
        break;

    case DBTYPE_DBTIME:
        if (cell->storage_c_type == SQL_C_TYPE_TIME)
        {
            DBTIME* out = (DBTIME*)value_ptr;
            out->hour = (USHORT)cell->native.time_value.hour;
            out->minute = (USHORT)cell->native.time_value.minute;
            out->second = (USHORT)cell->native.time_value.second;
            Rowset_SetLength(binding, base, sizeof(DBTIME));
            *pstatus = DBSTATUS_S_OK;
            return S_OK;
        }
        if (cell->storage_c_type == SQL_C_TYPE_TIMESTAMP)
        {
            DBTIME* out = (DBTIME*)value_ptr;
            out->hour = (USHORT)cell->native.timestamp_value.hour;
            out->minute = (USHORT)cell->native.timestamp_value.minute;
            out->second = (USHORT)cell->native.timestamp_value.second;
            Rowset_SetLength(binding, base, sizeof(DBTIME));
            *pstatus = DBSTATUS_S_OK;
            return S_OK;
        }
        break;

    case DBTYPE_DBTIMESTAMP:
        if (cell->storage_c_type == SQL_C_TYPE_TIMESTAMP)
        {
            DBTIMESTAMP* out = (DBTIMESTAMP*)value_ptr;
            out->year = (SHORT)cell->native.timestamp_value.year;
            out->month = (USHORT)cell->native.timestamp_value.month;
            out->day = (USHORT)cell->native.timestamp_value.day;
            out->hour = (USHORT)cell->native.timestamp_value.hour;
            out->minute = (USHORT)cell->native.timestamp_value.minute;
            out->second = (USHORT)cell->native.timestamp_value.second;
            out->fraction = (ULONG)cell->native.timestamp_value.fraction;
            Rowset_SetLength(binding, base, sizeof(DBTIMESTAMP));
            *pstatus = DBSTATUS_S_OK;
            return S_OK;
        }
        if (cell->storage_c_type == SQL_C_TYPE_DATE)
        {
            DBTIMESTAMP* out = (DBTIMESTAMP*)value_ptr;
            ZeroMemory(out, sizeof(*out));
            out->year = (SHORT)cell->native.date_value.year;
            out->month = (USHORT)cell->native.date_value.month;
            out->day = (USHORT)cell->native.date_value.day;
            Rowset_SetLength(binding, base, sizeof(DBTIMESTAMP));
            *pstatus = DBSTATUS_S_OK;
            return S_OK;
        }
        break;

    case DBTYPE_DATE:
        if (cell->storage_c_type == SQL_C_TYPE_DATE)
        {
            HRESULT hr = Rowset_CopyDateStructToVariantDate(&cell->native.date_value, (DATE*)value_ptr);
            if (FAILED(hr))
            {
                *pstatus = DBSTATUS_E_CANTCONVERTVALUE;
                return DB_E_ERRORSOCCURRED;
            }
            Rowset_SetLength(binding, base, sizeof(DATE));
            *pstatus = DBSTATUS_S_OK;
            return S_OK;
        }
        if (cell->storage_c_type == SQL_C_TYPE_TIMESTAMP)
        {
            HRESULT hr = Rowset_CopyTimestampStructToVariantDate(&cell->native.timestamp_value, (DATE*)value_ptr);
            if (FAILED(hr))
            {
                *pstatus = DBSTATUS_E_CANTCONVERTVALUE;
                return DB_E_ERRORSOCCURRED;
            }
            Rowset_SetLength(binding, base, sizeof(DATE));
            *pstatus = DBSTATUS_S_OK;
            return S_OK;
        }
        break;

    default:
        break;
    }

    return S_FALSE;
}

static HRESULT Rowset_ConvertValue(MonetCellValue* cell, const MonetColumnInfo* col, const DBBINDING* binding, void* base, DBSTATUS* pstatus)
{
    BYTE* value_ptr = (BYTE*)Rowset_BindingPointer(base, binding->obValue);
    DBTYPE base_type = (DBTYPE)(binding->wType & ~DBTYPE_BYREF);
    BOOL is_byref = ((binding->wType & DBTYPE_BYREF) != 0);
    DBSTATUS status = DBSTATUS_S_OK;
    HRESULT hr = S_FALSE;

    MONET_UNUSED(col);
    if (cell->is_null)
    {
        Rowset_SetLength(binding, base, 0);
        *pstatus = DBSTATUS_S_ISNULL;
        return S_OK;
    }

    hr = Rowset_ConvertNativeValue(cell, col, binding, base, pstatus);
    if (hr != S_FALSE)
    {
        return hr;
    }

    if (cell->storage_c_type != 0 && !cell->text)
    {
        hr = Rowset_EnsureAnsiCache(cell, col);
        if (FAILED(hr))
        {
            Rowset_LogConvertFailure(col, binding, cell);
            *pstatus = DBSTATUS_E_CANTCONVERTVALUE;
            return DB_E_ERRORSOCCURRED;
        }
    }

    if (is_byref)
    {
        switch (base_type)
        {
        case DBTYPE_STR:
            *((CHAR**)value_ptr) = cell->text ? cell->text : "";
            Rowset_SetLength(binding, base, cell->length);
            *pstatus = DBSTATUS_S_OK;
            return S_OK;

        case DBTYPE_WSTR:
            hr = Rowset_EnsureWideCache(cell, col);
            if (FAILED(hr))
            {
                Rowset_LogConvertFailure(col, binding, cell);
                *pstatus = DBSTATUS_E_CANTCONVERTVALUE;
                return DB_E_ERRORSOCCURRED;
            }
            *((WCHAR**)value_ptr) = cell->wide_text;
            Rowset_SetLength(binding, base, cell->wide_length);
            *pstatus = DBSTATUS_S_OK;
            return S_OK;

        case DBTYPE_BYTES:
            *((BYTE**)value_ptr) = (BYTE*)cell->text;
            Rowset_SetLength(binding, base, cell->length);
            *pstatus = DBSTATUS_S_OK;
            return S_OK;

        default:
            Rowset_LogConvertFailure(col, binding, cell);
            *pstatus = DBSTATUS_E_CANTCONVERTVALUE;
            return DB_E_ERRORSOCCURRED;
        }
    }

    switch (base_type)
    {
    case DBTYPE_STR:
        Rowset_SetLength(binding, base, cell->length);
        return Rowset_CopyAnsi(cell->text, cell->length, value_ptr, binding->cbMaxLen, pstatus);

    case DBTYPE_WSTR:
        hr = Rowset_EnsureWideCache(cell, col);
        if (FAILED(hr))
        {
            Rowset_LogConvertFailure(col, binding, cell);
            *pstatus = DBSTATUS_E_CANTCONVERTVALUE;
            return DB_E_ERRORSOCCURRED;
        }
        Rowset_SetLength(binding, base, cell->wide_length);
        return Rowset_CopyWideBuffer(cell->wide_text, cell->wide_length, (WCHAR*)value_ptr, binding->cbMaxLen, pstatus);

    case DBTYPE_I2:
        *((SHORT*)value_ptr) = (SHORT)atoi(cell->text);
        Rowset_SetLength(binding, base, sizeof(SHORT));
        *pstatus = DBSTATUS_S_OK;
        return S_OK;

    case DBTYPE_UI2:
        *((USHORT*)value_ptr) = (USHORT)strtoul(cell->text, NULL, 10);
        Rowset_SetLength(binding, base, sizeof(USHORT));
        *pstatus = DBSTATUS_S_OK;
        return S_OK;

    case DBTYPE_I4:
        *((LONG*)value_ptr) = atol(cell->text);
        Rowset_SetLength(binding, base, sizeof(LONG));
        *pstatus = DBSTATUS_S_OK;
        return S_OK;

    case DBTYPE_UI4:
        *((ULONG*)value_ptr) = strtoul(cell->text, NULL, 10);
        Rowset_SetLength(binding, base, sizeof(ULONG));
        *pstatus = DBSTATUS_S_OK;
        return S_OK;

    case DBTYPE_I8:
        *((LONGLONG*)value_ptr) = _atoi64(cell->text);
        Rowset_SetLength(binding, base, sizeof(LONGLONG));
        *pstatus = DBSTATUS_S_OK;
        return S_OK;

    case DBTYPE_UI8:
        *((ULONGLONG*)value_ptr) = _strtoui64(cell->text, NULL, 10);
        Rowset_SetLength(binding, base, sizeof(ULONGLONG));
        *pstatus = DBSTATUS_S_OK;
        return S_OK;

    case DBTYPE_UI1:
        *((BYTE*)value_ptr) = (BYTE)atoi(cell->text);
        Rowset_SetLength(binding, base, sizeof(BYTE));
        *pstatus = DBSTATUS_S_OK;
        return S_OK;

    case DBTYPE_R4:
        *((FLOAT*)value_ptr) = (FLOAT)atof(cell->text);
        Rowset_SetLength(binding, base, sizeof(FLOAT));
        *pstatus = DBSTATUS_S_OK;
        return S_OK;

    case DBTYPE_R8:
        *((DOUBLE*)value_ptr) = atof(cell->text);
        Rowset_SetLength(binding, base, sizeof(DOUBLE));
        *pstatus = DBSTATUS_S_OK;
        return S_OK;

    case DBTYPE_BOOL:
    {
        VARIANT_BOOL flag = VARIANT_FALSE;
        if (!Rowset_ParseBool(cell->text, &flag))
        {
            *pstatus = DBSTATUS_E_CANTCONVERTVALUE;
            return DB_E_ERRORSOCCURRED;
        }
        *((VARIANT_BOOL*)value_ptr) = flag;
        Rowset_SetLength(binding, base, sizeof(VARIANT_BOOL));
        *pstatus = DBSTATUS_S_OK;
        return S_OK;
    }

    case DBTYPE_NUMERIC:
        Rowset_SetLength(binding, base, sizeof(DB_NUMERIC));
        if (FAILED(Rowset_CopyNumeric(cell->text, col ? col->precision : 38, col ? col->scale : 0, (DB_NUMERIC*)value_ptr)))
        {
            Rowset_LogConvertFailure(col, binding, cell);
            *pstatus = DBSTATUS_E_CANTCONVERTVALUE;
            return DB_E_ERRORSOCCURRED;
        }
        *pstatus = DBSTATUS_S_OK;
        return S_OK;

    case DBTYPE_DATE:
        Rowset_SetLength(binding, base, sizeof(DATE));
        if (FAILED(Rowset_ParseVariantDate(cell->text, (DATE*)value_ptr)))
        {
            Rowset_LogConvertFailure(col, binding, cell);
            *pstatus = DBSTATUS_E_CANTCONVERTVALUE;
            return DB_E_ERRORSOCCURRED;
        }
        *pstatus = DBSTATUS_S_OK;
        return S_OK;

    case DBTYPE_GUID:
        Rowset_SetLength(binding, base, sizeof(GUID));
        if (FAILED(Rowset_ParseGuid(cell->text, (GUID*)value_ptr)))
        {
            Rowset_LogConvertFailure(col, binding, cell);
            *pstatus = DBSTATUS_E_CANTCONVERTVALUE;
            return DB_E_ERRORSOCCURRED;
        }
        *pstatus = DBSTATUS_S_OK;
        return S_OK;

    case DBTYPE_DBDATE:
        Rowset_SetLength(binding, base, sizeof(DBDATE));
        if (FAILED(Rowset_ParseDate(cell->text, (DBDATE*)value_ptr)))
        {
            Rowset_LogConvertFailure(col, binding, cell);
            *pstatus = DBSTATUS_E_CANTCONVERTVALUE;
            return DB_E_ERRORSOCCURRED;
        }
        *pstatus = DBSTATUS_S_OK;
        return S_OK;

    case DBTYPE_DBTIME:
        Rowset_SetLength(binding, base, sizeof(DBTIME));
        if (FAILED(Rowset_ParseTime(cell->text, (DBTIME*)value_ptr)))
        {
            Rowset_LogConvertFailure(col, binding, cell);
            *pstatus = DBSTATUS_E_CANTCONVERTVALUE;
            return DB_E_ERRORSOCCURRED;
        }
        *pstatus = DBSTATUS_S_OK;
        return S_OK;

    case DBTYPE_DBTIMESTAMP:
        Rowset_SetLength(binding, base, sizeof(DBTIMESTAMP));
        if (FAILED(Rowset_ParseTimestamp(cell->text, (DBTIMESTAMP*)value_ptr)))
        {
            Rowset_LogConvertFailure(col, binding, cell);
            *pstatus = DBSTATUS_E_CANTCONVERTVALUE;
            return DB_E_ERRORSOCCURRED;
        }
        *pstatus = DBSTATUS_S_OK;
        return S_OK;

    case DBTYPE_BYTES:
        Rowset_SetLength(binding, base, cell->length);
        if (binding->cbMaxLen < cell->length)
        {
            memcpy(value_ptr, cell->text, binding->cbMaxLen);
            *pstatus = DBSTATUS_S_TRUNCATED;
        }
        else
        {
            memcpy(value_ptr, cell->text, cell->length);
            *pstatus = DBSTATUS_S_OK;
        }
        return S_OK;

    default:
        Rowset_LogConvertFailure(col, binding, cell);
        status = DBSTATUS_E_CANTCONVERTVALUE;
        *pstatus = status;
        return DB_E_ERRORSOCCURRED;
    }
}

static void Rowset_AppendOutstanding(MonetRowset* self, MonetBufferedRow* row)
{
    row->next = self->outstanding_rows;
    self->outstanding_rows = row;
}

static void Rowset_DetachOutstanding(MonetRowset* self, MonetBufferedRow* row)
{
    MonetBufferedRow** current = &self->outstanding_rows;
    while (*current)
    {
        if (*current == row)
        {
            *current = row->next;
            row->next = NULL;
            return;
        }
        current = &(*current)->next;
    }
}

static void Rowset_FreeList(MonetBufferedRow* row)
{
    MonetBufferedRow* next;
    while (row)
    {
        next = row->next;
        Rowset_FreeBufferedRow(row);
        row = next;
    }
}

static HRESULT Rowset_CreateInsertedRowHandle(MonetRowset* self, HROW* ph_row)
{
    MonetBufferedRow* row = NULL;

    if (!ph_row)
    {
        return S_OK;
    }

    *ph_row = DB_NULL_HROW;
    row = (MonetBufferedRow*)CoTaskMemAlloc(sizeof(*row));
    if (!row)
    {
        return E_OUTOFMEMORY;
    }
    ZeroMemory(row, sizeof(*row));
    row->ref_count = 1;
    row->column_count = self ? self->column_count : 0;

    Rowset_AppendOutstanding(self, row);
    *ph_row = (HROW)row;
    MONET_TRACE("Rowset::InsertRow", "returned inserted HROW=%p", (void*)row);
    return S_OK;
}

static SIZE_T Rowset_GetFetchRowTarget(const MonetRowset* self)
{
    LONG value = MONETDB_DEFAULT_FETCH_ROWS;

    if (self && self->session && self->session->datasource)
    {
        value = self->session->datasource->config.fetch_rows;
    }
    if (value < MONETDB_MIN_FETCH_ROWS)
    {
        value = MONETDB_DEFAULT_FETCH_ROWS;
    }
    else if (value > MONETDB_MAX_FETCH_ROWS)
    {
        value = MONETDB_MAX_FETCH_ROWS;
    }

    return (SIZE_T)value;
}

static SIZE_T Rowset_GetFetchWindowBytes(const MonetRowset* self)
{
    LONG value_kb = MONETDB_DEFAULT_FETCH_WINDOW_KB;

    if (self && self->session && self->session->datasource)
    {
        value_kb = self->session->datasource->config.fetch_window_kb;
    }
    if (value_kb < MONETDB_MIN_FETCH_WINDOW_KB)
    {
        value_kb = MONETDB_DEFAULT_FETCH_WINDOW_KB;
    }
    else if (value_kb > MONETDB_MAX_FETCH_WINDOW_KB)
    {
        value_kb = MONETDB_MAX_FETCH_WINDOW_KB;
    }

    return ((SIZE_T)value_kb) * 1024U;
}

static size_t Rowset_AlignBufferCapacity(size_t requested, SIZE_T window_bytes)
{
    size_t aligned = requested;
    size_t chunk = (size_t)MONETDB_READ_CHUNK_BYTES;

    if (aligned < chunk)
    {
        aligned = chunk;
    }
    else
    {
        size_t remainder = aligned % chunk;
        if (remainder != 0)
        {
            aligned += chunk - remainder;
        }
    }

    if (window_bytes > chunk && requested <= window_bytes && aligned > window_bytes)
    {
        aligned = (size_t)window_bytes;
    }
    if (aligned < requested)
    {
        aligned = requested;
    }

    return aligned;
}

static void Rowset_ClearBufferedState(MonetRowset* self)
{
    if (!self)
    {
        return;
    }

    Rowset_FreeList(self->available_head);
    Rowset_FreeList(self->outstanding_rows);
    self->available_head = NULL;
    self->available_tail = NULL;
    self->outstanding_rows = NULL;
    self->end_of_rowset = FALSE;
    self->fetched_row_count = 0;
    self->trace_row_limit_logged = FALSE;
}

static HRESULT Rowset_RestartCommandRowset(MonetRowset* self)
{
    SQLHSTMT hstmt = SQL_NULL_HSTMT;
    MonetColumnInfo* columns = NULL;
    DBORDINAL column_count = 0;
    HRESULT hr;

    if (!self || !self->command || !self->command->sql_text[0])
    {
        return DB_E_CANTSCROLLBACKWARDS;
    }

    if (self->outstanding_rows)
    {
        MONET_TRACE("Rowset::RestartPosition", "%s", "outstanding rows still referenced, cannot restart");
        return DB_E_ROWSNOTRELEASED;
    }

    hr = Odbc_ExecDirectA(self->session->datasource->hdbc, self->command->sql_text, &hstmt);
    if (FAILED(hr))
    {
        return hr;
    }

    hr = Odbc_DescribeColumns(hstmt, &columns, &column_count);
    if (FAILED(hr))
    {
        Odbc_CloseStatement(&hstmt);
        return hr;
    }

    Rowset_ClearBufferedState(self);
    Odbc_CloseStatement(&self->hstmt);
    if (self->columns)
    {
        CoTaskMemFree(self->columns);
    }

    self->hstmt = hstmt;
    self->columns = columns;
    self->column_count = column_count;
    return S_OK;
}

static HRESULT Rowset_ReadCellText(MonetRowset* self, SQLHSTMT hstmt, SQLUSMALLINT column, MonetCellValue* cell)
{
    SQLRETURN rc;
    SQLLEN indicator = 0;
    CHAR chunk[MONETDB_READ_CHUNK_BYTES];
    CHAR* buffer = NULL;
    size_t used = 0;
    size_t capacity = 0;
    SIZE_T window_bytes = Rowset_GetFetchWindowBytes(self);

    ZeroMemory(cell, sizeof(*cell));
    for (;;)
    {
        rc = SQLGetData(hstmt, column, SQL_C_CHAR, chunk, sizeof(chunk), &indicator);
        if (indicator == SQL_NULL_DATA)
        {
            cell->is_null = TRUE;
            cell->length = 0;
            cell->text = NULL;
            return S_OK;
        }

        if (rc == SQL_NO_DATA)
        {
            break;
        }
        if (!(SQL_SUCCEEDED(rc) || rc == SQL_SUCCESS_WITH_INFO))
        {
            CoTaskMemFree(buffer);
            return DB_E_ERRORSINCOMMAND;
        }

        {
            size_t part_len = strnlen(chunk, MONET_ARRAY_SIZE(chunk) - 1);
            size_t new_used = used + part_len;
            if (new_used + 1 > capacity)
            {
                size_t hinted_capacity = 0;
                size_t new_capacity = 0;
                CHAR* new_buffer = NULL;

                if (indicator > 0 && indicator != SQL_NO_TOTAL)
                {
                    hinted_capacity = (size_t)indicator + 1;
                }
                new_capacity = (capacity == 0)
                    ? (hinted_capacity ? hinted_capacity : (size_t)MONETDB_READ_CHUNK_BYTES)
                    : capacity * 2;
                if (new_capacity < new_used + 1)
                {
                    new_capacity = new_used + 1;
                }
                if (new_capacity < hinted_capacity)
                {
                    new_capacity = hinted_capacity;
                }
                new_capacity = Rowset_AlignBufferCapacity(new_capacity, window_bytes);
                new_buffer = (CHAR*)CoTaskMemRealloc(buffer, new_capacity);
                if (!new_buffer)
                {
                    if (buffer)
                    {
                        CoTaskMemFree(buffer);
                    }
                    return E_OUTOFMEMORY;
                }
                buffer = new_buffer;
                capacity = new_capacity;
            }
            memcpy(buffer + used, chunk, part_len);
            used = new_used;
            buffer[used] = '\0';
        }

        if (rc == SQL_SUCCESS)
        {
            break;
        }
    }

    if (!buffer)
    {
        buffer = (CHAR*)CoTaskMemAlloc(1);
        if (!buffer)
        {
            return E_OUTOFMEMORY;
        }
        buffer[0] = '\0';
    }

    cell->is_null = FALSE;
    cell->length = (DBLENGTH)used;
    cell->text = buffer;
    return S_OK;
}

static HRESULT Rowset_ReadCellBoundedAnsi(SQLHSTMT hstmt, SQLUSMALLINT column, SQLLEN initial_buffer_size, BOOL fixed_width, MonetCellValue* cell)
{
    SQLRETURN rc;
    SQLLEN indicator = 0;
    CHAR* buffer = NULL;
    size_t capacity = 0;
    size_t used = 0;
    CHAR chunk[MONETDB_READ_CHUNK_BYTES];

    if (!cell || initial_buffer_size <= 0)
    {
        return E_POINTER;
    }

    capacity = (size_t)initial_buffer_size;
    buffer = (CHAR*)CoTaskMemAlloc(capacity);
    if (!buffer)
    {
        return E_OUTOFMEMORY;
    }

    rc = SQLGetData(hstmt, column, SQL_C_CHAR, buffer, initial_buffer_size, &indicator);
    if (indicator == SQL_NULL_DATA)
    {
        CoTaskMemFree(buffer);
        cell->is_null = TRUE;
        return S_OK;
    }
    if (!(SQL_SUCCEEDED(rc) || rc == SQL_SUCCESS_WITH_INFO))
    {
        CoTaskMemFree(buffer);
        return DB_E_ERRORSINCOMMAND;
    }

    if (fixed_width && rc == SQL_SUCCESS && indicator >= 0 && indicator != SQL_NO_TOTAL && (size_t)indicator + 1U <= capacity)
    {
        used = (size_t)indicator;
        buffer[used] = '\0';
        cell->is_null = FALSE;
        cell->storage_c_type = SQL_C_CHAR;
        cell->text = buffer;
        cell->length = (DBLENGTH)used;
        return S_OK;
    }

    used = strnlen(buffer, capacity ? capacity - 1U : 0U);
    while (rc == SQL_SUCCESS_WITH_INFO)
    {
        size_t part_len = 0;
        size_t new_capacity = 0;
        CHAR* new_buffer = NULL;

        rc = SQLGetData(hstmt, column, SQL_C_CHAR, chunk, sizeof(chunk), &indicator);
        if (rc == SQL_NO_DATA)
        {
            break;
        }
        if (!(SQL_SUCCEEDED(rc) || rc == SQL_SUCCESS_WITH_INFO))
        {
            CoTaskMemFree(buffer);
            return DB_E_ERRORSINCOMMAND;
        }

        part_len = strnlen(chunk, MONET_ARRAY_SIZE(chunk) - 1);
        if (used + part_len + 1U > capacity)
        {
            new_capacity = Rowset_AlignBufferCapacity(used + part_len + 1U, used + part_len + 1U);
            new_buffer = (CHAR*)CoTaskMemRealloc(buffer, new_capacity);
            if (!new_buffer)
            {
                CoTaskMemFree(buffer);
                return E_OUTOFMEMORY;
            }
            buffer = new_buffer;
            capacity = new_capacity;
        }

        memcpy(buffer + used, chunk, part_len);
        used += part_len;
        buffer[used] = '\0';
    }

    cell->is_null = FALSE;
    cell->storage_c_type = SQL_C_CHAR;
    cell->text = buffer;
    cell->length = (DBLENGTH)used;
    return S_OK;
}

static HRESULT Rowset_ReadCellBoundedWide(SQLHSTMT hstmt, SQLUSMALLINT column, SQLLEN initial_buffer_size, BOOL fixed_width, MonetCellValue* cell)
{
    SQLRETURN rc;
    SQLLEN indicator = 0;
    WCHAR* buffer = NULL;
    size_t capacity_bytes = 0;
    size_t used_bytes = 0;
    WCHAR chunk[MONETDB_READ_CHUNK_BYTES / sizeof(WCHAR)];

    if (!cell || initial_buffer_size <= 0)
    {
        return E_POINTER;
    }

    capacity_bytes = (size_t)initial_buffer_size;
    buffer = (WCHAR*)CoTaskMemAlloc(capacity_bytes);
    if (!buffer)
    {
        return E_OUTOFMEMORY;
    }

    rc = SQLGetData(hstmt, column, SQL_C_WCHAR, buffer, initial_buffer_size, &indicator);
    if (indicator == SQL_NULL_DATA)
    {
        CoTaskMemFree(buffer);
        cell->is_null = TRUE;
        return S_OK;
    }
    if (!(SQL_SUCCEEDED(rc) || rc == SQL_SUCCESS_WITH_INFO))
    {
        CoTaskMemFree(buffer);
        return DB_E_ERRORSINCOMMAND;
    }

    if (fixed_width && rc == SQL_SUCCESS && indicator >= 0 && indicator != SQL_NO_TOTAL && (size_t)indicator + sizeof(WCHAR) <= capacity_bytes)
    {
        used_bytes = (size_t)indicator;
        buffer[used_bytes / sizeof(WCHAR)] = L'\0';
        cell->is_null = FALSE;
        cell->storage_c_type = SQL_C_WCHAR;
        cell->wide_text = buffer;
        cell->wide_length = (DBLENGTH)used_bytes;
        return S_OK;
    }

    used_bytes = (size_t)(wcsnlen(buffer, (capacity_bytes / sizeof(WCHAR)) ? (capacity_bytes / sizeof(WCHAR)) - 1U : 0U) * sizeof(WCHAR));
    while (rc == SQL_SUCCESS_WITH_INFO)
    {
        size_t part_bytes = 0;
        size_t new_capacity_bytes = 0;
        WCHAR* new_buffer = NULL;

        rc = SQLGetData(hstmt, column, SQL_C_WCHAR, chunk, sizeof(chunk), &indicator);
        if (rc == SQL_NO_DATA)
        {
            break;
        }
        if (!(SQL_SUCCEEDED(rc) || rc == SQL_SUCCESS_WITH_INFO))
        {
            CoTaskMemFree(buffer);
            return DB_E_ERRORSINCOMMAND;
        }

        part_bytes = (size_t)(wcsnlen(chunk, MONET_ARRAY_SIZE(chunk) - 1U) * sizeof(WCHAR));
        if (used_bytes + part_bytes + sizeof(WCHAR) > capacity_bytes)
        {
            new_capacity_bytes = Rowset_AlignBufferCapacity(used_bytes + part_bytes + sizeof(WCHAR), used_bytes + part_bytes + sizeof(WCHAR));
            new_buffer = (WCHAR*)CoTaskMemRealloc(buffer, new_capacity_bytes);
            if (!new_buffer)
            {
                CoTaskMemFree(buffer);
                return E_OUTOFMEMORY;
            }
            buffer = new_buffer;
            capacity_bytes = new_capacity_bytes;
        }

        memcpy(((BYTE*)buffer) + used_bytes, chunk, part_bytes);
        used_bytes += part_bytes;
        buffer[used_bytes / sizeof(WCHAR)] = L'\0';
    }

    cell->is_null = FALSE;
    cell->storage_c_type = SQL_C_WCHAR;
    cell->wide_text = buffer;
    cell->wide_length = (DBLENGTH)used_bytes;
    return S_OK;
}

static HRESULT Rowset_ReadCellTyped(MonetRowset* self, SQLHSTMT hstmt, SQLUSMALLINT column, const MonetColumnInfo* col, MonetCellValue* cell)
{
    SQLRETURN rc;
    SQLLEN indicator = 0;

    MONET_UNUSED(self);
    if (!col || !cell || col->fetch_c_type == 0 || col->fetch_buffer_size <= 0)
    {
        return S_FALSE;
    }

    if (col->fetch_c_type == SQL_C_CHAR)
    {
        return Rowset_ReadCellBoundedAnsi(hstmt, column, col->fetch_buffer_size, col->sql_type == SQL_CHAR, cell);
    }
    if (col->fetch_c_type == SQL_C_WCHAR)
    {
        return Rowset_ReadCellBoundedWide(hstmt, column, col->fetch_buffer_size, col->sql_type == SQL_WCHAR, cell);
    }

    ZeroMemory(cell, sizeof(*cell));
    rc = SQLGetData(hstmt, column, col->fetch_c_type, &cell->native, col->fetch_buffer_size, &indicator);
    if (indicator == SQL_NULL_DATA)
    {
        cell->is_null = TRUE;
        return S_OK;
    }
    if (!SQL_SUCCEEDED(rc))
    {
        return DB_E_ERRORSINCOMMAND;
    }

    cell->is_null = FALSE;
    cell->storage_c_type = col->fetch_c_type;
    cell->native_length = (indicator > 0 && indicator != SQL_NO_TOTAL) ? (DBLENGTH)indicator : (DBLENGTH)col->fetch_buffer_size;
    return S_OK;
}

static HRESULT Rowset_ReadCell(MonetRowset* self, SQLHSTMT hstmt, const MonetColumnInfo* col, SQLUSMALLINT column, MonetCellValue* cell)
{
    HRESULT hr = Rowset_ReadCellTyped(self, hstmt, column, col, cell);

    if (hr == S_FALSE)
    {
        return Rowset_ReadCellText(self, hstmt, column, cell);
    }

    return hr;
}

static void Rowset_LogFetchedRow(MonetRowset* self, MonetBufferedRow* row)
{
    CHAR line[3584];
    size_t used = 0;
    DBORDINAL i;
    DBORDINAL max_columns;
    int written = 0;

    if (!Config_IsTraceEnabled() || !self || !row || !self->columns)
    {
        return;
    }

    if (self->fetched_row_count >= MONETDB_TRACE_RESULT_ROWS)
    {
        if (!self->trace_row_limit_logged)
        {
            self->trace_row_limit_logged = TRUE;
            Log_WriteResultA("Rowset::Fetch", "row trace limit reached after %u rows", (unsigned)MONETDB_TRACE_RESULT_ROWS);
        }
        return;
    }

    ++self->fetched_row_count;
    max_columns = (row->column_count > 12) ? 12 : row->column_count;
    written = _snprintf(
        line,
        MONET_ARRAY_SIZE(line),
        "row[%llu] cols=%lu",
        (unsigned long long)self->fetched_row_count,
        (unsigned long)row->column_count);
    if (written < 0)
    {
        used = MONET_ARRAY_SIZE(line) - 1;
    }
    else
    {
        used = (size_t)written;
    }
    line[MONET_ARRAY_SIZE(line) - 1] = '\0';

    for (i = 0; i < max_columns && used + 32 < MONET_ARRAY_SIZE(line); ++i)
    {
        MonetCellValue* cell = &row->cells[i];
        const CHAR* name = self->columns[i].name_a[0] ? self->columns[i].name_a : "?";
        CHAR value[96];
        size_t j = 0;

        if (cell->is_null)
        {
            Monet_StringCopyA(value, MONET_ARRAY_SIZE(value), "<NULL>");
        }
        else
        {
            if (!cell->text && FAILED(Rowset_EnsureAnsiCache(cell, &self->columns[i])))
            {
                Monet_StringCopyA(value, MONET_ARRAY_SIZE(value), "<FORMAT-ERR>");
                j = strlen(value);
            }
            else
            {
                for (j = 0; j + 1 < MONET_ARRAY_SIZE(value) && j < (size_t)cell->length; ++j)
                {
                    CHAR ch = cell->text[j];
                    if (ch == '\r' || ch == '\n' || ch == '\t')
                    {
                        ch = ' ';
                    }
                    value[j] = ch;
                }
                value[j] = '\0';
                if ((size_t)cell->length > j && j + 4 < MONET_ARRAY_SIZE(value))
                {
                    strcat(value, "...");
                }
            }
        }

        written = _snprintf(
            line + used,
            MONET_ARRAY_SIZE(line) - used,
            "%s%s='%s'(%ld)",
            (i == 0) ? " " : ", ",
            name,
            value,
            (long)(cell->is_null ? 0 : cell->length));
        if (written < 0)
        {
            used = MONET_ARRAY_SIZE(line) - 1;
            break;
        }
        used += (size_t)written;
    }

    if (row->column_count > max_columns && used + 16 < MONET_ARRAY_SIZE(line))
    {
        _snprintf(
            line + used,
            MONET_ARRAY_SIZE(line) - used,
            ", ...");
        line[MONET_ARRAY_SIZE(line) - 1] = '\0';
    }

    Log_WriteResultA("Rowset::Fetch", "%s", line);
}

static HRESULT Rowset_Prefetch(MonetRowset* self)
{
    SQLRETURN rc;
    ULONG count = 0;
    SIZE_T window_used = 0;
    ULONGLONG typed_cells = 0;
    ULONGLONG text_cells = 0;
    SIZE_T max_rows = Rowset_GetFetchRowTarget(self);
    SIZE_T max_window_bytes = Rowset_GetFetchWindowBytes(self);
    LARGE_INTEGER perf_start = { 0 };
    LARGE_INTEGER perf_end = { 0 };
    LARGE_INTEGER perf_freq = { 0 };
    BOOL have_perf = FALSE;
    ULONGLONG elapsed_us = 0;

    if (self->end_of_rowset)
    {
        return DB_S_ENDOFROWSET;
    }

    if (QueryPerformanceFrequency(&perf_freq))
    {
        QueryPerformanceCounter(&perf_start);
        have_perf = TRUE;
    }

    while ((SIZE_T)count < max_rows && window_used < max_window_bytes)
    {
        MonetBufferedRow* row = NULL;
        DBORDINAL i;
        SIZE_T row_bytes = sizeof(*row);

        rc = SQLFetch(self->hstmt);
        if (rc == SQL_NO_DATA)
        {
            self->end_of_rowset = TRUE;
            break;
        }
        if (!(SQL_SUCCEEDED(rc) || rc == SQL_SUCCESS_WITH_INFO))
        {
            return DB_E_ERRORSINCOMMAND;
        }

        row = (MonetBufferedRow*)CoTaskMemAlloc(sizeof(*row));
        if (!row)
        {
            return E_OUTOFMEMORY;
        }
        ZeroMemory(row, sizeof(*row));
        row->column_count = self->column_count;
        row->cells = (MonetCellValue*)CoTaskMemAlloc(sizeof(MonetCellValue) * self->column_count);
        if (!row->cells)
        {
            CoTaskMemFree(row);
            return E_OUTOFMEMORY;
        }
        ZeroMemory(row->cells, sizeof(MonetCellValue) * self->column_count);
        row_bytes += sizeof(MonetCellValue) * self->column_count;

        for (i = 0; i < self->column_count; ++i)
        {
            HRESULT hr = Rowset_ReadCell(self, self->hstmt, &self->columns[i], (SQLUSMALLINT)(i + 1), &row->cells[i]);
            if (FAILED(hr))
            {
                Rowset_FreeBufferedRow(row);
                return hr;
            }
            if (!row->cells[i].is_null)
            {
                if (row->cells[i].storage_c_type != 0)
                {
                    ++typed_cells;
                    if (row->cells[i].storage_c_type == SQL_C_CHAR)
                    {
                        row_bytes += (SIZE_T)row->cells[i].length + 1U;
                    }
                    else if (row->cells[i].storage_c_type == SQL_C_WCHAR)
                    {
                        row_bytes += (SIZE_T)row->cells[i].wide_length + sizeof(WCHAR);
                    }
                }
                else
                {
                    ++text_cells;
                    row_bytes += (SIZE_T)row->cells[i].length + 1U;
                }
            }
        }

        Rowset_LogFetchedRow(self, row);

        if (self->available_tail)
        {
            self->available_tail->next = row;
        }
        else
        {
            self->available_head = row;
        }
        self->available_tail = row;
        ++count;
        window_used += row_bytes;
    }

    if (have_perf)
    {
        QueryPerformanceCounter(&perf_end);
        elapsed_us = (ULONGLONG)((perf_end.QuadPart - perf_start.QuadPart) * 1000000ULL / perf_freq.QuadPart);
    }

    MONET_TRACE(
        "Rowset::Prefetch",
        "rows=%lu window_bytes=%llu limit_rows=%llu limit_window=%llu typed_cells=%llu text_cells=%llu elapsed_us=%llu avg_row_bytes=%llu rows_per_sec=%llu end=%d",
        (unsigned long)count,
        (unsigned long long)window_used,
        (unsigned long long)max_rows,
        (unsigned long long)max_window_bytes,
        (unsigned long long)typed_cells,
        (unsigned long long)text_cells,
        (unsigned long long)elapsed_us,
        count ? (unsigned long long)(window_used / count) : 0ULL,
        elapsed_us ? (unsigned long long)((count * 1000000ULL) / elapsed_us) : 0ULL,
        self->end_of_rowset ? 1 : 0);

    return count > 0 ? S_OK : DB_S_ENDOFROWSET;
}

void Rowset_FreeBufferedRow(MonetBufferedRow* row)
{
    DBORDINAL i;
    if (!row)
    {
        return;
    }

    if (row->cells)
    {
        for (i = 0; i < row->column_count; ++i)
        {
            if (row->cells[i].text)
            {
                CoTaskMemFree(row->cells[i].text);
            }
            if (row->cells[i].wide_text)
            {
                CoTaskMemFree(row->cells[i].wide_text);
            }
        }
        CoTaskMemFree(row->cells);
    }
    CoTaskMemFree(row);
}

HRESULT AccessorTable_Init(MonetAccessorTable* table)
{
    if (!table)
    {
        return E_POINTER;
    }
    ZeroMemory(table, sizeof(*table));
    InitializeCriticalSection(&table->lock);
    table->next_handle = 1;
    return S_OK;
}

void AccessorTable_Destroy(MonetAccessorTable* table)
{
    size_t i;
    if (!table)
    {
        return;
    }
    for (i = 0; i < table->count; ++i)
    {
        if (table->items[i])
        {
            CoTaskMemFree(table->items[i]->bindings);
            CoTaskMemFree(table->items[i]);
        }
    }
    CoTaskMemFree(table->items);
    DeleteCriticalSection(&table->lock);
}

MonetAccessor* AccessorTable_Find(MonetAccessorTable* table, HACCESSOR h_accessor)
{
    size_t i;
    if (!table)
    {
        return NULL;
    }
    for (i = 0; i < table->count; ++i)
    {
        if (table->items[i] && table->items[i]->handle == h_accessor)
        {
            return table->items[i];
        }
    }
    return NULL;
}

static HRESULT AccessorTable_Grow(MonetAccessorTable* table)
{
    size_t new_capacity = (table->capacity == 0) ? 8 : table->capacity * 2;
    MonetAccessor** new_items = (MonetAccessor**)CoTaskMemAlloc(sizeof(MonetAccessor*) * new_capacity);
    if (!new_items)
    {
        return E_OUTOFMEMORY;
    }
    ZeroMemory(new_items, sizeof(MonetAccessor*) * new_capacity);
    if (table->items)
    {
        memcpy(new_items, table->items, sizeof(MonetAccessor*) * table->count);
        CoTaskMemFree(table->items);
    }
    table->items = new_items;
    table->capacity = new_capacity;
    return S_OK;
}

HRESULT AccessorTable_Create(MonetAccessorTable* table, DBACCESSORFLAGS flags, DBCOUNTITEM binding_count, const DBBINDING bindings[], DBLENGTH row_size, HACCESSOR* ph_accessor, DBBINDSTATUS rg_status[])
{
    MonetAccessor* accessor = NULL;
    HRESULT hr;
    DBCOUNTITEM i;

    if (!table || !ph_accessor)
    {
        return E_POINTER;
    }
    if ((flags & DBACCESSOR_ROWDATA) == 0 || (flags & DBACCESSOR_PARAMETERDATA) != 0)
    {
        return DB_E_BADACCESSORFLAGS;
    }
    if (binding_count == 0 || !bindings)
    {
        return DB_E_NULLACCESSORNOTSUPPORTED;
    }

    MONET_TRACE(
        "Accessor::Create",
        "flags=0x%08lx bindings=%llu row_size=%llu",
        (unsigned long)flags,
        (unsigned long long)binding_count,
        (unsigned long long)row_size);
    for (i = 0; i < binding_count; ++i)
    {
        MONET_TRACE(
            "Accessor::Create",
            "binding[%llu] ordinal=%lu part=0x%08lx type=0x%04x obValue=%llu obLength=%llu obStatus=%llu cbMaxLen=%llu precision=%u scale=%u",
            (unsigned long long)i,
            (unsigned long)bindings[i].iOrdinal,
            (unsigned long)bindings[i].dwPart,
            (unsigned int)bindings[i].wType,
            (unsigned long long)bindings[i].obValue,
            (unsigned long long)bindings[i].obLength,
            (unsigned long long)bindings[i].obStatus,
            (unsigned long long)bindings[i].cbMaxLen,
            (unsigned int)bindings[i].bPrecision,
            (unsigned int)bindings[i].bScale);
    }

    accessor = (MonetAccessor*)CoTaskMemAlloc(sizeof(*accessor));
    if (!accessor)
    {
        return E_OUTOFMEMORY;
    }
    ZeroMemory(accessor, sizeof(*accessor));
    accessor->bindings = (DBBINDING*)CoTaskMemAlloc(sizeof(DBBINDING) * binding_count);
    if (!accessor->bindings)
    {
        CoTaskMemFree(accessor);
        return E_OUTOFMEMORY;
    }

    memcpy(accessor->bindings, bindings, sizeof(DBBINDING) * binding_count);
    accessor->binding_count = binding_count;
    accessor->flags = flags;
    accessor->row_size = row_size;
    accessor->ref_count = 1;

    EnterCriticalSection(&table->lock);
    if (table->count == table->capacity)
    {
        hr = AccessorTable_Grow(table);
        if (FAILED(hr))
        {
            LeaveCriticalSection(&table->lock);
            CoTaskMemFree(accessor->bindings);
            CoTaskMemFree(accessor);
            return hr;
        }
    }

    accessor->handle = (HACCESSOR)(ULONG_PTR)table->next_handle++;
    table->items[table->count++] = accessor;
    LeaveCriticalSection(&table->lock);

    if (rg_status)
    {
        for (i = 0; i < binding_count; ++i)
        {
            rg_status[i] = DBBINDSTATUS_OK;
        }
    }

    *ph_accessor = accessor->handle;
    return S_OK;
}

HRESULT AccessorTable_AddRef(MonetAccessorTable* table, HACCESSOR h_accessor, DBREFCOUNT* pc_refcount)
{
    MonetAccessor* accessor = AccessorTable_Find(table, h_accessor);
    if (!accessor)
    {
        return DB_E_BADACCESSORHANDLE;
    }
    accessor->ref_count++;
    if (pc_refcount)
    {
        *pc_refcount = accessor->ref_count;
    }
    return S_OK;
}

HRESULT AccessorTable_Release(MonetAccessorTable* table, HACCESSOR h_accessor, DBREFCOUNT* pc_refcount)
{
    size_t i;
    if (!table)
    {
        return E_POINTER;
    }

    EnterCriticalSection(&table->lock);
    for (i = 0; i < table->count; ++i)
    {
        if (table->items[i] && table->items[i]->handle == h_accessor)
        {
            MonetAccessor* accessor = table->items[i];
            if (accessor->ref_count > 0)
            {
                accessor->ref_count--;
            }
            if (pc_refcount)
            {
                *pc_refcount = accessor->ref_count;
            }
            if (accessor->ref_count == 0)
            {
                CoTaskMemFree(accessor->bindings);
                CoTaskMemFree(accessor);
                table->items[i] = table->items[table->count - 1];
                table->items[table->count - 1] = NULL;
                table->count--;
            }
            LeaveCriticalSection(&table->lock);
            return S_OK;
        }
    }
    LeaveCriticalSection(&table->lock);
    return DB_E_BADACCESSORHANDLE;
}

HRESULT AccessorTable_GetBindings(MonetAccessorTable* table, HACCESSOR h_accessor, DBACCESSORFLAGS* pflags, DBCOUNTITEM* pc_bindings, DBBINDING** pp_bindings)
{
    MonetAccessor* accessor = AccessorTable_Find(table, h_accessor);
    if (!accessor)
    {
        return DB_E_BADACCESSORHANDLE;
    }
    if (!pflags || !pc_bindings || !pp_bindings)
    {
        return E_POINTER;
    }
    *pflags = accessor->flags;
    *pc_bindings = accessor->binding_count;
    *pp_bindings = (DBBINDING*)CoTaskMemAlloc(sizeof(DBBINDING) * accessor->binding_count);
    if (!*pp_bindings)
    {
        return E_OUTOFMEMORY;
    }
    memcpy(*pp_bindings, accessor->bindings, sizeof(DBBINDING) * accessor->binding_count);
    return S_OK;
}

static HRESULT Rowset_QueryInterfaceInternal(MonetRowset* self, REFIID riid, void** ppv)
{
    if (!ppv)
    {
        return E_POINTER;
    }

    *ppv = NULL;
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IRowset))
    {
        *ppv = &self->IRowset_iface;
    }
    else if (IsEqualIID(riid, &IID_IRowsetChange))
    {
        *ppv = &self->IRowsetChange_iface;
    }
    else if (IsEqualIID(riid, &IID_IAccessor))
    {
        *ppv = &self->IAccessor_iface;
    }
    else if (IsEqualIID(riid, &IID_IColumnsInfo))
    {
        *ppv = &self->IColumnsInfo_iface;
    }
    else if (IsEqualIID(riid, &IID_IRowsetInfo))
    {
        *ppv = &self->IRowsetInfo_iface;
    }
    else if (IsEqualIID(riid, &IID_IConvertType))
    {
        *ppv = &self->IConvertType_iface;
    }
    else if (IsEqualIID(riid, &IID_ISupportErrorInfo))
    {
        *ppv = &self->ISupportErrorInfo_iface;
    }
    else
    {
        if (Config_IsTraceEnabled())
        {
            WCHAR wide[64];
            CHAR ansi[64];
            if (StringFromGUID2(riid, wide, (int)MONET_ARRAY_SIZE(wide)) > 0)
            {
                Monet_WideToAnsi(ansi, MONET_ARRAY_SIZE(ansi), wide);
                Log_WriteA(MONET_LOG_TRACE, "Rowset::QueryInterface", "IID non supportato full=%s", ansi);
            }
        }
        return E_NOINTERFACE;
    }

    InterlockedIncrement(&self->ref_count);
    if (Config_IsTraceEnabled())
    {
        WCHAR wide[64];
        CHAR ansi[64];
        if (StringFromGUID2(riid, wide, (int)MONET_ARRAY_SIZE(wide)) > 0)
        {
            Monet_WideToAnsi(ansi, MONET_ARRAY_SIZE(ansi), wide);
            Log_WriteA(MONET_LOG_TRACE, "Rowset::QueryInterface", "IID supportato full=%s ref=%ld", ansi, self->ref_count);
        }
    }
    return S_OK;
}

static ULONG Rowset_AddRefInternal(MonetRowset* self)
{
    return (ULONG)InterlockedIncrement(&self->ref_count);
}

static ULONG Rowset_ReleaseInternal(MonetRowset* self)
{
    ULONG ref = (ULONG)InterlockedDecrement(&self->ref_count);
    if (ref == 0)
    {
        HRESULT flush_hr = Rowset_FlushInsertBatch(self);
        if (FAILED(flush_hr))
        {
            Log_WriteA(MONET_LOG_ERROR, "Rowset::Release", "Flush batch insert fallito hr=0x%08lx", (unsigned long)flush_hr);
        }
        if (self->insert_batch_total_buffered || self->insert_batch_total_flushed)
        {
            Log_WriteA(
                MONET_LOG_INFO,
                "Rowset::Release",
                "insert summary table='%s' buffered=%llu inserted=%llu flushes=%llu pending=%llu",
                self->base_table,
                (unsigned long long)self->insert_batch_total_buffered,
                (unsigned long long)self->insert_batch_total_flushed,
                (unsigned long long)self->insert_batch_flush_count,
                (unsigned long long)(self->insert_batch_total_buffered - self->insert_batch_total_flushed));
        }
        Rowset_ClosePreparedInsert(self);
        Odbc_CloseStatement(&self->hstmt);
        AccessorTable_Destroy(&self->accessors);
        if (self->columns)
        {
            CoTaskMemFree(self->columns);
        }
        Rowset_FreeList(self->available_head);
        Rowset_FreeList(self->outstanding_rows);
        if (self->command)
        {
            self->command->ICommandText_iface.lpVtbl->Release(&self->command->ICommandText_iface);
        }
        if (self->session)
        {
            self->session->IOpenRowset_iface.lpVtbl->Release(&self->session->IOpenRowset_iface);
        }
        CoTaskMemFree(self);
        Monet_ObjectRelease();
    }
    return ref;
}

static HRESULT Rowset_QueryInterface_Rowset(IRowset* iface, REFIID riid, void** ppv) { return Rowset_QueryInterfaceInternal(Rowset_FromRowset(iface), riid, ppv); }
static ULONG Rowset_AddRef_Rowset(IRowset* iface) { return Rowset_AddRefInternal(Rowset_FromRowset(iface)); }
static ULONG Rowset_Release_Rowset(IRowset* iface) { return Rowset_ReleaseInternal(Rowset_FromRowset(iface)); }
static HRESULT Rowset_QueryInterface_RowsetChange(IRowsetChange* iface, REFIID riid, void** ppv) { return Rowset_QueryInterfaceInternal(Rowset_FromRowsetChange(iface), riid, ppv); }
static ULONG Rowset_AddRef_RowsetChange(IRowsetChange* iface) { return Rowset_AddRefInternal(Rowset_FromRowsetChange(iface)); }
static ULONG Rowset_Release_RowsetChange(IRowsetChange* iface) { return Rowset_ReleaseInternal(Rowset_FromRowsetChange(iface)); }
static HRESULT Rowset_QueryInterface_Accessor(IAccessor* iface, REFIID riid, void** ppv) { return Rowset_QueryInterfaceInternal(Rowset_FromAccessor(iface), riid, ppv); }
static ULONG Rowset_AddRef_Accessor(IAccessor* iface) { return Rowset_AddRefInternal(Rowset_FromAccessor(iface)); }
static ULONG Rowset_Release_Accessor(IAccessor* iface) { return Rowset_ReleaseInternal(Rowset_FromAccessor(iface)); }
static HRESULT Rowset_QueryInterface_Columns(IColumnsInfo* iface, REFIID riid, void** ppv) { return Rowset_QueryInterfaceInternal(Rowset_FromColumns(iface), riid, ppv); }
static ULONG Rowset_AddRef_Columns(IColumnsInfo* iface) { return Rowset_AddRefInternal(Rowset_FromColumns(iface)); }
static ULONG Rowset_Release_Columns(IColumnsInfo* iface) { return Rowset_ReleaseInternal(Rowset_FromColumns(iface)); }
static HRESULT Rowset_QueryInterface_Info(IRowsetInfo* iface, REFIID riid, void** ppv) { return Rowset_QueryInterfaceInternal(Rowset_FromInfo(iface), riid, ppv); }
static ULONG Rowset_AddRef_Info(IRowsetInfo* iface) { return Rowset_AddRefInternal(Rowset_FromInfo(iface)); }
static ULONG Rowset_Release_Info(IRowsetInfo* iface) { return Rowset_ReleaseInternal(Rowset_FromInfo(iface)); }
static HRESULT Rowset_QueryInterface_Convert(IConvertType* iface, REFIID riid, void** ppv) { return Rowset_QueryInterfaceInternal(Rowset_FromConvert(iface), riid, ppv); }
static ULONG Rowset_AddRef_Convert(IConvertType* iface) { return Rowset_AddRefInternal(Rowset_FromConvert(iface)); }
static ULONG Rowset_Release_Convert(IConvertType* iface) { return Rowset_ReleaseInternal(Rowset_FromConvert(iface)); }
static HRESULT Rowset_QueryInterface_SupportErrorInfo(ISupportErrorInfo* iface, REFIID riid, void** ppv) { return Rowset_QueryInterfaceInternal(Rowset_FromSupportErrorInfo(iface), riid, ppv); }
static ULONG Rowset_AddRef_SupportErrorInfo(ISupportErrorInfo* iface) { return Rowset_AddRefInternal(Rowset_FromSupportErrorInfo(iface)); }
static ULONG Rowset_Release_SupportErrorInfo(ISupportErrorInfo* iface) { return Rowset_ReleaseInternal(Rowset_FromSupportErrorInfo(iface)); }

static HRESULT STDMETHODCALLTYPE Rowset_InterfaceSupportsErrorInfo(ISupportErrorInfo* iface, REFIID riid)
{
    MonetRowset* self = Rowset_FromSupportErrorInfo(iface);
    MONET_UNUSED(self);

    if (IsEqualIID(riid, &IID_IRowset) ||
        IsEqualIID(riid, &IID_IRowsetChange) ||
        IsEqualIID(riid, &IID_IAccessor) ||
        IsEqualIID(riid, &IID_IColumnsInfo) ||
        IsEqualIID(riid, &IID_IRowsetInfo) ||
        IsEqualIID(riid, &IID_IConvertType))
    {
        return S_OK;
    }

    return S_FALSE;
}

static HRESULT Rowset_FillInsertedStubBindings(MonetAccessor* accessor, void* p_data)
{
    DBCOUNTITEM i;

    if (!accessor || !p_data)
    {
        return E_INVALIDARG;
    }

    for (i = 0; i < accessor->binding_count; ++i)
    {
        const DBBINDING* binding = &accessor->bindings[i];
        Rowset_SetLength(binding, p_data, 0);
        Rowset_SetStatus(binding, p_data, DBSTATUS_S_ISNULL);
    }
    return S_OK;
}

HRESULT Rowset_FillBindings(MonetRowset* rowset, MonetBufferedRow* row, MonetAccessor* accessor, void* p_data)
{
    DBCOUNTITEM i;
    HRESULT final_hr = S_OK;

    MONET_UNUSED(rowset);
    for (i = 0; i < accessor->binding_count; ++i)
    {
        const DBBINDING* binding = &accessor->bindings[i];
        DBSTATUS status = DBSTATUS_S_OK;

        if (binding->iOrdinal == 0 || binding->iOrdinal > row->column_count)
        {
            Rowset_SetStatus(binding, p_data, DBSTATUS_E_BADACCESSOR);
            Rowset_SetLength(binding, p_data, 0);
            final_hr = DB_S_ERRORSOCCURRED;
            continue;
        }

        if (!(binding->dwPart & DBPART_VALUE))
        {
            Rowset_SetStatus(binding, p_data, DBSTATUS_E_BADACCESSOR);
            Rowset_SetLength(binding, p_data, 0);
            final_hr = DB_S_ERRORSOCCURRED;
            continue;
        }

        if (FAILED(Rowset_ConvertValue(&row->cells[binding->iOrdinal - 1], &rowset->columns[binding->iOrdinal - 1], binding, p_data, &status)))
        {
            final_hr = DB_S_ERRORSOCCURRED;
        }
        Rowset_SetStatus(binding, p_data, status);
    }

    return final_hr;
}

static HRESULT Rowset_BuildColumnInfo(MonetRowset* self, DBORDINAL* pc_columns, DBCOLUMNINFO** prg_info, OLECHAR** pp_buffer)
{
    size_t buffer_chars = 1;
    DBCOLUMNINFO* info = NULL;
    OLECHAR* buffer = NULL;
    OLECHAR* cursor = NULL;
    DBORDINAL i;

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
        info[i].iOrdinal = self->columns[i].ordinal;
        info[i].dwFlags = self->columns[i].flags;
        info[i].ulColumnSize = self->columns[i].column_size;
        info[i].wType = self->columns[i].type;
        info[i].bPrecision = self->columns[i].precision;
        info[i].bScale = self->columns[i].scale;
        info[i].pwszName = cursor;
        wcscpy(cursor, self->columns[i].name_w);
        cursor += wcslen(self->columns[i].name_w) + 1;
        ZeroMemory(&info[i].columnid, sizeof(info[i].columnid));

        MONET_TRACE(
            "Rowset::GetColumnInfo",
            "column[%lu] name='%S' wType=0x%04x ulColumnSize=%ld precision=%u scale=%u flags=0x%08lx sql_type=%d sql_size=%llu",
            (unsigned long)i,
            self->columns[i].name_w,
            (unsigned)info[i].wType,
            (long)info[i].ulColumnSize,
            (unsigned)info[i].bPrecision,
            (unsigned)info[i].bScale,
            (unsigned long)info[i].dwFlags,
            (int)self->columns[i].sql_type,
            (unsigned long long)self->columns[i].sql_size);
    }

    *pc_columns = self->column_count;
    *prg_info = info;
    *pp_buffer = buffer;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE Rowset_AddRefRows(IRowset* iface, DBCOUNTITEM c_rows, const HROW rgh_rows[], DBREFCOUNT rg_ref_counts[], DBROWSTATUS rg_row_status[])
{
    DBCOUNTITEM i;
    MONET_UNUSED(iface);
    for (i = 0; i < c_rows; ++i)
    {
        MonetBufferedRow* row = (MonetBufferedRow*)rgh_rows[i];
        if (row)
        {
            row->ref_count++;
            if (rg_ref_counts) rg_ref_counts[i] = row->ref_count;
            if (rg_row_status) rg_row_status[i] = DBROWSTATUS_S_OK;
        }
        else
        {
            if (rg_ref_counts) rg_ref_counts[i] = 0;
            if (rg_row_status) rg_row_status[i] = DBROWSTATUS_E_INVALID;
        }
    }
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE Rowset_GetData(IRowset* iface, HROW h_row, HACCESSOR h_accessor, void* p_data)
{
    MonetRowset* self = Rowset_FromRowset(iface);
    MonetBufferedRow* row = (MonetBufferedRow*)h_row;
    MonetAccessor* accessor = NULL;

    if (!row || !p_data)
    {
        return E_INVALIDARG;
    }

    accessor = AccessorTable_Find(&self->accessors, h_accessor);
    if (!accessor)
    {
        return DB_E_BADACCESSORHANDLE;
    }

    ZeroMemory(p_data, accessor->row_size ? accessor->row_size : 1);
    if (!row->cells)
    {
        return Rowset_FillInsertedStubBindings(accessor, p_data);
    }
    return Rowset_FillBindings(self, row, accessor, p_data);
}

static HRESULT STDMETHODCALLTYPE Rowset_GetNextRows(IRowset* iface, HCHAPTER h_chapter, DBROWOFFSET l_rows_offset, DBROWCOUNT c_rows, DBCOUNTITEM* pc_rows_obtained, HROW** prgh_rows)
{
    MonetRowset* self = Rowset_FromRowset(iface);
    HROW* rows = NULL;
    DBCOUNTITEM obtained = 0;
    DBROWCOUNT wanted;
    DBROWOFFSET skip;

    if (!pc_rows_obtained || !prgh_rows)
    {
        return E_POINTER;
    }
    *pc_rows_obtained = 0;
    *prgh_rows = NULL;

    if (h_chapter != DB_NULL_HCHAPTER)
    {
        return DB_E_BADCHAPTER;
    }
    if (l_rows_offset < 0)
    {
        return DB_E_CANTSCROLLBACKWARDS;
    }
    if (c_rows < 0)
    {
        return DB_E_CANTFETCHBACKWARDS;
    }
    if (c_rows == 0)
    {
        return S_OK;
    }

    for (skip = 0; skip < l_rows_offset; ++skip)
    {
        SQLRETURN rc = SQLFetch(self->hstmt);
        if (rc == SQL_NO_DATA)
        {
            self->end_of_rowset = TRUE;
            return DB_S_ENDOFROWSET;
        }
        if (!(SQL_SUCCEEDED(rc) || rc == SQL_SUCCESS_WITH_INFO))
        {
            return DB_E_ERRORSINCOMMAND;
        }
    }

    rows = (HROW*)CoTaskMemAlloc(sizeof(HROW) * c_rows);
    if (!rows)
    {
        return E_OUTOFMEMORY;
    }
    ZeroMemory(rows, sizeof(HROW) * c_rows);

    wanted = c_rows;
    while (wanted > 0)
    {
        MonetBufferedRow* row = NULL;
        if (!self->available_head)
        {
            HRESULT hr = Rowset_Prefetch(self);
            if (FAILED(hr))
            {
                CoTaskMemFree(rows);
                return hr;
            }
            if (!self->available_head)
            {
                break;
            }
        }

        row = self->available_head;
        self->available_head = row->next;
        if (!self->available_head)
        {
            self->available_tail = NULL;
        }
        row->next = NULL;
        row->ref_count = 1;
        Rowset_AppendOutstanding(self, row);
        rows[obtained++] = (HROW)row;
        --wanted;
    }

    if (obtained == 0)
    {
        CoTaskMemFree(rows);
        return DB_S_ENDOFROWSET;
    }

    *pc_rows_obtained = obtained;
    *prgh_rows = rows;
    return (obtained < (DBCOUNTITEM)c_rows && self->end_of_rowset) ? DB_S_ENDOFROWSET : S_OK;
}

static HRESULT STDMETHODCALLTYPE Rowset_ReleaseRows(IRowset* iface, DBCOUNTITEM c_rows, const HROW rgh_rows[], DBROWOPTIONS rg_row_options[], DBREFCOUNT rg_ref_counts[], DBROWSTATUS rg_row_status[])
{
    MonetRowset* self = Rowset_FromRowset(iface);
    DBCOUNTITEM i;
    MONET_UNUSED(rg_row_options);

    for (i = 0; i < c_rows; ++i)
    {
        MonetBufferedRow* row = (MonetBufferedRow*)rgh_rows[i];
        if (!row)
        {
            if (rg_ref_counts) rg_ref_counts[i] = 0;
            if (rg_row_status) rg_row_status[i] = DBROWSTATUS_E_INVALID;
            continue;
        }

        if (row->ref_count > 0)
        {
            row->ref_count--;
        }
        if (rg_ref_counts) rg_ref_counts[i] = row->ref_count;
        if (rg_row_status) rg_row_status[i] = DBROWSTATUS_S_OK;

        if (row->ref_count == 0)
        {
            Rowset_DetachOutstanding(self, row);
            Rowset_FreeBufferedRow(row);
        }
    }
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE Rowset_RestartPosition(IRowset* iface, HCHAPTER h_chapter)
{
    MonetRowset* self = Rowset_FromRowset(iface);
    HRESULT hr;

    if (h_chapter != DB_NULL_HCHAPTER)
    {
        return DB_E_BADCHAPTER;
    }

    MONET_TRACE(
        "Rowset::RestartPosition",
        "enter command=%p available=%p outstanding=%p end=%d fetched=%llu",
        self->command,
        self->available_head,
        self->outstanding_rows,
        self->end_of_rowset ? 1 : 0,
        (unsigned long long)self->fetched_row_count);

    if (!self->command)
    {
        MONET_TRACE("Rowset::RestartPosition", "%s", "rowset is not command-backed, cannot restart");
        return DB_E_CANTSCROLLBACKWARDS;
    }

    hr = Rowset_RestartCommandRowset(self);
    MONET_TRACE("Rowset::RestartPosition", "exit hr=0x%08lx", (unsigned long)hr);
    return hr;
}

static HRESULT STDMETHODCALLTYPE Rowset_DeleteRowsChange(IRowsetChange* iface, HCHAPTER h_reserved, DBCOUNTITEM c_rows, const HROW rgh_rows[], DBROWSTATUS rg_row_status[])
{
    DBCOUNTITEM i;
    MonetRowset* self = Rowset_FromRowsetChange(iface);
    MONET_UNUSED(h_reserved);
    MONET_UNUSED(rgh_rows);

    for (i = 0; i < c_rows; ++i)
    {
        if (rg_row_status)
        {
            rg_row_status[i] = DBROWSTATUS_E_INVALID;
        }
    }
    MONET_TRACE("Rowset::DeleteRows", "table='%s' support=%d", self->base_table, Rowset_IsInsertable(self) ? 1 : 0);
    return DB_E_NOTSUPPORTED;
}

static HRESULT STDMETHODCALLTYPE Rowset_SetDataChange(IRowsetChange* iface, HROW h_row, HACCESSOR h_accessor, void* p_data)
{
    MonetRowset* self = Rowset_FromRowsetChange(iface);
    MONET_UNUSED(h_row);
    MONET_UNUSED(h_accessor);
    MONET_UNUSED(p_data);
    MONET_TRACE("Rowset::SetData", "table='%s' support=%d", self->base_table, Rowset_IsInsertable(self) ? 1 : 0);
    return DB_E_NOTSUPPORTED;
}

static HRESULT STDMETHODCALLTYPE Rowset_InsertRowChange(IRowsetChange* iface, HCHAPTER h_reserved, HACCESSOR h_accessor, void* p_data, HROW* ph_row)
{
    MonetRowset* self = Rowset_FromRowsetChange(iface);
    MonetAccessor* accessor = NULL;
    CHAR sql[MONETDB_MAX_SQL_TEXT];
    size_t used = 0;
    DBCOUNTITEM i;
    BOOL first = TRUE;
    BOOL have_columns = FALSE;
    SQLHSTMT hstmt = SQL_NULL_HSTMT;
    HRESULT hr;
    const CHAR* schema_name = NULL;

    MONET_UNUSED(h_reserved);
    if (ph_row)
    {
        *ph_row = DB_NULL_HROW;
    }
    if (!p_data)
    {
        return E_INVALIDARG;
    }
    if (!Rowset_IsInsertable(self))
    {
        return DB_E_NOTSUPPORTED;
    }

    accessor = AccessorTable_Find(&self->accessors, h_accessor);
    if (!accessor)
    {
        return DB_E_BADACCESSORHANDLE;
    }

    schema_name = self->base_schema[0] ? self->base_schema : self->session->datasource->config.schema;
    sql[0] = '\0';
    MONET_TRACE(
        "Rowset::InsertRow",
        "enter table='%s.%s' accessor=%p bindings=%llu row_size=%llu phRowRequested=%d",
        schema_name ? schema_name : "",
        self->base_table,
        (void*)h_accessor,
        (unsigned long long)accessor->binding_count,
        (unsigned long long)accessor->row_size,
        ph_row ? 1 : 0);

    if (Rowset_InsertBatchTargetRows(self) > 1)
    {
        hr = Rowset_BufferInsertRow(self, h_accessor, accessor, p_data, schema_name);
        if (hr != S_FALSE)
        {
            if (SUCCEEDED(hr))
            {
                HRESULT handle_hr = Rowset_CreateInsertedRowHandle(self, ph_row);
                if (FAILED(handle_hr))
                {
                    return handle_hr;
                }
            }
            return hr;
        }
        MONET_TRACE("Rowset::InsertRow", "%s", "batch insert skipped, fallback to prepared/text path");
    }

    hr = Rowset_EnsurePreparedInsert(self, h_accessor, accessor, p_data, schema_name);
    if (hr == S_OK)
    {
        hr = Rowset_ExecutePreparedInsert(self, accessor, p_data);
        if (hr != S_FALSE)
        {
            if (SUCCEEDED(hr))
            {
                HRESULT handle_hr = Rowset_CreateInsertedRowHandle(self, ph_row);
                if (FAILED(handle_hr))
                {
                    return handle_hr;
                }
            }
            return hr;
        }
        MONET_TRACE("Rowset::InsertRow", "%s", "prepared fast path skipped, fallback to text SQL");
    }
    else if (FAILED(hr))
    {
        return hr;
    }

    hr = Rowset_AppendSqlText(sql, MONET_ARRAY_SIZE(sql), &used, "INSERT INTO ");
    if (FAILED(hr))
    {
        return hr;
    }
    if (schema_name && schema_name[0])
    {
        hr = Rowset_AppendQuotedIdentifier(sql, MONET_ARRAY_SIZE(sql), &used, schema_name);
        if (FAILED(hr))
        {
            return hr;
        }
        hr = Rowset_AppendSqlText(sql, MONET_ARRAY_SIZE(sql), &used, ".");
        if (FAILED(hr))
        {
            return hr;
        }
    }
    hr = Rowset_AppendQuotedIdentifier(sql, MONET_ARRAY_SIZE(sql), &used, self->base_table);
    if (FAILED(hr))
    {
        return hr;
    }
    hr = Rowset_AppendSqlText(sql, MONET_ARRAY_SIZE(sql), &used, " (");
    if (FAILED(hr))
    {
        return hr;
    }

    for (i = 0; i < accessor->binding_count; ++i)
    {
        const DBBINDING* binding = &accessor->bindings[i];
        DBSTATUS status = Rowset_ReadInputStatus(binding, p_data);
        DBLENGTH length = Rowset_ReadInputLength(binding, p_data);
        BOOL include = Rowset_BindingParticipatesInInsert(binding, p_data, self->column_count);

        MONET_TRACE(
            "Rowset::InsertRow",
            "binding[%llu] ordinal=%lu include=%d part=0x%08lx type=0x%04x status=0x%08lx length=%llu",
            (unsigned long long)i,
            (unsigned long)binding->iOrdinal,
            include ? 1 : 0,
            (unsigned long)binding->dwPart,
            (unsigned int)binding->wType,
            (unsigned long)status,
            (unsigned long long)length);

        if (!include)
        {
            continue;
        }
        if (!first)
        {
            hr = Rowset_AppendSqlText(sql, MONET_ARRAY_SIZE(sql), &used, ", ");
            if (FAILED(hr))
            {
                return hr;
            }
        }
        hr = Rowset_AppendQuotedIdentifier(sql, MONET_ARRAY_SIZE(sql), &used, self->columns[binding->iOrdinal - 1].name_a);
        if (FAILED(hr))
        {
            return hr;
        }
        first = FALSE;
        have_columns = TRUE;
    }

    if (!have_columns)
    {
        return DB_E_ERRORSOCCURRED;
    }

    hr = Rowset_AppendSqlText(sql, MONET_ARRAY_SIZE(sql), &used, ") VALUES (");
    if (FAILED(hr))
    {
        return hr;
    }

    first = TRUE;
    for (i = 0; i < accessor->binding_count; ++i)
    {
        const DBBINDING* binding = &accessor->bindings[i];
        if (!Rowset_BindingParticipatesInInsert(binding, p_data, self->column_count))
        {
            continue;
        }
        if (!first)
        {
            hr = Rowset_AppendSqlText(sql, MONET_ARRAY_SIZE(sql), &used, ", ");
            if (FAILED(hr))
            {
                return hr;
            }
        }
        hr = Rowset_AppendBindingValueSql(binding, &self->columns[binding->iOrdinal - 1], p_data, sql, MONET_ARRAY_SIZE(sql), &used);
        if (FAILED(hr))
        {
            Log_WriteA(
                MONET_LOG_ERROR,
                "Rowset::InsertRow",
                "Impossibile convertire la colonna ordinal=%lu name='%s' wType=0x%04X status=0x%08lX",
                (unsigned long)binding->iOrdinal,
                self->columns[binding->iOrdinal - 1].name_a,
                (unsigned int)binding->wType,
                (unsigned long)Rowset_ReadInputStatus(binding, p_data));
            return hr;
        }
        first = FALSE;
    }

    hr = Rowset_AppendSqlText(sql, MONET_ARRAY_SIZE(sql), &used, ")");
    if (FAILED(hr))
    {
        return hr;
    }

    MONET_TRACE("Rowset::InsertRow", "sql=\"%s\"", sql);
    hr = Odbc_ExecDirectA(self->session->datasource->hdbc, sql, &hstmt);
    Odbc_CloseStatement(&hstmt);
    if (SUCCEEDED(hr))
    {
        HRESULT handle_hr = Rowset_CreateInsertedRowHandle(self, ph_row);
        if (FAILED(handle_hr))
        {
            return handle_hr;
        }
    }
    return hr;
}

static HRESULT STDMETHODCALLTYPE Rowset_CreateAccessorMethod(IAccessor* iface, DBACCESSORFLAGS flags, DBCOUNTITEM c_bindings, const DBBINDING bindings[], DBLENGTH row_size, HACCESSOR* ph_accessor, DBBINDSTATUS rg_status[])
{
    return AccessorTable_Create(&Rowset_FromAccessor(iface)->accessors, flags, c_bindings, bindings, row_size, ph_accessor, rg_status);
}

static HRESULT STDMETHODCALLTYPE Rowset_AddRefAccessorMethod(IAccessor* iface, HACCESSOR h_accessor, DBREFCOUNT* pc_refcount)
{
    return AccessorTable_AddRef(&Rowset_FromAccessor(iface)->accessors, h_accessor, pc_refcount);
}

static HRESULT STDMETHODCALLTYPE Rowset_GetBindingsMethod(IAccessor* iface, HACCESSOR h_accessor, DBACCESSORFLAGS* pflags, DBCOUNTITEM* pc_bindings, DBBINDING** pp_bindings)
{
    return AccessorTable_GetBindings(&Rowset_FromAccessor(iface)->accessors, h_accessor, pflags, pc_bindings, pp_bindings);
}

static HRESULT STDMETHODCALLTYPE Rowset_ReleaseAccessorMethod(IAccessor* iface, HACCESSOR h_accessor, DBREFCOUNT* pc_refcount)
{
    return AccessorTable_Release(&Rowset_FromAccessor(iface)->accessors, h_accessor, pc_refcount);
}

static HRESULT STDMETHODCALLTYPE Rowset_GetColumnInfoMethod(IColumnsInfo* iface, DBORDINAL* pc_columns, DBCOLUMNINFO** prg_info, OLECHAR** pp_buffer)
{
    return Rowset_BuildColumnInfo(Rowset_FromColumns(iface), pc_columns, prg_info, pp_buffer);
}

static HRESULT STDMETHODCALLTYPE Rowset_MapColumnIDsMethod(IColumnsInfo* iface, DBORDINAL c_column_ids, const DBID rg_column_ids[], DBORDINAL rg_columns[])
{
    MonetRowset* self = Rowset_FromColumns(iface);
    DBORDINAL i;
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
            if (_wcsicmp(rg_column_ids[i].uName.pwszName, self->columns[j].name_w) == 0)
            {
                rg_columns[i] = self->columns[j].ordinal;
                break;
            }
        }
    }
    return S_OK;
}

typedef struct RowsetPropMeta
{
    DBPROPID id;
    VARTYPE vt;
    LONG value;
} RowsetPropMeta;

static const RowsetPropMeta g_rowset_props[] =
{
    { DBPROP_CANFETCHBACKWARDS, VT_BOOL, VARIANT_FALSE },
    { DBPROP_CANSCROLLBACKWARDS, VT_BOOL, VARIANT_FALSE },
    { DBPROP_BOOKMARKS, VT_BOOL, VARIANT_FALSE },
    { DBPROP_LITERALBOOKMARKS, VT_BOOL, VARIANT_FALSE },
    { DBPROP_IRowsetLocate, VT_BOOL, VARIANT_FALSE },
    { DBPROP_IRowsetChange, VT_BOOL, VARIANT_FALSE },
    { DBPROP_UPDATABILITY, VT_I4, 0 }
};

static HRESULT Rowset_FillProperty(const MonetRowset* self, DBPROPID propid, DBPROP* prop)
{
    size_t i;
    VariantInit(&prop->vValue);
    ZeroMemory(&prop->colid, sizeof(prop->colid));
    prop->dwOptions = DBPROPOPTIONS_OPTIONAL;
    prop->dwStatus = DBPROPSTATUS_OK;
    prop->dwPropertyID = propid;

    for (i = 0; i < MONET_ARRAY_SIZE(g_rowset_props); ++i)
    {
        if (g_rowset_props[i].id == propid)
        {
            if (propid == DBPROP_IRowsetChange)
            {
                prop->vValue.vt = VT_BOOL;
                prop->vValue.boolVal = VARIANT_TRUE;
                return S_OK;
            }
            if (propid == DBPROP_UPDATABILITY)
            {
                prop->vValue.vt = VT_I4;
                prop->vValue.lVal = (LONG)(self ? self->updatability : 0);
                return S_OK;
            }

            prop->vValue.vt = g_rowset_props[i].vt;
            if (g_rowset_props[i].vt == VT_BOOL)
            {
                prop->vValue.boolVal = (VARIANT_BOOL)g_rowset_props[i].value;
            }
            else
            {
                prop->vValue.lVal = g_rowset_props[i].value;
            }
            return S_OK;
        }
    }

    prop->dwStatus = DBPROPSTATUS_NOTSUPPORTED;
    prop->vValue.vt = VT_EMPTY;
    return DB_S_ERRORSOCCURRED;
}

static HRESULT STDMETHODCALLTYPE Rowset_GetPropertiesMethod(IRowsetInfo* iface, const ULONG c_sets, const DBPROPIDSET rg_sets[], ULONG* pc_sets, DBPROPSET** prg_sets)
{
    MonetRowset* self = Rowset_FromInfo(iface);
    ULONG out_sets = (c_sets == 0 || !rg_sets) ? 1 : c_sets;
    DBPROPSET* sets = NULL;
    ULONG i;

    if (!pc_sets || !prg_sets)
    {
        return E_POINTER;
    }
    *pc_sets = 0;
    *prg_sets = NULL;

    sets = (DBPROPSET*)CoTaskMemAlloc(sizeof(DBPROPSET) * out_sets);
    if (!sets)
    {
        return E_OUTOFMEMORY;
    }
    ZeroMemory(sets, sizeof(DBPROPSET) * out_sets);

    for (i = 0; i < out_sets; ++i)
    {
        ULONG j;
        const DBPROPIDSET* request = (c_sets == 0 || !rg_sets) ? NULL : &rg_sets[i];
        sets[i].guidPropertySet = request ? request->guidPropertySet : DBPROPSET_ROWSET;
        if (request && !Monet_IsEqualPropertySet(&request->guidPropertySet, &DBPROPSET_ROWSET))
        {
            continue;
        }
        sets[i].cProperties = request && request->cPropertyIDs > 0 ? request->cPropertyIDs : (ULONG)MONET_ARRAY_SIZE(g_rowset_props);
        sets[i].rgProperties = (DBPROP*)CoTaskMemAlloc(sizeof(DBPROP) * sets[i].cProperties);
        if (!sets[i].rgProperties)
        {
            return E_OUTOFMEMORY;
        }
        ZeroMemory(sets[i].rgProperties, sizeof(DBPROP) * sets[i].cProperties);
        for (j = 0; j < sets[i].cProperties; ++j)
        {
            DBPROPID propid = request && request->cPropertyIDs > 0 ? request->rgPropertyIDs[j] : g_rowset_props[j].id;
            Rowset_FillProperty(self, propid, &sets[i].rgProperties[j]);
        }
    }

    *pc_sets = out_sets;
    *prg_sets = sets;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE Rowset_GetReferencedRowsetMethod(IRowsetInfo* iface, DBORDINAL ordinal, REFIID riid, IUnknown** pp_referenced)
{
    MONET_UNUSED(iface);
    MONET_UNUSED(ordinal);
    if (pp_referenced)
    {
        *pp_referenced = NULL;
    }
    MONET_UNUSED(riid);
    return DB_E_NOTFOUND;
}

static HRESULT STDMETHODCALLTYPE Rowset_GetSpecificationMethod(IRowsetInfo* iface, REFIID riid, IUnknown** pp_specification)
{
    MonetRowset* self = Rowset_FromInfo(iface);
    if (!pp_specification)
    {
        return E_POINTER;
    }
    *pp_specification = NULL;
    if (self->command)
    {
        return self->command->ICommandText_iface.lpVtbl->QueryInterface(&self->command->ICommandText_iface, riid, (void**)pp_specification);
    }
    return self->session->IOpenRowset_iface.lpVtbl->QueryInterface(&self->session->IOpenRowset_iface, riid, (void**)pp_specification);
}

static HRESULT STDMETHODCALLTYPE Rowset_CanConvertMethod(IConvertType* iface, DBTYPE from_type, DBTYPE to_type, DBCONVERTFLAGS flags)
{
    DBTYPE from_base = (DBTYPE)(from_type & ~DBTYPE_BYREF);
    DBTYPE to_base = (DBTYPE)(to_type & ~DBTYPE_BYREF);

    MONET_UNUSED(iface);
    MONET_UNUSED(flags);
    if (from_type == to_type || from_base == to_base || to_base == DBTYPE_STR || to_base == DBTYPE_WSTR)
    {
        return S_OK;
    }
    return DB_E_UNSUPPORTEDCONVERSION;
}

static IRowsetVtbl g_rowset_vtbl =
{
    Rowset_QueryInterface_Rowset,
    Rowset_AddRef_Rowset,
    Rowset_Release_Rowset,
    Rowset_AddRefRows,
    Rowset_GetData,
    Rowset_GetNextRows,
    Rowset_ReleaseRows,
    Rowset_RestartPosition
};

static IRowsetChangeVtbl g_rowset_change_vtbl =
{
    Rowset_QueryInterface_RowsetChange,
    Rowset_AddRef_RowsetChange,
    Rowset_Release_RowsetChange,
    Rowset_DeleteRowsChange,
    Rowset_SetDataChange,
    Rowset_InsertRowChange
};

static IAccessorVtbl g_rowset_accessor_vtbl =
{
    Rowset_QueryInterface_Accessor,
    Rowset_AddRef_Accessor,
    Rowset_Release_Accessor,
    Rowset_AddRefAccessorMethod,
    Rowset_CreateAccessorMethod,
    Rowset_GetBindingsMethod,
    Rowset_ReleaseAccessorMethod
};

static IColumnsInfoVtbl g_rowset_columns_vtbl =
{
    Rowset_QueryInterface_Columns,
    Rowset_AddRef_Columns,
    Rowset_Release_Columns,
    Rowset_GetColumnInfoMethod,
    Rowset_MapColumnIDsMethod
};

static IRowsetInfoVtbl g_rowset_info_vtbl =
{
    Rowset_QueryInterface_Info,
    Rowset_AddRef_Info,
    Rowset_Release_Info,
    Rowset_GetPropertiesMethod,
    Rowset_GetReferencedRowsetMethod,
    Rowset_GetSpecificationMethod
};

static IConvertTypeVtbl g_rowset_convert_vtbl =
{
    Rowset_QueryInterface_Convert,
    Rowset_AddRef_Convert,
    Rowset_Release_Convert,
    Rowset_CanConvertMethod
};

static ISupportErrorInfoVtbl g_rowset_support_error_info_vtbl =
{
    Rowset_QueryInterface_SupportErrorInfo,
    Rowset_AddRef_SupportErrorInfo,
    Rowset_Release_SupportErrorInfo,
    Rowset_InterfaceSupportsErrorInfo
};

HRESULT Rowset_Create(MonetSession* session, MonetCommand* command, SQLHSTMT hstmt, REFGUID schema_rowset, REFIID riid, void** ppv)
{
    MonetRowset* self = NULL;
    HRESULT hr;

    if (!session || !ppv)
    {
        return E_POINTER;
    }

    *ppv = NULL;
    self = (MonetRowset*)CoTaskMemAlloc(sizeof(*self));
    if (!self)
    {
        return E_OUTOFMEMORY;
    }
    ZeroMemory(self, sizeof(*self));

    self->IRowset_iface.lpVtbl = &g_rowset_vtbl;
    self->IRowsetChange_iface.lpVtbl = &g_rowset_change_vtbl;
    self->IAccessor_iface.lpVtbl = &g_rowset_accessor_vtbl;
    self->IColumnsInfo_iface.lpVtbl = &g_rowset_columns_vtbl;
    self->IRowsetInfo_iface.lpVtbl = &g_rowset_info_vtbl;
    self->IConvertType_iface.lpVtbl = &g_rowset_convert_vtbl;
    self->ISupportErrorInfo_iface.lpVtbl = &g_rowset_support_error_info_vtbl;
    self->ref_count = 1;
    self->session = session;
    self->command = command;
    self->hstmt = hstmt;
    session->IOpenRowset_iface.lpVtbl->AddRef(&session->IOpenRowset_iface);
    if (command)
    {
        command->ICommandText_iface.lpVtbl->AddRef(&command->ICommandText_iface);
    }
    AccessorTable_Init(&self->accessors);
    Monet_ObjectAddRef();

    if (command && !schema_rowset && command->columns && command->column_count > 0)
    {
        hr = Rowset_CloneColumns(command->columns, command->column_count, &self->columns);
        if (SUCCEEDED(hr))
        {
            self->column_count = command->column_count;
            MONET_TRACE("Rowset::Create", "reused command metadata columns=%lu", (unsigned long)self->column_count);
        }
    }
    else
    {
        hr = Odbc_DescribeColumns(hstmt, &self->columns, &self->column_count);
    }
    if (FAILED(hr))
    {
        Rowset_ReleaseInternal(self);
        return hr;
    }

    Rowset_AdjustSchemaMetadata(schema_rowset, self->columns, self->column_count);
    Rowset_DetectUpdatability(self);

    hr = Rowset_QueryInterfaceInternal(self, riid, ppv);
    Rowset_ReleaseInternal(self);
    return hr;
}
