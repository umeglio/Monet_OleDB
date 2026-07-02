SET NOCOUNT ON;
GO

DECLARE @LinkedServerName NVARCHAR(128) = N'MONETDB_LS';
DECLARE @DataSource       NVARCHAR(256) = N'MonetDB';
DECLARE @Catalog          NVARCHAR(128) = N'demo';
DECLARE @RemoteUser       NVARCHAR(128) = N'monetdb';
DECLARE @RemotePassword   NVARCHAR(128) = N'monetdb';

IF EXISTS (SELECT 1 FROM sys.servers WHERE name = @LinkedServerName)
BEGIN
    EXEC master.dbo.sp_dropserver @server = @LinkedServerName, @droplogins = 'droplogins';
END
GO

DECLARE @LinkedServerName NVARCHAR(128) = N'MONETDB_LS';
DECLARE @DataSource       NVARCHAR(256) = N'MonetDB';
DECLARE @Catalog          NVARCHAR(128) = N'demo';
DECLARE @RemoteUser       NVARCHAR(128) = N'monetdb';
DECLARE @RemotePassword   NVARCHAR(128) = N'monetdb';

EXEC master.dbo.sp_addlinkedserver
    @server     = @LinkedServerName,
    @srvproduct = N'MonetDB',
    @provider   = N'MonetDB.OleDb',
    @datasrc    = @DataSource,
    @catalog    = @Catalog;

EXEC master.dbo.sp_addlinkedsrvlogin
    @rmtsrvname = @LinkedServerName,
    @useself    = 'False',
    @locallogin = NULL,
    @rmtuser    = @RemoteUser,
    @rmtpassword= @RemotePassword;

EXEC master.dbo.sp_serveroption @server=@LinkedServerName, @optname=N'data access',            @optvalue=N'true';
EXEC master.dbo.sp_serveroption @server=@LinkedServerName, @optname=N'rpc out',                @optvalue=N'true';
EXEC master.dbo.sp_serveroption @server=@LinkedServerName, @optname=N'use remote collation',   @optvalue=N'true';
EXEC master.dbo.sp_serveroption @server=@LinkedServerName, @optname=N'collation compatible',   @optvalue=N'false';
EXEC master.dbo.sp_serveroption @server=@LinkedServerName, @optname=N'connect timeout',        @optvalue=N'30';
EXEC master.dbo.sp_serveroption @server=@LinkedServerName, @optname=N'query timeout',          @optvalue=N'120';
-- EXEC master.dbo.sp_serveroption @server=@LinkedServerName, @optname=N'lazy schema validation', @optvalue=N'true';
GO

PRINT 'Linked server MONETDB_LS creato.';
GO

