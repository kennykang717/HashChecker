@echo off
chcp 65001 >nul

echo Building HashCheck...

:: Try MinGW-w64
where gcc.exe >nul 2>nul
if %errorlevel% equ 0 (
    echo [1/2] Compiling resources...
    windres --codepage=65001 -i resource.rc -o resource.o
    if %errorlevel% neq 0 exit /b %errorlevel%

    echo [2/2] Compiling source...
    gcc main.c hash_utils.c resource.o -o hashcheck.exe -mwindows -ladvapi32 -lgdi32 -lole32 -lwinhttp -luuid -lcomctl32 -Os -s -fdata-sections -ffunction-sections -Wl,--gc-sections
    if %errorlevel% neq 0 exit /b %errorlevel%

    del resource.o >nul 2>nul
    echo Done: hashcheck.exe
    exit /b 0
)

:: Try MSVC (Developer Command Prompt required)
where cl.exe >nul 2>nul
if %errorlevel% equ 0 (
    echo [1/2] Compiling resources...
    rc /n resource.rc
    if %errorlevel% neq 0 exit /b %errorlevel%

    echo [2/2] Compiling source...
    cl /O1 /Gy /nologo main.c hash_utils.c resource.res /Fehashcheck.exe /link /OPT:REF crypt32.lib comctl32.lib gdi32.lib ole32.lib oleaut32.lib winhttp.lib uuid.lib
    if %errorlevel% neq 0 exit /b %errorlevel%

    del resource.res >nul 2>nul
    echo Done: hashcheck.exe
    exit /b 0
)

echo Error: Neither MinGW-w64 (gcc) nor MSVC (cl) found.
echo Install one of them and try again.
exit /b 1
