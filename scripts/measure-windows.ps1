# Runs the Release build and checks it against docs/performance.md.
# Usage: scripts\measure-windows.ps1 [-Window 30]   Exits non-zero if a budget is exceeded.
param([int]$Window = 30, [int]$Settle = 10)
$ErrorActionPreference = 'Stop'

# Provisional: this has never been measured. Tighten it after the first real run.
$MaxPrivateMB = 30
$MaxCpuMsPerMin = 20

$exe = Join-Path $PSScriptRoot '..\windows\build\Release\Noitch.exe'
$process = Start-Process -FilePath $exe -PassThru
try {
    Start-Sleep -Seconds $Settle
    $process.Refresh()
    $cpuBefore = $process.TotalProcessorTime.TotalMilliseconds
    Start-Sleep -Seconds $Window
    $process.Refresh()

    $private = $process.PrivateMemorySize64 / 1MB
    $workingSet = $process.WorkingSet64 / 1MB
    $cpu = ($process.TotalProcessorTime.TotalMilliseconds - $cpuBefore) * 60 / $Window
    $binary = (Get-Item $exe).Length / 1KB

    '{0,-14}{1:N0} KB' -f 'binary', $binary
    '{0,-14}{1:N2} MB        (budget {2})' -f 'private', $private, $MaxPrivateMB
    '{0,-14}{1:N2} MB        (report only: includes shared GPU driver pages)' -f 'working set', $workingSet
    '{0,-14}{1:N2} ms/min    (budget {2})' -f 'idle CPU', $cpu, $MaxCpuMsPerMin

    if ($private -gt $MaxPrivateMB -or $cpu -gt $MaxCpuMsPerMin) { exit 1 }
}
finally {
    Stop-Process -Id $process.Id -ErrorAction SilentlyContinue
}
