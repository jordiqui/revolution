@echo off
setlocal EnableExtensions EnableDelayedExpansion

rem =============================================
rem Revolution vs baseline gauntlet harness
rem =============================================

rem -------- User configurable paths --------
set "CUTECHESS=C:\\cutechess\\cutechess-cli.exe"
set "ENGINE_NEW=C:\\engines\\revolution-dev\\revolution-2.81-071025.exe"
set "ENGINE_BASE=C:\\engines\\revolution-stable\\revolution-stable.exe"
set "DIR_NEW=C:\\engines\\revolution-dev"
set "DIR_BASE=C:\\engines\\revolution-stable"
rem Leave NNUE paths blank to rely on each engine's compiled-in default.
set "NNUE_NEW=C:\\engines\\revolution-dev\\networks\\candidate.nnue"
set "NNUE_BASE=C:\\engines\\revolution-stable\\networks\\baseline.nnue"
set "BOOK=C:\\cutechess\\books\\UHO_2024_8mvs_+085_+094.pgn"

rem -------- Engine options (edit as needed) --------
set "BLOCK_EXP=Y"
set "THREADS=1"
set "HASH=32"
set "PONDER=off"
set "SYZYGY_PATH=C:\\Syzygy"

set "EXP_BLOCK_CHAIN="
if /i "%BLOCK_EXP%"=="Y" (
    set "EXP_BLOCK_CHAIN= ^|setoption name Experience value false"
    set "EXP_BLOCK_CHAIN=!EXP_BLOCK_CHAIN!^|setoption name Experience Book value false"
    set "EXP_BLOCK_CHAIN=!EXP_BLOCK_CHAIN!^|setoption name Experience Prior value false"
    set "EXP_BLOCK_CHAIN=!EXP_BLOCK_CHAIN!^|setoption name Experience Concurrent value false"
    set "EXP_BLOCK_CHAIN=!EXP_BLOCK_CHAIN!^|setoption name ExperienceFile value <empty>"
)

rem -------- Gauntlet controls --------
set "TC=10+0.1"
set "GAMES=400"
set "CONCURRENCY=2"
set "ROUNDS=2"
set "ADJ_MOVES=6"
set "ADJ_MARGIN=550"
set "RESIGN_SCORE=-3500"
set "RESIGN_MOVES=4"
set "BOOKPLIES=16"

rem -------- Timestamp + output directory --------
set "OUTDIR=%~dp0out"
if not exist "%OUTDIR%" mkdir "%OUTDIR%"

for /f "tokens=1-6 delims=/:. " %%a in ("%date% %time%") do set TS=%%f%%e%%d_%%a%%b%%c
set "TS=%TS: =0%"

set "GAUNTLET_LOG=%OUTDIR%\gauntlet_%TS%.log"
set "GAUNTLET_PGN=%OUTDIR%\gauntlet_%TS%.pgn"

rem -------- Validations --------
if not exist "%CUTECHESS%" (echo [ERR] CUTECHESS no existe: "%CUTECHESS%" & goto :fail)
if not exist "%ENGINE_NEW%" (echo [ERR] ENGINE_NEW no existe: "%ENGINE_NEW%" & goto :fail)
if not exist "%ENGINE_BASE%" (echo [ERR] ENGINE_BASE no existe: "%ENGINE_BASE%" & goto :fail)
if not exist "%BOOK%" (echo [ERR] BOOK no existe: "%BOOK%" & goto :fail)
if not "%NNUE_NEW%"=="" if not exist "%NNUE_NEW%" (echo [ERR] NNUE_NEW no existe: "%NNUE_NEW%" & goto :fail)
if not "%NNUE_BASE%"=="" if not exist "%NNUE_BASE%" (echo [ERR] NNUE_BASE no existe: "%NNUE_BASE%" & goto :fail)

rem Prepare optional Syzygy option
set "SYZYGY_OPT="
if not "%SYZYGY_PATH%"=="" for %%F in ("%SYZYGY_PATH%") do set "SYZYGY_OPT= option.SyzygyPath=^"%%~fF^""

set "ENGINE_NEW_CORE=option.Threads=%THREADS% option.Hash=%HASH% option.Ponder=%PONDER%"
set "ENGINE_BASE_CORE=option.Threads=%THREADS% option.Hash=%HASH% option.Ponder=%PONDER%"

set "ENGINE_NEW_EVAL="
if not "%NNUE_NEW%"=="" for %%F in ("%NNUE_NEW%") do set "ENGINE_NEW_EVAL= option.EvalFile=^"%%~fF^""
set "ENGINE_BASE_EVAL="
if not "%NNUE_BASE%"=="" for %%F in ("%NNUE_BASE%") do set "ENGINE_BASE_EVAL= option.EvalFile=^"%%~fF^""

echo Starting gauntlet (%GAMES% games, %ROUNDS% rounds) ...
"%CUTECHESS%" ^
 -tournament gauntlet ^
 -engine cmd="%ENGINE_NEW%" name="revolution-2.81-071025" dir="%DIR_NEW%" ^
    %ENGINE_NEW_CORE%%ENGINE_NEW_EVAL%%EXP_BLOCK_CHAIN% ^
 -engine cmd="%ENGINE_BASE%" name="revolution-stable" dir="%DIR_BASE%" ^
    %ENGINE_BASE_CORE%%ENGINE_BASE_EVAL%%EXP_BLOCK_CHAIN% ^
 -each tc=%TC%%SYZYGY_OPT% ^
 -openings file="%BOOK%" format=pgn order=random -plies %BOOKPLIES% -repeat ^
 -adjudication movenumber=%ADJ_MOVES% score=%ADJ_MARGIN% -resign movecount=%RESIGN_MOVES% score=%RESIGN_SCORE% ^
 -games %GAMES% -rounds %ROUNDS% -concurrency %CONCURRENCY% -recover ^
 -report penta=true ^
 -ratinginterval 10 -scoreinterval 5 -autosaveinterval 50 ^
 -pgnout "%GAUNTLET_PGN%" ^
 -log file="%GAUNTLET_LOG%" level=info
if errorlevel 1 goto :fail

echo [DONE] Gauntlet log: "%GAUNTLET_LOG%"
echo [DONE] Gauntlet PGN: "%GAUNTLET_PGN%"
goto :eof

:fail
echo [FAIL] ExitCode=%errorlevel%
pause
exit /b %errorlevel%
