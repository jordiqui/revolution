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
rem Deja las rutas NNUE en blanco para usar la red integrada de cada motor.
set "NNUE_NEW="
set "NNUE_BASE="
set "BOOK=C:\fastchess\Books\UHO_2024_8mvs_+085_+094.pgn"

rem -------- Engine options (edit as needed) --------
set "THREADS=1"
set "HASH=32"
rem Revolution espera valores booleanos true/false para Ponder.
set "PONDER=false"
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
if not "%SYZYGY_PATH%"=="" for %%F in ("%SYZYGY_PATH%") do (
    if exist "%%~fF" (
        set "SYZYGY_OPT= option.SyzygyPath=^"%%~fF^""
    ) else (
        echo [WARN] SYZYGY_PATH no existe: "%%~fF" - se omite SyzygyPath
    )
)

set "ENGINE_NEW_OPTS=option.Threads=%THREADS% option.Hash=%HASH% option.Ponder=%PONDER%"
set "ENGINE_BASE_OPTS=option.Threads=%THREADS% option.Hash=%HASH% option.Ponder=%PONDER%"

rem Resolver redes NNUE opcionales
set "NNUE_NEW_PATH="
if not "%NNUE_NEW%"=="" (
    for %%F in ("%NNUE_NEW%") do (
        if exist "%%~fF" (
            set "NNUE_NEW_PATH=%%~fF"
        ) else (
            echo [WARN] NNUE_NEW no existe: "%%~fF" - se omite EvalFile
        )
    )
) else (
    for %%F in ("%DIR_NEW%\nn-c01dc0ffeede.nnue") do if exist "%%~fF" (
        set "NNUE_NEW_PATH=%%~fF"
        echo [INFO] NNUE_NEW detectado: "%%~fF"
    )
)
if defined NNUE_NEW_PATH set "ENGINE_NEW_OPTS=!ENGINE_NEW_OPTS! option.EvalFile=^"!NNUE_NEW_PATH!^""

set "NNUE_BASE_PATH="
if not "%NNUE_BASE%"=="" for %%F in ("%NNUE_BASE%") do (
    if exist "%%~fF" (
        set "NNUE_BASE_PATH=%%~fF"
    ) else (
        echo [WARN] NNUE_BASE no existe: "%%~fF" - se omite EvalFile
    )
)
if defined NNUE_BASE_PATH set "ENGINE_BASE_OPTS=!ENGINE_BASE_OPTS! option.EvalFile=^"!NNUE_BASE_PATH!^""

rem -------- Validations --------
if not exist "%FASTCHESS%" (echo [ERR] FASTCHESS no existe: "%FASTCHESS%" & goto :fail)
if not exist "%ENGINE_NEW%" (echo [ERR] ENGINE_NEW no existe: "%ENGINE_NEW%" & goto :fail)
if not exist "%ENGINE_BASE%" (echo [ERR] ENGINE_BASE no existe: "%ENGINE_BASE%" & goto :fail)
if not exist "%BOOK%" (echo [ERR] BOOK no existe: "%BOOK%" & goto :fail)

set "GATING_DONE=0"

rem ---------------------------------
rem 1) Short gating gauntlet (opcional)
rem ---------------------------------
choice /m "Ejecutar match corto de validacion (%GATING_GAMES% partidas) antes del SPRT?" /c YN /d Y /t 5
if errorlevel 2 goto :sprt

set "GATING_DONE=1"
echo Running gating match (%GATING_GAMES% games) ...
"%FASTCHESS%" ^
 -engine cmd="%ENGINE_NEW%" name="revolution_2.60_190925" dir="%DIR_NEW%" %ENGINE_NEW_OPTS% ^
 -engine cmd="%ENGINE_BASE%" name="revolution_dev_v2.40" dir="%DIR_BASE%" %ENGINE_BASE_OPTS% ^
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
 -engine cmd="%ENGINE_NEW%" name="revolution_2.60_190925" dir="%DIR_NEW%" %ENGINE_NEW_OPTS% ^
 -engine cmd="%ENGINE_BASE%" name="revolution_dev_v2.40" dir="%DIR_BASE%" %ENGINE_BASE_OPTS% ^
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
