@echo off
setlocal

set "SCRIPT_DIR=%~dp0"
for %%I in ("%SCRIPT_DIR%..") do set "SCRIPTS_ROOT=%%~fI"
for %%I in ("%SCRIPTS_ROOT%..") do set "SRC_ROOT=%%~fI"
for %%I in ("%SRC_ROOT%..") do set "REPO_ROOT=%%~fI"

set "ENGINES_DIR=%SRC_ROOT%\engines"
set "REV_DEV_DIR=%ENGINES_DIR%\revolution dev"
set "OPENINGS_DIR=%SRC_ROOT%\openings\pgn"
set "GAMES_DIR=%SRC_ROOT%\games"

if not exist "%GAMES_DIR%" mkdir "%GAMES_DIR%" >nul

call "%SCRIPT_DIR%prepare_resources.bat"
if errorlevel 1 goto :error

if defined REVOLUTION_BIN (
    set "REVOLUTION_PATH=%REVOLUTION_BIN%"
) else (
    set "REVOLUTION_PATH=%SRC_ROOT%\revolution.exe"
)

if not exist "%REVOLUTION_PATH%" (
    echo Could not find the Revolution engine binary at "%REVOLUTION_PATH%".
    echo Please copy the compiled binary from the Revolution repository or set REVOLUTION_BIN.
    goto :error
)

if defined WORDFISH_BIN (
    set "WORDFISH_PATH=%WORDFISH_BIN%"
) else (
    set "WORDFISH_PATH=%REPO_ROOT%\wordfish\src\wordfish.exe"
)

if not exist "%WORDFISH_PATH%" (
    echo Could not find the Wordfish engine binary at "%WORDFISH_PATH%".
    echo Place the binary from the Wordfish repository there or set WORDFISH_BIN before running.
    goto :error
)

set "NNUE_FILE=%REV_DEV_DIR%\nn-1c0000000000.nnue"
if not exist "%NNUE_FILE%" (
    echo Missing Revolution Dev NNUE network at "%NNUE_FILE%".
    echo Run prepare_resources.bat again or download it manually.
    goto :error
)

set "OPENINGS_FILE=%OPENINGS_DIR%\UHO_4060_v4.epd"
if not exist "%OPENINGS_FILE%" (
    echo Missing UHO suite file at "%OPENINGS_FILE%".
    echo Run prepare_resources.bat again or download it manually.
    goto :error
)

if defined CUTECHESS (
    set "CUTECHESS_CMD=%CUTECHESS%"
) else (
    set "CUTECHESS_CMD=cutechess-cli"
)

where "%CUTECHESS_CMD%" >nul 2>&1
if errorlevel 1 (
    echo cutechess-cli executable not found. Set the CUTECHESS environment variable to its full path.
    goto :error
)

if not defined GAMES (
    set "GAMES=20"
)
if not defined CONCURRENCY (
    set "CONCURRENCY=4"
)
if not defined TIME_CONTROL (
    set "TIME_CONTROL=180+2"
)
if not defined THREADS (
    set "THREADS=4"
)

set "PGN_FILE=%GAMES_DIR%\revolution_dev_vs_wordfish_uho.pgn"

echo Running Revolution Dev vs Wordfish match...
"%CUTECHESS_CMD%" ^
  -engine name="Revolution Dev" cmd="%REVOLUTION_PATH%" option.EvalFile="%NNUE_FILE%" option.Threads=%THREADS% ^
  -engine name="Wordfish" cmd="%WORDFISH_PATH%" option.Threads=%THREADS% ^
  -each proto=uci tc=%TIME_CONTROL% option.Hash=256 ^
  -openings file="%OPENINGS_FILE%" format=epd order=random plies=16 start=1 ^
  -games %GAMES% -rounds %GAMES% -repeat -resign movecount=3 score=600 ^
  -draw movenumber=40 movecount=6 score=10 -concurrency %CONCURRENCY% ^
  -pgnout "%PGN_FILE%"
if errorlevel 1 goto :error

echo.
echo Match completed. PGN saved to "%PGN_FILE%".
exit /b 0

:error
echo.
echo The match could not be started. Resolve the issues above and retry.
exit /b 1
