param(
    [Parameter(Position = 0, ValueFromRemainingArguments = $true)]
    [string[]]$Command,
    [string]$HostName = "127.0.0.1",
    [int]$Port = 4380,
    [double]$TimeoutSec = 5
)

if (-not $Command -or $Command.Count -eq 0) {
    throw "missing command"
}

$cmd = ($Command -join " ")
$timeoutMs = [Math]::Max(1, [int]($TimeoutSec * 1000))
$client = [System.Net.Sockets.TcpClient]::new()
$reader = $null
$writer = $null

function Read-TcpResponse {
    param(
        [Parameter(Mandatory = $true)]
        [System.Net.Sockets.NetworkStream]$Stream,
        [int]$TimeoutMs
    )

    $buf = [byte[]]::new(8192)
    $sb = [System.Text.StringBuilder]::new()
    $deadline = [DateTime]::UtcNow.AddMilliseconds($TimeoutMs)
    $sawData = $false
    $idleDeadline = $null

    while ([DateTime]::UtcNow -lt $deadline) {
        if ($Stream.DataAvailable) {
            $n = $Stream.Read($buf, 0, $buf.Length)
            if ($n -le 0) { break }
            $sawData = $true
            [void]$sb.Append([System.Text.Encoding]::UTF8.GetString($buf, 0, $n))
            if ($sb.ToString().Contains("`n")) { break }
            $idleDeadline = [DateTime]::UtcNow.AddMilliseconds(100)
            continue
        }

        if ($sawData -and $null -ne $idleDeadline -and [DateTime]::UtcNow -ge $idleDeadline) {
            break
        }
        Start-Sleep -Milliseconds 5
    }

    $text = $sb.ToString()
    if ($text.Contains("`n")) {
        return (($text -split "`n", 2)[0]).TrimEnd("`r")
    }
    return $text.Trim()
}

try {
    $client.Connect($HostName, $Port)
    $stream = $client.GetStream()
    $stream.ReadTimeout = $timeoutMs
    $stream.WriteTimeout = $timeoutMs

    $bytes = [System.Text.Encoding]::UTF8.GetBytes($cmd + "`n")
    $stream.Write($bytes, 0, $bytes.Length)
    $resp = Read-TcpResponse -Stream $stream -TimeoutMs $timeoutMs
    if ($null -ne $resp -and $resp.Length -gt 0) {
        $lines = @($resp -split "`n" | ForEach-Object { $_.TrimEnd("`r") } | Where-Object { $_.Length -gt 0 })
        $commandLines = @($lines | Where-Object { $_ -notmatch '^\{"connected":true,' })
        if ($commandLines.Count -gt 0) {
            Write-Output $commandLines[$commandLines.Count - 1]
        } elseif ($lines.Count -gt 0) {
            Write-Output $lines[$lines.Count - 1]
        }
    }
} finally {
    if ($null -ne $writer) { $writer.Dispose() }
    if ($null -ne $reader) { $reader.Dispose() }
    $client.Close()
}
