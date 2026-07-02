!IF "$(DEBUG)" == "1"
CFG=Debug
RUNTIME=/MTd
OPTFLAGS=/Od /Zi
DEFINES=/DDEBUG /D_DEBUG
LDFLAGS=/DEBUG
!ELSE
CFG=Release
RUNTIME=/MT
OPTFLAGS=/O2 /GL /DNDEBUG
DEFINES=/DNDEBUG
LDFLAGS=/LTCG
!ENDIF

PROJECT=monetdb_oledb
OUTDIR=build\$(CFG)
INSTALLDIR=C:\MonetDB_OleDb
INCLUDES=/Iinclude
CFLAGS=/nologo /W4 /TC /EHsc- /GS /Zc:wchar_t /DWIN32 /D_WINDOWS /D_USRDLL /D_CRT_SECURE_NO_WARNINGS $(DEFINES) $(RUNTIME) $(OPTFLAGS) $(INCLUDES)
LINKFLAGS=/nologo /DLL /DEF:monetdb_oledb.def /OUT:$(OUTDIR)\$(PROJECT).dll $(LDFLAGS) odbc32.lib odbccp32.lib ole32.lib oleaut32.lib advapi32.lib uuid.lib

SOURCES= \
    src\monetdb_oledb_main.c \
    src\config.c \
    src\odbc_utils.c \
    src\datasource.c \
    src\session.c \
    src\command.c \
    src\rowset.c \
    src\schema.c

OBJS= \
    $(OUTDIR)\monetdb_oledb_main.obj \
    $(OUTDIR)\config.obj \
    $(OUTDIR)\odbc_utils.obj \
    $(OUTDIR)\datasource.obj \
    $(OUTDIR)\session.obj \
    $(OUTDIR)\command.obj \
    $(OUTDIR)\rowset.obj \
    $(OUTDIR)\schema.obj

all: $(OUTDIR)\$(PROJECT).dll

$(OUTDIR):
	@if not exist "$(OUTDIR)" mkdir "$(OUTDIR)"

$(OUTDIR)\monetdb_oledb_main.obj: src\monetdb_oledb_main.c include\monetdb_oledb.h
	@if not exist "$(OUTDIR)" mkdir "$(OUTDIR)"
	cl $(CFLAGS) /Fo$@ /c src\monetdb_oledb_main.c

$(OUTDIR)\config.obj: src\config.c include\monetdb_oledb.h
	@if not exist "$(OUTDIR)" mkdir "$(OUTDIR)"
	cl $(CFLAGS) /Fo$@ /c src\config.c

$(OUTDIR)\odbc_utils.obj: src\odbc_utils.c include\monetdb_oledb.h
	@if not exist "$(OUTDIR)" mkdir "$(OUTDIR)"
	cl $(CFLAGS) /Fo$@ /c src\odbc_utils.c

$(OUTDIR)\datasource.obj: src\datasource.c include\monetdb_oledb.h
	@if not exist "$(OUTDIR)" mkdir "$(OUTDIR)"
	cl $(CFLAGS) /Fo$@ /c src\datasource.c

$(OUTDIR)\session.obj: src\session.c include\monetdb_oledb.h
	@if not exist "$(OUTDIR)" mkdir "$(OUTDIR)"
	cl $(CFLAGS) /Fo$@ /c src\session.c

$(OUTDIR)\command.obj: src\command.c include\monetdb_oledb.h
	@if not exist "$(OUTDIR)" mkdir "$(OUTDIR)"
	cl $(CFLAGS) /Fo$@ /c src\command.c

$(OUTDIR)\rowset.obj: src\rowset.c include\monetdb_oledb.h
	@if not exist "$(OUTDIR)" mkdir "$(OUTDIR)"
	cl $(CFLAGS) /Fo$@ /c src\rowset.c

$(OUTDIR)\schema.obj: src\schema.c include\monetdb_oledb.h
	@if not exist "$(OUTDIR)" mkdir "$(OUTDIR)"
	cl $(CFLAGS) /Fo$@ /c src\schema.c

$(OUTDIR)\$(PROJECT).dll: $(OBJS)
	link $(LINKFLAGS) $(OBJS)

clean:
	@if exist build rmdir /s /q build

install: all
	@if not exist "$(INSTALLDIR)" mkdir "$(INSTALLDIR)"
	powershell -NoProfile -ExecutionPolicy Bypass -File "scripts\unlock_provider.ps1" -Path "$(INSTALLDIR)\$(PROJECT).dll"
	copy "$(OUTDIR)\$(PROJECT).dll" "$(INSTALLDIR)\$(PROJECT).dll" >nul
	copy "config\monetdb_oledb.ini" "$(INSTALLDIR)\monetdb_oledb.ini" >nul
	copy "scripts\register.bat" "$(INSTALLDIR)\register.bat" >nul
	copy "scripts\diagnose_provider.ps1" "$(INSTALLDIR)\diagnose_provider.ps1" >nul
	copy "scripts\unlock_provider.ps1" "$(INSTALLDIR)\unlock_provider.ps1" >nul
	copy "scripts\setup_linkedserver.sql" "$(INSTALLDIR)\setup_linkedserver.sql" >nul
	regsvr32 /s "$(INSTALLDIR)\$(PROJECT).dll"
	call "$(INSTALLDIR)\register.bat"

uninstall:
	@if exist "$(INSTALLDIR)\$(PROJECT).dll" regsvr32 /u /s "$(INSTALLDIR)\$(PROJECT).dll"
	@if exist "$(INSTALLDIR)" rmdir /s /q "$(INSTALLDIR)"
