param(
    [string]$Dsn = "MonetDB",
    [string]$User = "monetdb",
    [string]$Password = "S1p@$",
    [string]$Schema = "sys",
    [Parameter(Mandatory = $true)]
    [string]$Table,
    [int]$Length = 2000,
    [switch]$Execute
)

Add-Type -AssemblyName System.Data

$connectionString = "DSN=$Dsn;UID=$User;PWD=$Password;"
$connection = New-Object System.Data.Odbc.OdbcConnection($connectionString)
$connection.Open()

try {
    $command = $connection.CreateCommand()
    $command.CommandText = @"
select s.name as schema_name, t.name as table_name, c.name as column_name, c.type_digits
from sys.columns c
join sys.tables t on t.id = c.table_id
join sys.schemas s on s.id = t.schema_id
where lower(s.name) = lower(?)
  and lower(t.name) = lower(?)
  and lower(c.type) = 'varchar'
  and coalesce(c.type_digits, 0) > 0
  and c.type_digits < ?
order by c.number
"@
    [void]$command.Parameters.Add("schema", [System.Data.Odbc.OdbcType]::VarChar, 256)
    [void]$command.Parameters.Add("table", [System.Data.Odbc.OdbcType]::VarChar, 256)
    [void]$command.Parameters.Add("length", [System.Data.Odbc.OdbcType]::Int)
    $command.Parameters[0].Value = $Schema
    $command.Parameters[1].Value = $Table
    $command.Parameters[2].Value = $Length

    $reader = $command.ExecuteReader()
    $alterStatements = New-Object System.Collections.Generic.List[string]
    while ($reader.Read()) {
        $schemaName = [string]$reader["schema_name"]
        $tableName = [string]$reader["table_name"]
        $columnName = [string]$reader["column_name"]
        $currentLength = [int]$reader["type_digits"]
        $sql = 'alter table "{0}"."{1}" alter column "{2}" varchar({3})' -f
            $schemaName.Replace('"', '""'),
            $tableName.Replace('"', '""'),
            $columnName.Replace('"', '""'),
            $Length
        $alterStatements.Add($sql)
        Write-Output ("-- {0}.{1}.{2}: varchar({3}) -> varchar({4})" -f $schemaName, $tableName, $columnName, $currentLength, $Length)
        Write-Output ($sql + ";")
    }
    $reader.Close()

    if ($alterStatements.Count -eq 0) {
        Write-Output "-- Nessuna colonna varchar sotto la soglia trovata per $Schema.$Table"
        return
    }

    if ($Execute) {
        foreach ($sql in $alterStatements) {
            $exec = $connection.CreateCommand()
            $exec.CommandText = $sql
            [void]$exec.ExecuteNonQuery()
            Write-Output ("-- Eseguito: {0}" -f $sql)
        }
    }
    else {
        Write-Output "-- Dry-run: aggiungi -Execute per applicare davvero gli ALTER."
    }
}
finally {
    $connection.Close()
}
