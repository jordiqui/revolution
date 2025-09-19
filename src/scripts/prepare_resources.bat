@echo off
setlocal

rem Determine key directories
set "SCRIPT_DIR=%~dp0"
for %%I in ("%SCRIPT_DIR%..") do set "SCRIPTS_ROOT=%%~fI"
for %%I in ("%SCRIPTS_ROOT%..") do set "SRC_ROOT=%%~fI"

set "REV_DEV_DIR=%SRC_ROOT%\engines\revolution dev"
set "OPENINGS_DIR=%SRC_ROOT%\openings\pgn"

if not exist "%REV_DEV_DIR%" (
    echo Creating Revolution Dev engine directory: "%REV_DEV_DIR%"
    mkdir "%REV_DEV_DIR%" >nul
)
if not exist "%OPENINGS_DIR%" (
    echo Creating openings directory: "%OPENINGS_DIR%"
    mkdir "%OPENINGS_DIR%" >nul
)

set "NNUE_FILE=%REV_DEV_DIR%\nn-1c0000000000.nnue"
set "NNUE_URL=https://tests.stockfishchess.org/api/nn/nn-1c0000000000.nnue"

set "UHO_FILE=%OPENINGS_DIR%\UHO_4060_v4.epd"
set "UHO_ZIP=%TEMP%\uho_suite.zip"
set "UHO_URL=https://raw.githubusercontent.com/official-stockfish/books/master/UHO_4060_v4.epd.zip"

call :download_nnue
if errorlevel 1 goto :error
call :download_uho
if errorlevel 1 goto :error

echo.
echo Resource preparation complete.
exit /b 0

:download_nnue
if exist "%NNUE_FILE%" (
    echo Found Revolution Dev NNUE network at "%NNUE_FILE%".
    exit /b 0
)

echo Downloading Revolution Dev NNUE network...
powershell -NoProfile -ExecutionPolicy Bypass -Command ^
  "$uri = '%NNUE_URL%';" ^
  "$outFile = '%NNUE_FILE%';" ^
  "Write-Host ('  Source: ' + $uri);" ^
  "Invoke-WebRequest -Uri $uri -OutFile $outFile"
if errorlevel 1 (
    echo Failed to download the NNUE network.
    exit /b 1
)

echo NNUE network downloaded to "%NNUE_FILE%".
exit /b 0

:download_uho
if exist "%UHO_FILE%" (
    echo Found UHO suite at "%UHO_FILE%".
    exit /b 0
)

echo Downloading UHO 40/60 v4 opening suite...
powershell -NoProfile -ExecutionPolicy Bypass -Command ^
  "$zipUri = '%UHO_URL%';" ^
  "$zipPath = '%UHO_ZIP%';" ^
  "$destination = '%OPENINGS_DIR%';" ^
  "Write-Host ('  Source: ' + $zipUri);" ^
  "Invoke-WebRequest -Uri $zipUri -OutFile $zipPath;" ^
  "Expand-Archive -LiteralPath $zipPath -DestinationPath $destination -Force;" ^
  "Remove-Item $zipPath"
if errorlevel 1 (
    if exist "%UHO_ZIP%" del "%UHO_ZIP%"
    echo Failed to download the UHO suite.
    exit /b 1
)

echo UHO suite extracted to "%OPENINGS_DIR%".
exit /b 0

:error
echo.
echo One or more resources could not be prepared. Please check the errors above.
exit /b 1
