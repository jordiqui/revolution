@echo off
setlocal EnableExtensions EnableDelayedExpansion

rem =============================================
rem Revolution vs baseline SPRT relaunch harness
rem Implements pre-SPRT gating match and full SPRT
rem =============================================

rem -------- User configurable paths --------
set "FASTCHESS=C:\fastchess\fastchess.exe"
set "ENGINE_NEW=C:\fastchess\revolution-ad\revolution_2.60_190925.exe"
set "ENGINE_BASE=C:\fastchess\revolution-base\revolution-dev_v2.40_130925.exe"
set "DIR_NEW=C:\fastchess\revolution-ad"
set "DIR_BASE=C:\fastchess\revolution-base"
rem Leave NNUE paths blank to rely on each engine's compiled-in default.
set "NNUE_NEW=C:\fastchess\revolution-ad\networks\candidate.nnue"
set "NNUE_BASE=C:\fastchess\revolution-base\networks\baseline.nnue"
set "BOOK=C:\fastchess\Books\UHO_2024_8mvs_+085_+094.pgn"

rem -------- Engine options (edit as needed) --------
set "THREADS=1"
set "HASH=32"
set "PONDER=off"
set "SYZYGY_PATH=C:\Syzygy"

rem -------- Test controls --------
set "TC=10+0.1"
set "CONCURRENCY=2"
set "ADJ_MOVES=6"
set "ADJ_MARGIN=550"
set "RESIGN_SCORE=-3500"
set "RESIGN_MOVES=4"
set "BOOKPLIES=16"

rem Pre-SPRT gating match (short sanity check)
set "GATING_GAMES=200"

rem SPRT parameters
set "ROUNDS=1000"
set "ELO0=0"
set "ELO1=2.5"
set "ALPHA=0.05"
set "BETA=0.05"

rem -------- Timestamp + output directory --------
set "OUTDIR=%~dp0out"
if not exist "%OUTDIR%" mkdir "%OUTDIR%"

for /f "tokens=1-6 delims=/:. " %%a in ("%date% %time%") do set TS=%%f%%e%%d_%%a%%b%%c
set "TS=%TS: =0%"

set "GATING_LOG=%OUTDIR%\gating_%TS%.log"
set "GATING_PGN=%OUTDIR%\gating_%TS%.pgn"
set "SPRT_LOG=%OUTDIR%\sprt_%TS%.log"
set "SPRT_PGN=%OUTDIR%\sprt_%TS%.pgn"

rem Prepare optional Syzygy option
set "SYZYGY_OPT="
if not "%SYZYGY_PATH%"=="" for %%F in ("%SYZYGY_PATH%") do set "SYZYGY_OPT= option.SyzygyPath=^"%%~fF^""

set "ENGINE_NEW_CORE=option.Threads=%THREADS% option.Hash=%HASH% option.Ponder=%PONDER%"
set "ENGINE_BASE_CORE=option.Threads=%THREADS% option.Hash=%HASH% option.Ponder=%PONDER%"

set "ENGINE_NEW_EVAL="
if not "%NNUE_NEW%"=="" for %%F in ("%NNUE_NEW%") do set "ENGINE_NEW_EVAL= option.EvalFile=^"%%~fF^""
set "ENGINE_BASE_EVAL="
if not "%NNUE_BASE%"=="" for %%F in ("%NNUE_BASE%") do set "ENGINE_BASE_EVAL= option.EvalFile=^"%%~fF^""

rem -------- Validations --------
if not exist "%FASTCHESS%" (echo [ERR] FASTCHESS no existe: "%FASTCHESS%" & goto :fail)
if not exist "%ENGINE_NEW%" (echo [ERR] ENGINE_NEW no existe: "%ENGINE_NEW%" & goto :fail)
if not exist "%ENGINE_BASE%" (echo [ERR] ENGINE_BASE no existe: "%ENGINE_BASE%" & goto :fail)
if not exist "%BOOK%" (echo [ERR] BOOK no existe: "%BOOK%" & goto :fail)
if not "%NNUE_NEW%"=="" if not exist "%NNUE_NEW%" (echo [ERR] NNUE_NEW no existe: "%NNUE_NEW%" & goto :fail)
if not "%NNUE_BASE%"=="" if not exist "%NNUE_BASE%" (echo [ERR] NNUE_BASE no existe: "%NNUE_BASE%" & goto :fail)

set "GATING_DONE=0"

rem ---------------------------------
rem 1) Short gating gauntlet (opcional)
rem ---------------------------------
choice /m "Ejecutar match corto de validacion (%GATING_GAMES% partidas) antes del SPRT?" /c YN /d Y /t 5
if errorlevel 2 goto :sprt

set "GATING_DONE=1"
echo Running gating match (%GATING_GAMES% games) ...
"%FASTCHESS%" ^
 -engine cmd="%ENGINE_NEW%" name="revolution_2.60_190925" dir="%DIR_NEW%" ^
    %ENGINE_NEW_CORE%%ENGINE_NEW_EVAL% ^
 -engine cmd="%ENGINE_BASE%" name="revolution_dev_v2.40" dir="%DIR_BASE%" ^
    %ENGINE_BASE_CORE%%ENGINE_BASE_EVAL% ^
 -each tc=%TC%%SYZYGY_OPT% ^
 -openings file="%BOOK%" format=pgn order=random -plies %BOOKPLIES% -repeat ^
 -adjudication movenumber=%ADJ_MOVES% score=%ADJ_MARGIN% -resign movecount=%RESIGN_MOVES% score=%RESIGN_SCORE% ^
 -games %GATING_GAMES% -concurrency %CONCURRENCY% -recover ^
 -report penta=true ^
 -ratinginterval 10 -scoreinterval 5 -autosaveinterval 50 ^
 -pgnout "%GATING_PGN%" ^
 -log file="%GATING_LOG%" level=info
if errorlevel 1 goto :fail

echo [DONE] Gating log: "%GATING_LOG%"
echo [DONE] Gating PGN: "%GATING_PGN%"

:sprt
echo Running SPRT (%ROUNDS% rounds, elo0=%ELO0%, elo1=%ELO1%) ...
"%FASTCHESS%" ^
 -engine cmd="%ENGINE_NEW%" name="revolution_2.60_190925" dir="%DIR_NEW%" ^
    %ENGINE_NEW_CORE%%ENGINE_NEW_EVAL% ^
 -engine cmd="%ENGINE_BASE%" name="revolution_dev_v2.40" dir="%DIR_BASE%" ^
    %ENGINE_BASE_CORE%%ENGINE_BASE_EVAL% ^
 -each tc=%TC%%SYZYGY_OPT% ^
 -openings file="%BOOK%" format=pgn order=random -plies %BOOKPLIES% -repeat ^
 -adjudication movenumber=%ADJ_MOVES% score=%ADJ_MARGIN% -resign movecount=%RESIGN_MOVES% score=%RESIGN_SCORE% ^
 -sprt elo0=%ELO0% elo1=%ELO1% alpha=%ALPHA% beta=%BETA% ^
 -rounds %ROUNDS% -concurrency %CONCURRENCY% -recover ^
 -report penta=true ^
 -ratinginterval 1 -scoreinterval 1 -autosaveinterval 50 ^
 -pgnout "%SPRT_PGN%" ^
 -log file="%SPRT_LOG%" level=info
if errorlevel 1 goto :fail

echo [DONE] SPRT log: "%SPRT_LOG%"
echo [DONE] SPRT PGN: "%SPRT_PGN%"
if "%GATING_DONE%"=="0" echo [INFO] Gating match omitido (respuesta del usuario).
goto :eof

:fail
echo [FAIL] ExitCode=%errorlevel%
pause
exit /b %errorlevel%
