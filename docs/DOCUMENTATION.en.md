# MonetDB OLE DB Provider — Documentation

> 🇮🇹 [Versione italiana](DOCUMENTAZIONE.it.md)

**Version:** 1.0.52-varchar2000
**Author:** Umberto Meglio
**License:** [MIT](../LICENSE)

---

## Table of contents

1. [Introduction](#1-introduction)
2. [Architecture](#2-architecture)
3. [COM objects and interfaces](#3-com-objects-and-interfaces)
4. [Schema rowsets and catalog browsing](#4-schema-rowsets-and-catalog-browsing)
5. [Configuration (INI file)](#5-configuration-ini-file)
6. [Building](#6-building)
7. [Installation and registration](#7-installation-and-registration)
8. [Linked Server setup](#8-linked-server-setup)
9. [Using it from SQL Server](#9-using-it-from-sql-server)
10. [Performance and tuning](#10-performance-and-tuning)
11. [Batched inserts (`IRowsetChange`)](#11-batched-inserts-irowsetchange)
12. [Logging and diagnostics](#12-logging-and-diagnostics)
13. [Test tools (probes)](#13-test-tools-probes)
14. [Troubleshooting](#14-troubleshooting)
15. [Technical notes](#15-technical-notes)
16. [Source code layout](#16-source-code-layout)
17. [License](#17-license)

---

## 1. Introduction

MonetDB OLE DB Provider is a native 64-bit OLE DB provider, written entirely in ANSI C, that bridges Microsoft SQL Server and MonetDB through the official MonetDB ODBC driver.

The provider registers itself in the Windows COM system as an in-process DLL (`InprocServer32`) and shows up as a selectable option in the Linked Server configuration of SQL Server Management Studio (SSMS). It lets you:

- query MonetDB tables and views directly from T-SQL (`OPENQUERY`, four-part names, `EXEC ... AT`);
- browse the MonetDB catalog (schemas, tables, views, columns, keys, indexes) from the SSMS object tree;
- insert data into MonetDB through `IRowsetChange::InsertRow` with automatic batching.

### Prerequisites

| Component | Minimum version |
|-----------|-----------------|
| Visual Studio | 2022 with the C/C++ workload |
| Windows SDK | 10.0+ |
| MonetDB ODBC driver | installed as a 64-bit system driver |
| SQL Server | 2016+ |
| SSMS | 19+ |

## 2. Architecture

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

A query flows through the provider as follows:

1. SQL Server instantiates the provider via COM (CLSID `{A3F2D8E1-7B4C-4E9A-B5D6-1C8F3E2A9D07}`).
2. `IDBInitialize::Initialize` loads the INI file and opens the ODBC connection to MonetDB (one `SQLHDBC` per data source, reused by every session).
3. The `Session` creates a `Command` (`IDBCreateCommand`) or opens a table directly (`IOpenRowset`).
4. The `Command` executes the SQL via `SQLExecDirect`/`SQLPrepare` and returns a `Rowset`.
5. The `Rowset` prefetches rows into a buffered window and exposes them to SQL Server through `IRowset::GetNextRows` and OLE DB accessors.

## 3. COM objects and interfaces

| COM object | Implemented interfaces |
|------------|------------------------|
| Data Source | `IDBInitialize`, `IDBProperties`, `IDBCreateSession`, `IPersist`, `IDBInfo`, `ISupportErrorInfo` |
| Session     | `IOpenRowset`, `IGetDataSource`, `IDBCreateCommand`, `IDBSchemaRowset`, `ISessionProperties`, `ITransactionJoin`, `ITransactionLocal`, `ISupportErrorInfo` |
| Command     | `ICommandText`, `ICommandProperties`, `IColumnsInfo`, `IAccessor`, `IConvertType`, `ICommandPrepare`, `ISupportErrorInfo` |
| Rowset      | `IRowset`, `IRowsetChange`, `IAccessor`, `IColumnsInfo`, `IRowsetInfo`, `IConvertType`, `ISupportErrorInfo` |

Implementation notes:

- Data Source and Session support COM aggregation (outer unknown).
- The registered threading model is `Both`; shared structures (accessor table, configuration) are protected by `CRITICAL_SECTION`s.
- `ITransactionLocal`/`ITransactionJoin` expose local transaction support built on ODBC autocommit.

## 4. Schema rowsets and catalog browsing

The `schema.c` module implements the 13 schema rowsets SSMS needs to build the linked server catalog tree. Each schema rowset is translated into a query against MonetDB's system catalog:

| Schema rowset | What it shows in SSMS | MonetDB source |
|---------------|-----------------------|----------------|
| `DBSCHEMA_CATALOGS` | catalog node | INI configuration |
| `DBSCHEMA_SCHEMATA` | schemas under the catalog | `sys.schemas` |
| `DBSCHEMA_TABLES` | tables per schema | `sys.tables` |
| `DBSCHEMA_VIEWS` | views per schema | `sys.tables` |
| `DBSCHEMA_COLUMNS` | table/view columns | `sys.columns` |
| `DBSCHEMA_VIEW_COLUMN_USAGE` | view columns | `sys.columns` |
| `DBSCHEMA_PROCEDURES` | functions/procedures | `sys.functions` |
| `DBSCHEMA_PROCEDURE_PARAMETERS` | procedure parameters | `sys.args` |
| `DBSCHEMA_PRIMARY_KEYS` | primary keys | `sys.keys` |
| `DBSCHEMA_FOREIGN_KEYS` | foreign keys | `sys.keys` |
| `DBSCHEMA_INDEXES` | indexes | `sys.idxs` + `sys.objects` |
| `DBSCHEMA_PROVIDER_TYPES` | supported data types | `sys.types` |
| `DBSCHEMA_TABLE_STATISTICS` | table statistics | `sys.tables` |

The restrictions (catalog, schema, table name) passed by SSMS are applied as `WHERE` filters in the generated queries (`Schema_BuildSql`).

## 5. Configuration (INI file)

The provider reads its configuration from `monetdb_oledb.ini`, section `[MonetDB]`, looked up in the DLL's directory. Supported keys:

| Key | Default | Description |
|-----|---------|-------------|
| `DSN` | `MonetDB` | Name of the 64-bit system ODBC DSN pointing to MonetDB |
| `Database` | — | MonetDB database/catalog |
| `Schema` | `sys` | Default schema |
| `User` | — | MonetDB user (may be overridden by the linked server) |
| `Password` | — | Password (may be overridden by the linked server) |
| `ConnectionTimeout` | `30` | Connection timeout in seconds |
| `QueryTimeout` | `120` | Query timeout in seconds |
| `ReadOnly` | `0` | `1` = open the connection read-only |
| `AutoCommit` | `1` | `1` = ODBC autocommit enabled |
| `FetchRows` | `256` | Rows prefetched per batch (min 1, max 4096) |
| `FetchWindowKB` | `1204` | Buffered-window memory cap in KB (min 64, max 16384) |
| `LogFile` | `monetdb_oledb.log` | Log file path |
| `LogLevel` | — | `1`=ERROR, `2`=INFO, `3`=DEBUG, `4`=TRACE |
| `Trace` | `0` | `1` = enable detailed COM call tracing |

Example:

```ini
[MonetDB]
DSN=MonetDB
Database=demo
Schema=sys
User=monetdb
Password=***
ConnectionTimeout=30
QueryTimeout=120
ReadOnly=0
AutoCommit=1
FetchRows=256
FetchWindowKB=1204
LogFile=monetdb_oledb.log
LogLevel=2
Trace=0
```

> ⚠️ **Security:** the INI file may contain plain-text credentials. Restrict the NTFS permissions of the installation directory and, where possible, prefer credentials configured at the linked server level (`sp_addlinkedsrvlogin`).

## 6. Building

### With NMAKE

From the **Developer Command Prompt for VS 2022 (x64)**:

```cmd
nmake                 :: Release build (build\Release\monetdb_oledb.dll)
nmake DEBUG=1         :: Debug build with symbols
nmake clean           :: remove the build directory
```

The Release build uses `/O2 /GL /MT` with `/LTCG` linking; the Debug build uses `/Od /Zi /MTd`.

### With Visual Studio

Open `monetdb_oledb.sln` in Visual Studio 2022 and build the `x64` configuration.

Linked libraries: `odbc32.lib`, `odbccp32.lib`, `ole32.lib`, `oleaut32.lib`, `advapi32.lib`, `uuid.lib`. The COM exports (`DllGetClassObject`, `DllCanUnloadNow`, `DllRegisterServer`, `DllUnregisterServer`) are declared in `monetdb_oledb.def`.

## 7. Installation and registration

### Automatic installation

1. Create a **64-bit system** ODBC DSN named `MonetDB` (or whatever name is configured in `DSN=`).
2. Update `config\monetdb_oledb.ini` with your connection parameters.
3. From an **elevated** Developer Command Prompt x64:

```cmd
nmake install
```

`nmake install`:

- runs `unlock_provider.ps1` to shut down any `dllhost.exe` COM surrogates holding the DLL open (if the DLL is loaded by `sqlservr.exe` or SSMS, installation stops with an explicit message instead of the generic "file in use" error);
- copies the DLL, INI and scripts to `C:\MonetDB_OleDb`;
- registers the DLL with `regsvr32 /s`;
- runs `register.bat` to create the SQL Server provider keys.

`nmake uninstall` unregisters the DLL and removes the installation directory.

### Manual installation

```cmd
powershell -ExecutionPolicy Bypass -File scripts\unlock_provider.ps1 -Path C:\MonetDB_OleDb\monetdb_oledb.dll
copy build\Release\monetdb_oledb.dll C:\MonetDB_OleDb\
copy config\monetdb_oledb.ini        C:\MonetDB_OleDb\
regsvr32 C:\MonetDB_OleDb\monetdb_oledb.dll
C:\MonetDB_OleDb\register.bat
```

### What `register.bat` does

For the SQL Server instance (and every instance found in the registry) it creates the `Providers\MonetDB.OleDb` key with:

| Value | Setting |
|-------|---------|
| `AllowInProcess` | `1` |
| `DynamicParameters` | `1` |
| `NestedQueries` | `1` |
| `LevelZeroOnly` | `0` |

It also grants *Modify* to `NT SERVICE\ALL SERVICES` on the installation directory, so that the SQL Server service can read the INI and write log files.

Options: `register.bat /check` verifies the registration state; `register.bat /u` removes the keys.

## 8. Linked Server setup

The `scripts\setup_linkedserver.sql` script creates a linked server named `MONETDB_LS`. Before running it, adjust the variables at the top of the script:

```sql
DECLARE @LinkedServerName NVARCHAR(128) = N'MONETDB_LS';
DECLARE @DataSource       NVARCHAR(256) = N'MonetDB';   -- ODBC DSN
DECLARE @Catalog          NVARCHAR(128) = N'demo';      -- MonetDB database
DECLARE @RemoteUser       NVARCHAR(128) = N'monetdb';
DECLARE @RemotePassword   NVARCHAR(128) = N'...';
```

The script:

1. drops any existing linked server with the same name;
2. creates the linked server with `@provider = N'MonetDB.OleDb'`;
3. maps the remote credentials with `sp_addlinkedsrvlogin`;
4. sets the `data access`, `rpc out`, `use remote collation`, `connect timeout` and `query timeout` options.

## 9. Using it from SQL Server

```sql
-- Pass-through (recommended): the query runs on MonetDB
SELECT * FROM OPENQUERY(MONETDB_LS, 'SELECT * FROM sys.tables LIMIT 10');

-- Four-part name: SQL Server uses the provider's schema rowsets and rowset
SELECT * FROM [MONETDB_LS]...[sys].[tables];

-- Remote execution
EXEC ('SELECT COUNT(*) FROM myschema.mytable') AT MONETDB_LS;

-- Remote insert (uses IRowsetChange::InsertRow with batching)
INSERT INTO [MONETDB_LS]...[myschema].[mytable] (col1, col2) VALUES (1, 'abc');
```

Tips:

- With `OPENQUERY`, filtering and aggregation run on MonetDB: it is almost always the most efficient path.
- With four-part names SQL Server may pull the whole table and filter locally; use them mostly for exploration.
- The SQL inside `OPENQUERY`/`EXEC ... AT` uses MonetDB's dialect (e.g. `LIMIT` instead of `TOP`).

## 10. Performance and tuning

Two INI settings control row prefetching:

```ini
FetchRows=256
FetchWindowKB=1204
```

- **`FetchRows`** — how many rows the rowset tries to prefetch per batch (bounds: 1–4096). It is also used as the batch size for inserts via `IRowsetChange::InsertRow`.
- **`FetchWindowKB`** — caps both the buffered-window memory and the maximum size of a multi-row `INSERT ... VALUES (...), ...` statement (bounds: 64–16384 KB).

With `Trace=1` the log reports, for every prefetch: `fetch_rows`, `fetch_window_kb`, `elapsed_us`, `avg_row_bytes`, `rows_per_sec` — useful to calibrate the values against your workload. For bulk loads, start from `FetchRows=256` and `FetchWindowKB=1204` and increase gradually while watching `rows_per_sec`.

Strings up to 2000 characters (`MONETDB_MAX_FAST_STRING_CHARS`) go through the fast fixed-buffer fetch path; beyond that threshold the value is read in chunks (`SQLGetData`).

## 11. Batched inserts (`IRowsetChange`)

When SQL Server inserts rows through the linked server, the provider accumulates rows in a buffer and sends them to MonetDB as a multi-row `INSERT ... VALUES (...), (...), ...`:

- the batch size is governed by `FetchRows`;
- the maximum size of the generated SQL statement is capped by `FetchWindowKB`;
- flushing happens automatically when the batch fills up and when the rowset is closed;
- with `Trace=1` every flush is traced in the log as `Rowset::InsertBatch`, with buffered/flushed row counts and flush count.

## 12. Logging and diagnostics

The provider writes three kinds of logs:

1. **Bootstrap log** — written from `DllMain` before the configuration is loaded; useful when the provider cannot even initialize.
2. **Operational log** — the file configured with `LogFile`, with timestamp, PID and TID on every line; verbosity is governed by `LogLevel` (1=ERROR … 4=TRACE).
3. **COM tracing** — with `Trace=1`, COM interface calls, executed queries and fetch/insert statistics are traced.

### Diagnostic scripts

```cmd
scripts\register.bat /check
```

verifies the COM registration and the SQL Server provider keys.

```powershell
powershell -ExecutionPolicy Bypass -File scripts\diagnose_provider.ps1
```

`diagnose_provider.ps1` shows:

- the provider registration state;
- the processes holding `monetdb_oledb.dll` open;
- the latest relevant lines of the SQL Server `ERRORLOG` for `MonetDB`, `OLE DB` and the most common linked server errors.

If `dllhost.exe /Processid:{2206CDB0-19C1-11D1-89E0-00C04FD7A829}` appears among the processes, that is the `MSDAINITIALIZE` COM surrogate of the OLE DB layer: `unlock_provider.ps1` shuts it down before replacing the DLL.

### Other scripts

| Script | Purpose |
|--------|---------|
| `unlock_provider.ps1` | shuts down surrogate processes locking the DLL before copying a new one |
| `reregister_here.bat` | re-registers the DLL from the current directory |
| `deploy_137_admin.ps1` | deployment script towards a specific server (remote deploy example) |
| `widen_varchar_2000.ps1` | utility related to 2000-character varchar handling |

## 13. Test tools (probes)

The `tools/` directory contains four standalone programs that exercise the provider without SQL Server:

| Probe | What it verifies |
|-------|------------------|
| `probe_msdainitialize.c` | provider instantiation through `MSDAINITIALIZE` (the same path SQL Server uses) |
| `probe_query_fetch.c` | query execution and row fetching via `IRowset` |
| `probe_rowsetchange.c` | inserts via `IRowsetChange::InsertRow` and batching |
| `probe_schema_rowsets.c` | the 13 catalog schema rowsets |

They are useful for isolating problems: if a probe works but the linked server does not, the problem lies in the SQL Server configuration (permissions, `AllowInProcess`, credentials) rather than in the provider.

## 14. Troubleshooting

| Symptom | Likely cause | Remedy |
|---------|--------------|--------|
| The provider does not appear in SSMS among the linked server providers | DLL not registered, or registered as 32-bit | `regsvr32` on the 64-bit copy; `register.bat /check` |
| Error 7302 "Cannot create an instance of OLE DB provider" | missing provider keys or permissions | run `register.bat` as administrator; check `AllowInProcess=1` |
| Error 7303 "Cannot initialize the data source object" | wrong/missing ODBC DSN, credentials, MonetDB unreachable | check the 64-bit system DSN and the INI file; read the log |
| "File in use" during installation | DLL loaded by `dllhost.exe`, `sqlservr.exe` or SSMS | `unlock_provider.ps1`; if needed restart SQL Server / close SSMS |
| Slow queries on large tables | prefetch too small, or filtering on the SQL Server side | use `OPENQUERY`; raise `FetchRows`/`FetchWindowKB` |
| No log produced | the SQL Server service cannot write to the directory | `register.bat` grants *Modify* to `NT SERVICE\ALL SERVICES`; check NTFS permissions |

For fine-grained analysis, set `LogLevel=4` and `Trace=1`, reproduce the issue and read the operational log (every line carries timestamp, PID and TID).

## 15. Technical notes

- Threading model: `Both`
- Fixed CLSID: `{A3F2D8E1-7B4C-4E9A-B5D6-1C8F3E2A9D07}`
- ProgIDs: `MonetDB.OleDb.1` and `MonetDB.OleDb`
- `INITGUID` and `DBINITCONSTANTS` are defined only in `monetdb_oledb_main.c`
- The ODBC connection is opened in `IDBInitialize::Initialize`
- Every `Session` reuses the data source's `SQLHDBC`
- Maximum SQL text length: 32768 characters (`MONETDB_MAX_SQL_TEXT`)
- Static runtime (`/MT`): no dependency on the VC++ redistributable

## 16. Source code layout

| File | Responsibility |
|------|----------------|
| `src/monetdb_oledb_main.c` | `DllMain`, class factory, COM registration, DLL exports |
| `src/config.c` | INI file loading, logging (bootstrap and operational) |
| `src/odbc_utils.c` | ODBC environment/connections, statement execution, SQL→DBTYPE mapping, error messages |
| `src/datasource.c` | Data Source object: initialization properties, connection opening |
| `src/session.c` | Session object: command creation, open rowset, transactions |
| `src/command.c` | Command object: SQL text, prepare, properties, columns |
| `src/rowset.c` | Rowset object: buffered fetch, accessors, conversions, batched inserts |
| `src/schema.c` | the 13 schema rowsets and catalog query generation |
| `include/monetdb_oledb.h` | shared structures, constants and prototypes |

## 17. License

This project is distributed under the **MIT** license — see the [LICENSE](../LICENSE) file.

Copyright (c) 2026 Umberto Meglio
