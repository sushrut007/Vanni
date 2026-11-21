# Package Vaani Release for Windows x64
param(
    [string]$Config = "Release"
)

$ErrorActionPreference = "Stop"

$root = $PSScriptRoot
Set-Location $root

Write-Host "Building Vaani ($Config)..." -ForegroundColor Cyan
cmake --build build --config $Config

$ver = (Get-Content -Path "VERSION").Trim()
$distName = "Vaani-v$ver-windows-x64"
$distDir = Join-Path $root "dist\$distName"
$zipPath = Join-Path $root "dist\$distName.zip"

Write-Host "Preparing deployment directory: $distDir" -ForegroundColor Cyan
if (Test-Path "dist") {
    Remove-Item -Recurse -Force "dist"
}
New-Item -ItemType Directory -Path $distDir -Force | Out-Null

# Copy binary and third-party dependencies
Copy-Item -Path "build\$Config\*" -Destination $distDir -Recurse

# Deploy Qt runtime dependencies
$windeployqt = (Get-Command windeployqt.exe -ErrorAction SilentlyContinue).Source
if (-not $windeployqt) {
    $found = Get-ChildItem -Path "C:\Qt", "D:\Qt" -Filter "windeployqt.exe" -Recurse -ErrorAction SilentlyContinue | Select-Object -First 1 -ExpandProperty FullName
    if ($found) { $windeployqt = $found }
}

if ($windeployqt) {
    Write-Host "Running windeployqt ($windeployqt)..." -ForegroundColor Cyan
    & $windeployqt --release --no-translations --compiler-runtime (Join-Path $distDir "Vaani.exe")
} else {
    Write-Warning "windeployqt.exe not found in PATH or Qt directories. Qt DLLs may need to be copied manually."
}

# Copy documentation and license
Copy-Item -Path "LICENSE" -Destination $distDir -ErrorAction SilentlyContinue
Copy-Item -Path "README.md" -Destination $distDir -ErrorAction SilentlyContinue

# Compress into portable zip archive
Write-Host "Creating zip package: $zipPath" -ForegroundColor Cyan
Compress-Archive -Path "$distDir\*" -DestinationPath $zipPath -Force

$item = Get-Item $zipPath
$sizeMb = [math]::Round($item.Length / 1MB, 2)
Write-Host "Done! Packaged $distName.zip ($sizeMb MB) in dist\" -ForegroundColor Green
