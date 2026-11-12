$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
Set-Location "$PSScriptRoot\.."
foreach ($name in @('RELEASE_MODE', 'RELEASE_VERSION')) {
    if (-not [Environment]::GetEnvironmentVariable($name)) { throw "Missing $name" }
}
if ($env:RELEASE_MODE -notin @('signed', 'unsigned')) { throw 'RELEASE_MODE must be signed or unsigned' }
if ($env:RELEASE_MODE -eq 'signed') {
    foreach ($name in @('WINDOWS_CERTIFICATE_BASE64', 'WINDOWS_CERTIFICATE_PASSWORD', 'WINDOWS_SIGNING_THUMBPRINT', 'WINDOWS_TIMESTAMP_URL')) {
        if (-not [Environment]::GetEnvironmentVariable($name)) { throw "Missing $name" }
    }
}
function Invoke-Native([scriptblock]$Command) {
    & $Command
    if ($LASTEXITCODE -ne 0) { throw "Native command failed: $Command" }
}
$temp = $null
$certs = @()
try {
    if ($env:RELEASE_MODE -eq 'signed') {
        $temp = Join-Path $env:RUNNER_TEMP ([guid]::NewGuid().ToString())
        New-Item -ItemType Directory $temp | Out-Null
        $pfx = Join-Path $temp 'signing.pfx'
        [IO.File]::WriteAllBytes($pfx, [Convert]::FromBase64String($env:WINDOWS_CERTIFICATE_BASE64))
        $password = ConvertTo-SecureString $env:WINDOWS_CERTIFICATE_PASSWORD -AsPlainText -Force
        $certs = @(Import-PfxCertificate -FilePath $pfx -CertStoreLocation Cert:\CurrentUser\My -Password $password)
        if (-not ($certs | Where-Object { $_.Thumbprint -eq $env:WINDOWS_SIGNING_THUMBPRINT -and $_.HasPrivateKey })) {
            throw 'PFX does not contain the configured signing key'
        }
    }
    Invoke-Native { cmake --preset default -A x64 "-DNOITCH_VERSION=$env:RELEASE_VERSION" }
    Invoke-Native { cmake --build --preset debug }
    Invoke-Native { ctest --preset debug }
    Invoke-Native { cmake --build --preset release }
    New-Item -ItemType Directory -Force build/dist | Out-Null
    $label = if ($env:RELEASE_MODE -eq 'unsigned') { '-unsigned' } else { '' }
    $asset = "build/dist/Noitch-$env:RELEASE_VERSION-windows-x64$label.exe"
    if (Test-Path -LiteralPath $asset) { throw 'Release asset already exists' }
    Copy-Item build/Release/Noitch.exe $asset
    if ($env:RELEASE_MODE -eq 'signed') {
        $signtool = Get-ChildItem "${env:ProgramFiles(x86)}\Windows Kits\10\bin\*\x64\signtool.exe" |
            Sort-Object FullName -Descending | Select-Object -First 1
        if (-not $signtool) { throw 'Windows SDK SignTool missing' }
        Invoke-Native { & $signtool.FullName sign /sha1 $env:WINDOWS_SIGNING_THUMBPRINT /s My /fd SHA256 /tr $env:WINDOWS_TIMESTAMP_URL /td SHA256 $asset }
    }
    & "$PSScriptRoot/verify-release.ps1" -Path $asset
    $toolchain = @(& cmake --version; Get-Content build/CMakeCache.txt |
        Select-String 'CMAKE_(CXX_COMPILER|GENERATOR|VS_VERSION|VS_WINDOWS_TARGET_PLATFORM_VERSION)';
        Get-Content build/CMakeFiles/*/CMakeCXXCompiler.cmake | Select-String 'CMAKE_CXX_COMPILER_(ID|VERSION)') -join "`n"
    [IO.File]::WriteAllText((Join-Path (Get-Location) 'build/release-toolchain.txt'), $toolchain)
} finally {
    foreach ($cert in $certs) { Remove-Item "Cert:\CurrentUser\My\$($cert.Thumbprint)" -DeleteKey -ErrorAction Continue }
    if ($temp) { Remove-Item $temp -Recurse -Force -ErrorAction Continue }
}
