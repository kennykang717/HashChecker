$ErrorActionPreference = "Stop"

Write-Host "Building HashCheck..." -ForegroundColor Cyan

# Find MinGW
$gcc = Get-Command gcc.exe -ErrorAction SilentlyContinue
$windres = Get-Command windres.exe -ErrorAction SilentlyContinue

if (-not $gcc -or -not $windres) {
    Write-Host "Error: MinGW-w64 (gcc + windres) not found." -ForegroundColor Red
    exit 1
}

Write-Host "[1/2] Compiling resources..." -ForegroundColor Yellow
& $windres.Source --codepage=65001 -i resource.rc -o resource.o
if ($LASTEXITCODE -ne 0) { throw "windres failed" }

Write-Host "[2/2] Compiling source..." -ForegroundColor Yellow
& $gcc.Source main.c hash_utils.c resource.o -o hashcheck.exe -mwindows -ladvapi32 -lgdi32 -lole32 -lwinhttp -luuid -lcomctl32 -Os -s -fdata-sections -ffunction-sections "-Wl,--gc-sections"
if ($LASTEXITCODE -ne 0) { throw "gcc failed" }

Remove-Item -Force resource.o -ErrorAction SilentlyContinue

Write-Host "Done: hashcheck.exe ($((Get-Item hashcheck.exe).Length / 1KB) KB)" -ForegroundColor Green
