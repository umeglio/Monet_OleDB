# MonetDB OLE DB Provider for SQL Server

> 🇮🇹 Questo documento è disponibile anche in italiano: **[README.it.md](README.it.md)**
>
> 🌐 Project page: **[umeglio.github.io/Monet_OleDB](https://umeglio.github.io/Monet_OleDB/)**

**Author:** Umberto Meglio
**Built with the support of:** Claude by Anthropic
**Current version:** 1.0.52-varchar2000
**License:** [MIT](LICENSE)

---

## Overview

A native OLE DB provider written in ANSI C that bridges SQL Server and [MonetDB](https://www.monetdb.org/) through the MonetDB ODBC driver. The provider registers itself in the Windows COM system as an in-process DLL and shows up as a selectable option when configuring SQL Server Linked Servers.

MonetDB is the pioneering open-source columnar database developed at CWI (Centrum Wiskunde & Informatica, Amsterdam), still actively developed after decades of research and engineering. What was missing on Windows was a way to plug it directly into the SQL Server ecosystem — this provider fills that gap.

```text
SQL Server (SSMS / T-SQL)
        |
        v
   OLE DB Layer (this provider, 64-bit)
        |
        v
   ODBC Driver Manager (Windows)
        |
        v
   MonetDB ODBC Driver
        |
        v
   MonetDB Server
```

Being MIT-licensed, the provider can be freely used, embedded, and redistributed — including integration into commercial suites that need direct SQL Server connectivity towards MonetDB.

## Implemented interfaces

| COM object | Interfaces |
|------------|------------|
| Data Source | `IDBInitialize`, `IDBProperties`, `IDBCreateSession`, `IPersist`, `IDBInfo` |
| Session     | `IOpenRowset`, `IGetDataSource`, `IDBCreateCommand`, `IDBSchemaRowset`, `ISessionProperties`, `ITransactionJoin`, `ITransactionLocal` |
| Command     | `ICommandText`, `ICommandProperties`, `IColumnsInfo`, `IAccessor`, `IConvertType`, `ICommandPrepare` |
| Rowset      | `IRowset`, `IAccessor`, `IColumnsInfo`, `IRowsetInfo`, `IConvertType` |

### Catalog browsing (`IDBSchemaRowset`)

The `schema.c` module implements 13 schema rowsets that let SSMS build the linked server catalog tree:

| Schema | What it shows in SSMS | MonetDB source |
|--------|-----------------------|----------------|
| DBSCHEMA_CATALOGS | catalog node | INI configuration |
| DBSCHEMA_SCHEMATA | schemas under the catalog | `sys.schemas` |
| DBSCHEMA_TABLES | tables per schema | `sys.tables` |
| DBSCHEMA_VIEWS | views per schema | `sys.tables` |
| DBSCHEMA_COLUMNS | table/view columns | `sys.columns` |
| DBSCHEMA_VIEW_COLUMN_USAGE | view columns | `sys.columns` |
| DBSCHEMA_PROCEDURES | functions/procedures | `sys.functions` |
| DBSCHEMA_PROCEDURE_PARAMETERS | procedure parameters | `sys.args` |
| DBSCHEMA_PRIMARY_KEYS | primary keys | `sys.keys` |
| DBSCHEMA_FOREIGN_KEYS | foreign keys | `sys.keys` |
| DBSCHEMA_INDEXES | indexes | `sys.idxs` + `sys.objects` |
| DBSCHEMA_PROVIDER_TYPES | supported data types | `sys.types` |
| DBSCHEMA_TABLE_STATISTICS | table statistics | `sys.tables` |

## Project layout

```text
monetdb_oledb/
├── include/
│   └── monetdb_oledb.h
├── src/
│   ├── monetdb_oledb_main.c
│   ├── config.c
│   ├── odbc_utils.c
│   ├── datasource.c
│   ├── session.c
│   ├── command.c
│   ├── rowset.c
│   └── schema.c
├── config/
│   └── monetdb_oledb.ini
├── scripts/
│   ├── register.bat
│   ├── setup_linkedserver.sql
│   └── diagnose_provider.ps1
├── monetdb_oledb.def
├── monetdb_oledb.sln / .vcxproj
├── Makefile
└── README.md
```

## Prerequisites

1. Visual Studio 2022 with the C/C++ workload.
2. Windows SDK 10.0+.
3. MonetDB ODBC driver installed.
4. SQL Server 2016+.
5. SSMS 19+.

## Build

From the **x64 Developer Command Prompt**:

```cmd
nmake
nmake DEBUG=1
nmake clean
```

Or open `monetdb_oledb.sln` in Visual Studio 2022 and build the `x64` configuration.

## Installation

1. Create a 64-bit system ODBC DSN named `MonetDB`.
2. Update `config\monetdb_oledb.ini`.
3. Run:

```cmd
nmake install
```

`nmake install` first tries to shut down any `dllhost.exe` COM surrogates keeping the DLL open. If the DLL is still loaded by `sqlservr.exe` or SSMS, the installation stops with an explicit message instead of the generic "file in use" error.

Alternatively, copy the DLL/INI and register manually:

```cmd
powershell -ExecutionPolicy Bypass -File scripts\unlock_provider.ps1 -Path C:\MonetDB_OleDb\monetdb_oledb.dll
copy build\Release\monetdb_oledb.dll C:\MonetDB_OleDb\
copy config\monetdb_oledb.ini        C:\MonetDB_OleDb\
regsvr32 C:\MonetDB_OleDb\monetdb_oledb.dll
C:\MonetDB_OleDb\register.bat
```

For heavy workloads the provider exposes two prefetch knobs in the INI file:

```ini
FetchRows=256
FetchWindowKB=1204
```

`FetchRows` controls how many rows the rowset tries to prefetch per batch and is also used as the batch size for inserts through `IRowsetChange::InsertRow`; `FetchWindowKB` caps both the buffered window memory and the maximum size of a multi-row `INSERT ... VALUES (...), ...`. With `Trace=1` the log also reports `fetch_rows`, `fetch_window_kb`, `elapsed_us`, `avg_row_bytes`, `rows_per_sec` for every prefetch and `Rowset::InsertBatch` for batched inserts.

## Using it from SQL Server

```sql
SELECT * FROM OPENQUERY(MONETDB_LS, 'SELECT * FROM sys.tables LIMIT 10');
SELECT * FROM [MONETDB_LS]...[sys].[tables];
EXEC ('SELECT COUNT(*) FROM myschema.mytable') AT MONETDB_LS;
```

## Logging and diagnostics

The provider writes:

- a bootstrap log from `DllMain`, before full initialization;
- an operational log with timestamp, PID and TID;
- COM tracing when `Trace=1`.

Useful scripts:

```cmd
scripts\register.bat /check
```

```powershell
powershell -ExecutionPolicy Bypass -File scripts\diagnose_provider.ps1
```

`diagnose_provider.ps1` also lists the processes keeping `monetdb_oledb.dll` open. If `dllhost.exe /Processid:{2206CDB0-19C1-11D1-89E0-00C04FD7A829}` shows up, it is the `MSDAINITIALIZE` COM surrogate of the OLE DB layer.
It also shows the latest relevant SQL Server `ERRORLOG` lines for `MonetDB`, `OLE DB` and the most common linked server errors.

## Technical notes

- Threading model: `Both`
- Fixed CLSID: `{A3F2D8E1-7B4C-4E9A-B5D6-1C8F3E2A9D07}`
- ProgIDs: `MonetDB.OleDb.1` and `MonetDB.OleDb`
- `INITGUID` and `DBINITCONSTANTS` are defined only in `monetdb_oledb_main.c`
- The ODBC connection is opened on `IDBInitialize::Initialize`
- Every `Session` reuses the data source's `SQLHDBC`

## Code status

This repository contains a complete, coherent Visual Studio 2022 code base with:

- COM registration of the DLL;
- INI configuration loading;
- bootstrap/operational logging;
- ODBC connection towards MonetDB;
- `DataSource`, `Session`, `Command`, `Rowset` objects;
- 13 schema rowsets for catalog browsing;
- setup, diagnostics and linked server scripts.

Before going to production it is recommended to validate and refine:

- the exact MonetDB catalog queries against your server version;
- the more complex type conversions;
- full compatibility with every probe performed by SQL Server/OLE DB services.

## License

Released under the [MIT License](LICENSE). You are free to use, modify, redistribute and integrate this provider — including in commercial products — as long as the copyright notice is preserved.

## Acknowledgements

A heartfelt thank-you goes to the MonetDB developers at CWI for their remarkable openness and availability towards the community: decades of pioneering work on columnar databases, still actively maintained and generously shared. More about the project's background is in the [Italian README](README.it.md).
