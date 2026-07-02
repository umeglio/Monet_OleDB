# MonetDB OLE DB Provider per SQL Server

**Autore:** Umberto Meglio
**Supporto alla creazione:** Claude di Anthropic
**Versione corrente:** 1.0.52-varchar2000
**Licenza:** [MIT](LICENSE)

> 🇬🇧 *English speakers: full documentation is available in [English](docs/DOCUMENTATION.en.md).*
> 🇮🇹 *La documentazione completa in italiano è disponibile in [docs/DOCUMENTAZIONE.it.md](docs/DOCUMENTAZIONE.it.md).*

---

## Panoramica

Provider OLE DB nativo scritto in ANSI C che fa da ponte tra SQL Server e MonetDB attraverso il driver ODBC di MonetDB. Il provider si registra nel sistema COM di Windows come DLL in-process e appare come opzione selezionabile nella configurazione dei Linked Server di SQL Server.

```text
SQL Server (SSMS / T-SQL)
        |
        v
   OLE DB Layer (questo provider, 64-bit)
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

## Documentazione

| Documento | Lingua |
|-----------|--------|
| [docs/DOCUMENTAZIONE.it.md](docs/DOCUMENTAZIONE.it.md) | Italiano |
| [docs/DOCUMENTATION.en.md](docs/DOCUMENTATION.en.md) | English |

La documentazione copre in dettaglio: architettura, interfacce COM implementate, schema rowset, configurazione INI, build, installazione, configurazione del linked server, tuning delle prestazioni, logging, diagnostica e risoluzione dei problemi.

## Interfacce implementate

| Oggetto COM | Interfacce |
|-------------|------------|
| Data Source | `IDBInitialize`, `IDBProperties`, `IDBCreateSession`, `IPersist`, `IDBInfo` |
| Session     | `IOpenRowset`, `IGetDataSource`, `IDBCreateCommand`, `IDBSchemaRowset`, `ISessionProperties`, `ITransactionJoin`, `ITransactionLocal` |
| Command     | `ICommandText`, `ICommandProperties`, `IColumnsInfo`, `IAccessor`, `IConvertType`, `ICommandPrepare` |
| Rowset      | `IRowset`, `IRowsetChange`, `IAccessor`, `IColumnsInfo`, `IRowsetInfo`, `IConvertType` |

### Navigazione catalogo (`IDBSchemaRowset`)

Il modulo `schema.c` implementa 13 schema rowset che permettono a SSMS di costruire il catalogo del linked server:

| schema | cosa mostra in SSMS | tabella MonetDB |
|--------|---------------------|-----------------|
| DBSCHEMA_CATALOGS | nodo catalogo | configurazione INI |
| DBSCHEMA_SCHEMATA | schemi sotto il catalogo | `sys.schemas` |
| DBSCHEMA_TABLES | tabelle per schema | `sys.tables` |
| DBSCHEMA_VIEWS | viste per schema | `sys.tables` |
| DBSCHEMA_COLUMNS | colonne di tabelle/viste | `sys.columns` |
| DBSCHEMA_VIEW_COLUMN_USAGE | colonne delle viste | `sys.columns` |
| DBSCHEMA_PROCEDURES | funzioni/procedure | `sys.functions` |
| DBSCHEMA_PROCEDURE_PARAMETERS | parametri procedure | `sys.args` |
| DBSCHEMA_PRIMARY_KEYS | chiavi primarie | `sys.keys` |
| DBSCHEMA_FOREIGN_KEYS | chiavi esterne | `sys.keys` |
| DBSCHEMA_INDEXES | indici | `sys.idxs` + `sys.objects` |
| DBSCHEMA_PROVIDER_TYPES | tipi dati supportati | `sys.types` |
| DBSCHEMA_TABLE_STATISTICS | statistiche tabelle | `sys.tables` |

## Struttura del progetto

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
├── docs/
│   ├── DOCUMENTAZIONE.it.md
│   └── DOCUMENTATION.en.md
├── scripts/
│   ├── register.bat
│   ├── reregister_here.bat
│   ├── setup_linkedserver.sql
│   ├── diagnose_provider.ps1
│   ├── unlock_provider.ps1
│   ├── deploy_137_admin.ps1
│   └── widen_varchar_2000.ps1
├── tools/
│   ├── probe_msdainitialize.c
│   ├── probe_query_fetch.c
│   ├── probe_rowsetchange.c
│   └── probe_schema_rowsets.c
├── monetdb_oledb.def
├── monetdb_oledb.sln / .vcxproj
├── Makefile
├── LICENSE
└── README.md
```

## Prerequisiti

1. Visual Studio 2022 con workload C/C++.
2. Windows SDK 10.0+.
3. Driver ODBC MonetDB installato.
4. SQL Server 2016+.
5. SSMS 19+.

## Build

Dal **Developer Command Prompt x64**:

```cmd
nmake
nmake DEBUG=1
nmake clean
```

Oppure aprire `monetdb_oledb.sln` in Visual Studio 2022 e compilare `x64`.

## Installazione

1. Creare un DSN ODBC di sistema 64-bit chiamato `MonetDB`.
2. Aggiornare `config\monetdb_oledb.ini`.
3. Eseguire:

```cmd
nmake install
```

`nmake install` prova prima a chiudere gli eventuali surrogate COM `dllhost.exe` che tengono aperta la DLL. Se la DLL è ancora caricata da `sqlservr.exe` o da SSMS, l'installazione si ferma con un messaggio esplicito invece del generico errore "file in uso".

Oppure copiare DLL/INI e registrare manualmente:

```cmd
powershell -ExecutionPolicy Bypass -File scripts\unlock_provider.ps1 -Path C:\MonetDB_OleDb\monetdb_oledb.dll
copy build\Release\monetdb_oledb.dll C:\MonetDB_OleDb\
copy config\monetdb_oledb.ini        C:\MonetDB_OleDb\
regsvr32 C:\MonetDB_OleDb\monetdb_oledb.dll
C:\MonetDB_OleDb\register.bat
```

Per i carichi voluminosi il provider supporta due knob di prefetch nel file INI:

```ini
FetchRows=256
FetchWindowKB=1204
```

`FetchRows` controlla quante righe il rowset prova a prefetchare in un batch e viene usato anche come dimensione batch per gli insert via `IRowsetChange::InsertRow`; `FetchWindowKB` limita sia la memoria della finestra buffered sia la dimensione massima di una `INSERT ... VALUES (...), ...` multi-riga. Con `Trace=1` il log riporta anche `fetch_rows`, `fetch_window_kb`, `elapsed_us`, `avg_row_bytes`, `rows_per_sec` per ogni prefetch e `Rowset::InsertBatch` per gli insert batch.

## Utilizzo da SQL Server

```sql
SELECT * FROM OPENQUERY(MONETDB_LS, 'SELECT * FROM sys.tables LIMIT 10');
SELECT * FROM [MONETDB_LS]...[sys].[tables];
EXEC ('SELECT COUNT(*) FROM myschema.mytable') AT MONETDB_LS;
```

## Logging e diagnostica

Il provider scrive:

- bootstrap log da `DllMain`, prima dell'inizializzazione completa;
- log operativo con timestamp, PID e TID;
- tracing COM quando `Trace=1`.

Script utili:

```cmd
scripts\register.bat /check
```

```powershell
powershell -ExecutionPolicy Bypass -File scripts\diagnose_provider.ps1
```

`diagnose_provider.ps1` mostra anche i processi che tengono aperta `monetdb_oledb.dll`. Se compare `dllhost.exe /Processid:{2206CDB0-19C1-11D1-89E0-00C04FD7A829}`, si tratta del surrogate COM `MSDAINITIALIZE` del layer OLE DB.
Mostra inoltre le ultime righe rilevanti dell'`ERRORLOG` di SQL Server per `MonetDB`, `OLE DB` e gli errori più comuni dei linked server.

## Note tecniche

- Threading model: `Both`
- CLSID fisso: `{A3F2D8E1-7B4C-4E9A-B5D6-1C8F3E2A9D07}`
- ProgID: `MonetDB.OleDb.1` e `MonetDB.OleDb`
- `INITGUID` e `DBINITCONSTANTS` sono definiti solo in `monetdb_oledb_main.c`
- La connessione ODBC è aperta su `IDBInitialize::Initialize`
- Ogni `Session` riusa la stessa `SQLHDBC` del data source

## Stato del codice

Questa repository contiene una base completa e coerente per Visual Studio 2022 con:

- registrazione COM della DLL;
- caricamento config INI;
- logging bootstrap/operativo;
- apertura ODBC verso MonetDB;
- oggetti `DataSource`, `Session`, `Command`, `Rowset`;
- 13 schema rowset per catalog browsing;
- script di setup, diagnostica e linked server.

Prima della messa in produzione è consigliato validare e rifinire:

- le query esatte sul catalogo MonetDB in base alla versione del server;
- le conversioni tipo più complesse;
- la compatibilità completa con tutti i probe effettuati da SQL Server/OLE DB services.

## Licenza

Questo progetto è distribuito con licenza [MIT](LICENSE).

Copyright (c) 2026 Umberto Meglio
