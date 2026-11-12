$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
Set-Location "$PSScriptRoot\.."
foreach ($name in @('RELEASE_VERSION', 'WINDOWS_CERTIFICATE_BASE64', 'WINDOWS_CERTIFICATE_PASSWORD',
    'WINDOWS_SIGNING_THUMBPRINT', 'WINDOWS_TIMESTAMP_URL')) {
    if (-not [Environment]::GetEnvironmentVariable($name)) { throw "Missing $name" }
}
function Invoke-Native([scriptblock]$Command) {
    & $Command
    if ($LASTEXITCODE -ne 0) { throw "Native command failed: $Command" }
}
$temp = Join-Path $env:RUNNER_TEMP ([guid]::NewGuid().ToString())
New-Item -ItemType Directory $temp | Out-Null
$certs = @()
try {
    $pfx = Join-Path $temp 'signing.pfx'
    [IO.File]::WriteAllBytes($pfx, [Convert]::FromBase64String($env:WINDOWS_CERTIFICATE_BASE64))
    $password = ConvertTo-SecureString $env:WINDOWS_CERTIFICATE_PASSWORD -AsPlainText -Force
    $certs = @(Import-PfxCertificate -FilePath $pfx -CertStoreLocation Cert:\CurrentUser\My -Password $password)
    if (-not ($certs | Where-Object { $_.Thumbprint -eq $env:WINDOWS_SIGNING_THUMBPRINT -and $_.HasPrivateKey })) {
        throw 'PFX does not contain the configured signing key'
    }
    Invoke-Native { cmake --preset default -A x64 "-DNOITCH_VERSION=$env:RELEASE_VERSION" }
    Invoke-Native { cmake --build --preset debug }
    Invoke-Native { ctest --preset debug }
    Invoke-Native { cmake --build --preset release }
    New-Item -ItemType Directory -Force build/release | Out-Null
    $asset = "build/release/Noitch-$env:RELEASE_VERSION-windows-x64.exe"
    Copy-Item build/Release/Noitch.exe $asset
    $signtool = Get-ChildItem "${env:ProgramFiles(x86)}\Windows Kits\10\bin\*\x64\signtool.exe" |
        Sort-Object FullName -Descending | Select-Object -First 1
    if (-not $signtool) { throw 'Windows SDK SignTool missing' }
    Invoke-Native { & $signtool.FullName sign /sha1 $env:WINDOWS_SIGNING_THUMBPRINT /s My /fd SHA256 /tr $env:WINDOWS_TIMESTAMP_URL /td SHA256 $asset }
    & "$PSScriptRoot/verify-release.ps1" -Path $asset
    $toolchain = @(& cmake --version; & $signtool.FullName /?; Get-Content build/CMakeCache.txt |
        Select-String 'CMAKE_(CXX_COMPILER|GENERATOR|VS_VERSION|VS_WINDOWS_TARGET_PLATFORM_VERSION)') -join "`n"
    [IO.File]::WriteAllText((Join-Path (Get-Location) 'build/release-toolchain.txt'), $toolchain)
} finally {
    foreach ($cert in $certs) { Remove-Item "Cert:\CurrentUser\My\$($cert.Thumbprint)" -DeleteKey -ErrorAction Continue }
    Remove-Item $temp -Recurse -Force -ErrorAction Continue
}
