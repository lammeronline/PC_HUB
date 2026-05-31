@echo off
setlocal
echo.
echo === PCHUB Agent Builder ===
echo.

dotnet --version > nul 2>&1
if errorlevel 1 (
    echo [ERROR] .NET SDK not found. Install .NET 8 from https://dotnet.microsoft.com/download
    pause
    exit /b 1
)

echo [1/3] Stopping agent if running...
taskkill /f /im PCAgent.exe > nul 2>&1
echo       OK

echo [2/3] Building PCAgent.exe...
dotnet publish PCAgent.csproj -c Release -r win-x64 --self-contained true -p:PublishSingleFile=true -p:DebugType=none -p:DebugSymbols=false -o build --verbosity quiet
if errorlevel 1 (
    echo [ERROR] Build failed.
    pause
    exit /b 1
)

echo [3/3] Cleaning build artifacts...
if exist bin rmdir /s /q bin
if exist obj rmdir /s /q obj
echo       OK

echo.
echo Build OK: build\PCAgent.exe
echo.
pause
endlocal
