# MonetDB OLE DB Provider — Documentazione

> 🇬🇧 [English version](DOCUMENTATION.en.md)

**Versione:** 1.0.52-varchar2000
**Autore:** Umberto Meglio
**Licenza:** [MIT](../LICENSE)

---

## Indice

1. [Introduzione](#1-introduzione)
2. [Architettura](#2-architettura)
3. [Oggetti COM e interfacce](#3-oggetti-com-e-interfacce)
4. [Schema rowset e navigazione catalogo](#4-schema-rowset-e-navigazione-catalogo)
5. [Configurazione (file INI)](#5-configurazione-file-ini)
6. [Build](#6-build)
7. [Installazione e registrazione](#7-installazione-e-registrazione)
8. [Configurazione del Linked Server](#8-configurazione-del-linked-server)
9. [Utilizzo da SQL Server](#9-utilizzo-da-sql-server)
10. [Prestazioni e tuning](#10-prestazioni-e-tuning)
11. [Insert batch (`IRowsetChange`)](#11-insert-batch-irowsetchange)
12. [Logging e diagnostica](#12-logging-e-diagnostica)
13. [Strumenti di test (probe)](#13-strumenti-di-test-probe)
14. [Risoluzione dei problemi](#14-risoluzione-dei-problemi)
15. [Note tecniche](#15-note-tecniche)
16. [Struttura del codice sorgente](#16-struttura-del-codice-sorgente)
17. [Licenza](#17-licenza)

---

## 1. Introduzione

MonetDB OLE DB Provider è un provider OLE DB nativo a 64 bit, scritto interamente in ANSI C, che fa da ponte tra Microsoft SQL Server e MonetDB attraverso il driver ODBC ufficiale di MonetDB.

Il provider si registra nel sistema COM di Windows come DLL in-process (`InprocServer32`) e appare come opzione selezionabile nella configurazione dei Linked Server di SQL Server Management Studio (SSMS). Consente quindi di:

- interrogare tabelle e viste MonetDB direttamente da T-SQL (`OPENQUERY`, nomi a quattro parti, `EXEC ... AT`);
- esplorare il catalogo MonetDB (schemi, tabelle, viste, colonne, chiavi, indici) dall'albero degli oggetti di SSMS;
- inserire dati in MonetDB tramite `IRowsetChange::InsertRow` con batching automatico.

### Prerequisiti

| Componente | Versione minima |
|------------|-----------------|
| Visual Studio | 2022 con workload C/C++ |
| Windows SDK | 10.0+ |
| Driver ODBC MonetDB | installato come driver di sistema a 64 bit |
| SQL Server | 2016+ |
| SSMS | 19+ |

## 2. Architettura

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

Il flusso di una query è il seguente:

1. SQL Server istanzia il provider via COM (CLSID `{A3F2D8E1-7B4C-4E9A-B5D6-1C8F3E2A9D07}`).
2. `IDBInitialize::Initialize` carica il file INI e apre la connessione ODBC verso MonetDB (una `SQLHDBC` per data source, riusata da tutte le sessioni).
3. La `Session` crea un `Command` (`IDBCreateCommand`) oppure apre direttamente una tabella (`IOpenRowset`).
4. Il `Command` esegue l'SQL via `SQLExecDirect`/`SQLPrepare` e restituisce un `Rowset`.
5. Il `Rowset` prefetcha le righe in una finestra buffered e le espone a SQL Server tramite `IRowset::GetNextRows` e gli accessor OLE DB.

## 3. Oggetti COM e interfacce

| Oggetto COM | Interfacce implementate |
|-------------|-------------------------|
| Data Source | `IDBInitialize`, `IDBProperties`, `IDBCreateSession`, `IPersist`, `IDBInfo`, `ISupportErrorInfo` |
| Session     | `IOpenRowset`, `IGetDataSource`, `IDBCreateCommand`, `IDBSchemaRowset`, `ISessionProperties`, `ITransactionJoin`, `ITransactionLocal`, `ISupportErrorInfo` |
| Command     | `ICommandText`, `ICommandProperties`, `IColumnsInfo`, `IAccessor`, `IConvertType`, `ICommandPrepare`, `ISupportErrorInfo` |
| Rowset      | `IRowset`, `IRowsetChange`, `IAccessor`, `IColumnsInfo`, `IRowsetInfo`, `IConvertType`, `ISupportErrorInfo` |

Note implementative:

- Data Source e Session supportano l'aggregazione COM (outer unknown).
- Il threading model registrato è `Both`; le strutture condivise (tabella accessor, configurazione) sono protette da `CRITICAL_SECTION`.
- `ITransactionLocal`/`ITransactionJoin` espongono il supporto transazionale locale basato sull'autocommit ODBC.

## 4. Schema rowset e navigazione catalogo

Il modulo `schema.c` implementa i 13 schema rowset richiesti da SSMS per costruire l'albero del catalogo del linked server. Ogni schema rowset viene tradotto in una query sul catalogo di sistema di MonetDB:

| Schema rowset | Cosa mostra in SSMS | Sorgente MonetDB |
|---------------|--------------------|------------------|
| `DBSCHEMA_CATALOGS` | nodo catalogo | configurazione INI |
| `DBSCHEMA_SCHEMATA` | schemi sotto il catalogo | `sys.schemas` |
| `DBSCHEMA_TABLES` | tabelle per schema | `sys.tables` |
| `DBSCHEMA_VIEWS` | viste per schema | `sys.tables` |
| `DBSCHEMA_COLUMNS` | colonne di tabelle/viste | `sys.columns` |
| `DBSCHEMA_VIEW_COLUMN_USAGE` | colonne delle viste | `sys.columns` |
| `DBSCHEMA_PROCEDURES` | funzioni/procedure | `sys.functions` |
| `DBSCHEMA_PROCEDURE_PARAMETERS` | parametri delle procedure | `sys.args` |
| `DBSCHEMA_PRIMARY_KEYS` | chiavi primarie | `sys.keys` |
| `DBSCHEMA_FOREIGN_KEYS` | chiavi esterne | `sys.keys` |
| `DBSCHEMA_INDEXES` | indici | `sys.idxs` + `sys.objects` |
| `DBSCHEMA_PROVIDER_TYPES` | tipi dati supportati | `sys.types` |
| `DBSCHEMA_TABLE_STATISTICS` | statistiche tabelle | `sys.tables` |

Le restrizioni (catalogo, schema, nome tabella) passate da SSMS vengono applicate come filtri `WHERE` nelle query generate (`Schema_BuildSql`).

## 5. Configurazione (file INI)

Il provider legge la configurazione dal file `monetdb_oledb.ini`, sezione `[MonetDB]`, cercato nella directory della DLL. Chiavi supportate:

| Chiave | Default | Descrizione |
|--------|---------|-------------|
| `DSN` | `MonetDB` | Nome del DSN ODBC di sistema (64 bit) verso MonetDB |
| `Database` | — | Database/catalogo MonetDB |
| `Schema` | `sys` | Schema predefinito |
| `User` | — | Utente MonetDB (può essere sovrascritto dal linked server) |
| `Password` | — | Password (può essere sovrascritta dal linked server) |
| `ConnectionTimeout` | `30` | Timeout di connessione in secondi |
| `QueryTimeout` | `120` | Timeout query in secondi |
| `ReadOnly` | `0` | `1` = apre la connessione in sola lettura |
| `AutoCommit` | `1` | `1` = autocommit ODBC attivo |
| `FetchRows` | `256` | Righe prefetchate per batch (min 1, max 4096) |
| `FetchWindowKB` | `1204` | Limite memoria della finestra buffered in KB (min 64, max 16384) |
| `LogFile` | `monetdb_oledb.log` | Percorso del file di log |
| `LogLevel` | — | `1`=ERROR, `2`=INFO, `3`=DEBUG, `4`=TRACE |
| `Trace` | `0` | `1` = abilita il tracing dettagliato delle chiamate COM |

Esempio:

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

> ⚠️ **Sicurezza:** il file INI può contenere credenziali in chiaro. Limitare i permessi NTFS della directory di installazione e preferire, dove possibile, le credenziali configurate a livello di linked server (`sp_addlinkedsrvlogin`).

## 6. Build

### Con NMAKE

Dal **Developer Command Prompt for VS 2022 (x64)**:

```cmd
nmake                 :: build Release (build\Release\monetdb_oledb.dll)
nmake DEBUG=1         :: build Debug con simboli
nmake clean           :: rimuove la directory build
```

La build Release usa `/O2 /GL /MT` con link `/LTCG`; la Debug usa `/Od /Zi /MTd`.

### Con Visual Studio

Aprire `monetdb_oledb.sln` in Visual Studio 2022 e compilare la configurazione `x64`.

Le librerie collegate sono: `odbc32.lib`, `odbccp32.lib`, `ole32.lib`, `oleaut32.lib`, `advapi32.lib`, `uuid.lib`. Gli export COM (`DllGetClassObject`, `DllCanUnloadNow`, `DllRegisterServer`, `DllUnregisterServer`) sono dichiarati in `monetdb_oledb.def`.

## 7. Installazione e registrazione

### Installazione automatica

1. Creare un DSN ODBC **di sistema a 64 bit** chiamato `MonetDB` (o il nome configurato in `DSN=`).
2. Aggiornare `config\monetdb_oledb.ini` con i parametri di connessione.
3. Da Developer Command Prompt x64 **con privilegi di amministratore**:

```cmd
nmake install
```

`nmake install`:

- esegue `unlock_provider.ps1` per chiudere gli eventuali surrogate COM `dllhost.exe` che tengono aperta la DLL (se la DLL è caricata da `sqlservr.exe` o SSMS, si ferma con un messaggio esplicito invece del generico "file in uso");
- copia DLL, INI e script in `C:\MonetDB_OleDb`;
- registra la DLL con `regsvr32 /s`;
- esegue `register.bat` per creare le chiavi provider di SQL Server.

`nmake uninstall` deregistra la DLL e rimuove la directory di installazione.

### Installazione manuale

```cmd
powershell -ExecutionPolicy Bypass -File scripts\unlock_provider.ps1 -Path C:\MonetDB_OleDb\monetdb_oledb.dll
copy build\Release\monetdb_oledb.dll C:\MonetDB_OleDb\
copy config\monetdb_oledb.ini        C:\MonetDB_OleDb\
regsvr32 C:\MonetDB_OleDb\monetdb_oledb.dll
C:\MonetDB_OleDb\register.bat
```

### Cosa fa `register.bat`

Per l'istanza SQL Server (e per ogni istanza rilevata dal registro) crea la chiave `Providers\MonetDB.OleDb` con:

| Valore | Impostazione |
|--------|--------------|
| `AllowInProcess` | `1` |
| `DynamicParameters` | `1` |
| `NestedQueries` | `1` |
| `LevelZeroOnly` | `0` |

Concede inoltre il permesso *Modify* a `NT SERVICE\ALL SERVICES` sulla directory di installazione, in modo che il servizio SQL Server possa leggere INI e scrivere i log.

Opzioni: `register.bat /check` verifica lo stato della registrazione; `register.bat /u` rimuove le chiavi.

## 8. Configurazione del Linked Server

Lo script `scripts\setup_linkedserver.sql` crea un linked server chiamato `MONETDB_LS`. Prima di eseguirlo, adattare le variabili in testa allo script:

```sql
DECLARE @LinkedServerName NVARCHAR(128) = N'MONETDB_LS';
DECLARE @DataSource       NVARCHAR(256) = N'MonetDB';   -- DSN ODBC
DECLARE @Catalog          NVARCHAR(128) = N'demo';      -- database MonetDB
DECLARE @RemoteUser       NVARCHAR(128) = N'monetdb';
DECLARE @RemotePassword   NVARCHAR(128) = N'...';
```

Lo script:

1. elimina l'eventuale linked server esistente con lo stesso nome;
2. crea il linked server con `@provider = N'MonetDB.OleDb'`;
3. mappa le credenziali remote con `sp_addlinkedsrvlogin`;
4. imposta le opzioni `data access`, `rpc out`, `use remote collation`, `connect timeout`, `query timeout`.

## 9. Utilizzo da SQL Server

```sql
-- Pass-through (consigliato): la query è eseguita da MonetDB
SELECT * FROM OPENQUERY(MONETDB_LS, 'SELECT * FROM sys.tables LIMIT 10');

-- Nome a quattro parti: SQL Server usa gli schema rowset e il rowset del provider
SELECT * FROM [MONETDB_LS]...[sys].[tables];

-- Esecuzione remota
EXEC ('SELECT COUNT(*) FROM myschema.mytable') AT MONETDB_LS;

-- Insert remoto (usa IRowsetChange::InsertRow con batching)
INSERT INTO [MONETDB_LS]...[myschema].[mytable] (col1, col2) VALUES (1, 'abc');
```

Suggerimenti:

- Con `OPENQUERY` il filtro e l'aggregazione vengono eseguiti da MonetDB: è quasi sempre la via più efficiente.
- Con i nomi a quattro parti SQL Server può scaricare l'intera tabella e filtrare localmente; usarli soprattutto per l'esplorazione.
- La sintassi SQL dentro `OPENQUERY`/`EXEC ... AT` è quella di MonetDB (es. `LIMIT` invece di `TOP`).

## 10. Prestazioni e tuning

Due parametri INI controllano il prefetch delle righe:

```ini
FetchRows=256
FetchWindowKB=1204
```

- **`FetchRows`** — quante righe il rowset prova a prefetchare per batch (limiti: 1–4096). È usato anche come dimensione batch per gli insert via `IRowsetChange::InsertRow`.
- **`FetchWindowKB`** — limita sia la memoria della finestra buffered sia la dimensione massima di una `INSERT ... VALUES (...), ...` multi-riga (limiti: 64–16384 KB).

Con `Trace=1` il log riporta per ogni prefetch: `fetch_rows`, `fetch_window_kb`, `elapsed_us`, `avg_row_bytes`, `rows_per_sec` — utile per calibrare i valori sul proprio carico. Per i carichi voluminosi conviene partire da `FetchRows=256` e `FetchWindowKB=1204` e aumentare gradualmente osservando `rows_per_sec`.

Le stringhe fino a 2000 caratteri (`MONETDB_MAX_FAST_STRING_CHARS`) usano il percorso di fetch veloce a buffer fisso; oltre questa soglia il valore viene letto a chunk (`SQLGetData`).

## 11. Insert batch (`IRowsetChange`)

Quando SQL Server inserisce righe attraverso il linked server, il provider accumula le righe in un buffer e le invia a MonetDB come `INSERT ... VALUES (...), (...), ...` multi-riga:

- la dimensione del batch è governata da `FetchRows`;
- la dimensione massima dell'istruzione SQL generata è limitata da `FetchWindowKB`;
- il flush avviene automaticamente al riempimento del batch e alla chiusura del rowset;
- con `Trace=1` ogni flush è tracciato nel log come `Rowset::InsertBatch` con righe bufferizzate/flushate e conteggio flush.

## 12. Logging e diagnostica

Il provider scrive tre tipi di log:

1. **Bootstrap log** — scritto da `DllMain` prima che la configurazione sia caricata; utile quando il provider non riesce nemmeno a inizializzarsi.
2. **Log operativo** — file configurato con `LogFile`, con timestamp, PID e TID per ogni riga; il livello di dettaglio è governato da `LogLevel` (1=ERROR … 4=TRACE).
3. **Tracing COM** — con `Trace=1` vengono tracciate le chiamate alle interfacce COM, le query eseguite e le statistiche di fetch/insert.

### Script di diagnostica

```cmd
scripts\register.bat /check
```

verifica la registrazione COM e le chiavi provider di SQL Server.

```powershell
powershell -ExecutionPolicy Bypass -File scripts\diagnose_provider.ps1
```

`diagnose_provider.ps1` mostra:

- lo stato della registrazione del provider;
- i processi che tengono aperta `monetdb_oledb.dll`;
- le ultime righe rilevanti dell'`ERRORLOG` di SQL Server per `MonetDB`, `OLE DB` e gli errori più comuni dei linked server.

Se tra i processi compare `dllhost.exe /Processid:{2206CDB0-19C1-11D1-89E0-00C04FD7A829}`, si tratta del surrogate COM `MSDAINITIALIZE` del layer OLE DB: `unlock_provider.ps1` lo chiude prima di sostituire la DLL.

### Altri script

| Script | Scopo |
|--------|-------|
| `unlock_provider.ps1` | chiude i processi surrogate che bloccano la DLL prima di una nuova copia |
| `reregister_here.bat` | ri-registra la DLL dalla directory corrente |
| `deploy_137_admin.ps1` | script di deploy verso un server specifico (esempio di deploy remoto) |
| `widen_varchar_2000.ps1` | utilità legata alla gestione varchar a 2000 caratteri |

## 13. Strumenti di test (probe)

La directory `tools/` contiene quattro programmi standalone che esercitano il provider senza SQL Server:

| Probe | Cosa verifica |
|-------|---------------|
| `probe_msdainitialize.c` | istanziazione del provider attraverso `MSDAINITIALIZE` (stesso percorso usato da SQL Server) |
| `probe_query_fetch.c` | esecuzione di una query e fetch delle righe via `IRowset` |
| `probe_rowsetchange.c` | insert via `IRowsetChange::InsertRow` e batching |
| `probe_schema_rowsets.c` | i 13 schema rowset del catalogo |

Sono utili per isolare i problemi: se un probe funziona ma il linked server no, il problema è nella configurazione SQL Server (permessi, `AllowInProcess`, credenziali) e non nel provider.

## 14. Risoluzione dei problemi

| Sintomo | Causa probabile | Rimedio |
|---------|-----------------|---------|
| Il provider non appare in SSMS tra i provider dei linked server | DLL non registrata o registrata a 32 bit | `regsvr32` dalla copia a 64 bit; `register.bat /check` |
| Errore 7302 "Cannot create an instance of OLE DB provider" | chiavi provider mancanti o permessi | eseguire `register.bat` come amministratore; verificare `AllowInProcess=1` |
| Errore 7303 "Cannot initialize the data source object" | DSN ODBC errato/mancante, credenziali, MonetDB irraggiungibile | verificare il DSN di sistema a 64 bit e il file INI; consultare il log |
| "File in uso" durante l'installazione | DLL caricata da `dllhost.exe`, `sqlservr.exe` o SSMS | `unlock_provider.ps1`; se necessario riavviare SQL Server / chiudere SSMS |
| Query lente su tabelle grandi | prefetch troppo piccolo o filtri lato SQL Server | usare `OPENQUERY`; alzare `FetchRows`/`FetchWindowKB` |
| Nessun log prodotto | il servizio SQL Server non può scrivere nella directory | `register.bat` concede *Modify* a `NT SERVICE\ALL SERVICES`; verificare i permessi NTFS |

Per l'analisi fine, impostare `LogLevel=4` e `Trace=1`, riprodurre il problema e leggere il log operativo (ogni riga contiene timestamp, PID e TID).

## 15. Note tecniche

- Threading model: `Both`
- CLSID fisso: `{A3F2D8E1-7B4C-4E9A-B5D6-1C8F3E2A9D07}`
- ProgID: `MonetDB.OleDb.1` e `MonetDB.OleDb`
- `INITGUID` e `DBINITCONSTANTS` sono definiti solo in `monetdb_oledb_main.c`
- La connessione ODBC è aperta in `IDBInitialize::Initialize`
- Ogni `Session` riusa la stessa `SQLHDBC` del data source
- Lunghezza massima del testo SQL: 32768 caratteri (`MONETDB_MAX_SQL_TEXT`)
- Runtime statico (`/MT`): nessuna dipendenza dal redistributable VC++

## 16. Struttura del codice sorgente

| File | Responsabilità |
|------|----------------|
| `src/monetdb_oledb_main.c` | `DllMain`, class factory, registrazione COM, export della DLL |
| `src/config.c` | caricamento del file INI, logging (bootstrap e operativo) |
| `src/odbc_utils.c` | ambiente/connessioni ODBC, esecuzione statement, mapping tipi SQL→DBTYPE, messaggi d'errore |
| `src/datasource.c` | oggetto Data Source: proprietà di inizializzazione, apertura connessione |
| `src/session.c` | oggetto Session: creazione command, open rowset, transazioni |
| `src/command.c` | oggetto Command: testo SQL, prepare, proprietà, colonne |
| `src/rowset.c` | oggetto Rowset: fetch buffered, accessor, conversioni, insert batch |
| `src/schema.c` | i 13 schema rowset e la generazione delle query di catalogo |
| `include/monetdb_oledb.h` | strutture, costanti e prototipi condivisi |

## 17. Licenza

Questo progetto è distribuito con licenza **MIT** — vedere il file [LICENSE](../LICENSE).

Copyright (c) 2026 Umberto Meglio
