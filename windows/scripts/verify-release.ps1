param([Parameter(Mandatory)][string]$Path)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
foreach ($name in @('RELEASE_VERSION', 'WINDOWS_SIGNING_THUMBPRINT')) {
    if (-not [Environment]::GetEnvironmentVariable($name)) { throw "Missing $name" }
}
$signature = Get-AuthenticodeSignature -LiteralPath $Path
if ($signature.Status -ne 'Valid' -or -not $signature.TimeStamperCertificate -or
    $signature.SignerCertificate.Thumbprint -ne $env:WINDOWS_SIGNING_THUMBPRINT) {
    throw 'Authenticode identity, trust or timestamp verification failed'
}
$info = (Get-Item -LiteralPath $Path).VersionInfo
if ($info.ProductVersion -ne $env:RELEASE_VERSION -or $info.FileVersion -ne "$env:RELEASE_VERSION.0") {
    throw 'Incorrect executable version'
}
$bytes = [IO.File]::ReadAllBytes((Resolve-Path $Path))
$pe = [BitConverter]::ToInt32($bytes, 0x3c)
if ([BitConverter]::ToUInt32($bytes, $pe) -ne 0x4550 -or
    [BitConverter]::ToUInt16($bytes, $pe + 4) -ne 0x8664) { throw 'Expected x64 PE executable' }
$signtool = Get-ChildItem "${env:ProgramFiles(x86)}\Windows Kits\10\bin\*\x64\signtool.exe" |
    Sort-Object FullName -Descending | Select-Object -First 1
if (-not $signtool) { throw 'Windows SDK SignTool missing' }
& $signtool.FullName verify /pa /all /v $Path
if ($LASTEXITCODE -ne 0) { throw 'SignTool verification failed' }
