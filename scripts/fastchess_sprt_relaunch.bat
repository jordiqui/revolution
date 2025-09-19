@echo off
setlocal EnableExtensions EnableDelayedExpansion
title FastChess SPRT – Revolution (robusto, sin cierres)

rem ======== CONFIG BÁSICA ========
set "FASTCHESS=C:\fastchess\fastchess.exe"
set "ENGINE_DEV=C:\fastchess\revolution-ad\revolution_2.60_190925.exe"
set "DIR_DEV=C:\fastchess\revolution-ad"
set "ENGINE_BASE=C:\fastchess\revolution-base\revolution-dev_v2.40_130925.exe"
set "DIR_BASE=C:\fastchess\revolution-base"
set "BOOK=C:\fastchess\Books\UHO_2024_8mvs_+085_+094.pgn"

rem Tiempo y hardware
set "TC=10+0.1"
set "THREADS=1"
set "HASH=32"
set "CONCURRENCY=2"

rem Rondas/pareado
set "ROUNDS=1000"
set "GAMES=2"   rem -games 2 + -repeat = pareado B/N por línea

rem --- SPRT ---
set "ELO0=0"
set "ELO1=2.5"
set "ALPHA=0.05"
set "BETA=0.05"

rem Salida
set "OUTDIR=C:\fastchess\out"
if not exist "%OUTDIR%" mkdir "%OUTDIR%"

for /f "tokens=1-6 delims=/:. " %%a in ("%date% %time%") do set TS=%%f%%e%%d_%%a%%b%%c
set "TS=%TS: =0%"
set "LOGTXT=%OUTDIR%\fc_revo_%TS%.log"
set "PGNOUT=%OUTDIR%\fc_revo_%TS%.pgn"

rem ======== CHEQUEOS ========
call :req "%FASTCHESS%"   "FASTCHESS"
call :req "%ENGINE_DEV%"  "ENGINE_DEV"
call :req "%ENGINE_BASE%" "ENGINE_BASE"
call :req "%BOOK%"        "BOOK"

echo.
echo ====== RESUMEN ======
echo DEV   : "%ENGINE_DEV%"
echo BASE  : "%ENGINE_BASE%"
echo BOOK  : "%BOOK%"
echo TC    : %TC%   THREADS=%THREADS%  HASH=%HASH%  CONC=%CONCURRENCY%
echo ROUNDS: %ROUNDS%  GAMES=%GAMES%  SPRT: elo0=%ELO0% elo1=%ELO1% alpha=%ALPHA% beta=%BETA%
echo OUT   : "%OUTDIR%"
echo.

rem ======== CONFIRMACIÓN ROBUSTA ========
call :ask "¿Lanzar GAUNTLET SPRT (Y/N)? " ANSWER
if /i "%ANSWER%"=="N" goto :smoke_prompt

rem ======== COMANDO SPRT ========
set "CMD=%FASTCHESS% -engine cmd=\"%ENGINE_DEV%\" name=DEV dir=\"%DIR_DEV%\" option.Threads=%THREADS% option.Hash=%HASH% option.Ponder=false option.MultiPV=1 -engine cmd=\"%ENGINE_BASE%\" name=BASE dir=\"%DIR_BASE%\" option.Threads=%THREADS% option.Hash=%HASH% option.Ponder=false option.MultiPV=1 -each tc=%TC% proto=uci -openings file=\"%BOOK%\" format=pgn order=sequential -repeat -sprt elo0=%ELO0% elo1=%ELO1% alpha=%ALPHA% beta=%BETA% -rounds %ROUNDS% -games %GAMES% -concurrency %CONCURRENCY% -recover -report penta=true -pgnout \"%PGNOUT%\" -log file=\"%LOGTXT%\" level=info"

echo.
echo ====== COMANDO QUE SE EJECUTA ======
echo %CMD%
echo.

%CMD%
set "RC=%ERRORLEVEL%"
echo.
echo [INFO] ExitCode=%RC%
echo [LOG ] "%LOGTXT%"
echo [PGN ] "%PGNOUT%"
goto :keepopen

:smoke_prompt
call :ask "¿Lanzar SMOKE TEST corto 20 partidas (Y/N)? " ANSWER2
if /i "%ANSWER2%"=="N" goto :keepopen

rem ======== COMANDO SMOKE (sin SPRT) ========
set "PGNOUT=%OUTDIR%\fc_smoke_%TS%.pgn"
set "LOGTXT=%OUTDIR%\fc_smoke_%TS%.log"
set "CMD=%FASTCHESS% -engine cmd=\"%ENGINE_DEV%\" name=DEV dir=\"%DIR_DEV%\" option.Threads=%THREADS% option.Hash=%HASH% option.Ponder=false option.MultiPV=1 -engine cmd=\"%ENGINE_BASE%\" name=BASE dir=\"%DIR_BASE%\" option.Threads=%THREADS% option.Hash=%HASH% option.Ponder=false option.MultiPV=1 -each tc=%TC% proto=uci -openings file=\"%BOOK%\" format=pgn order=sequential -repeat -rounds 10 -games 2 -concurrency 1 -recover -report penta=true -pgnout \"%PGNOUT%\" -log file=\"%LOGTXT%\" level=info"

echo.
echo ====== COMANDO SMOKE ======
echo %CMD%
echo.

%CMD%
set "RC=%ERRORLEVEL%"
echo.
echo [INFO] ExitCode=%RC%
echo [LOG ] "%LOGTXT%"
echo [PGN ] "%PGNOUT%"
goto :keepopen

rem ======== SUBRUTINAS ========
:req
if not exist %~1 (
  echo [ERR] %~2 no existe: %~1
  goto :keepopen
)
exit /b 0

:ask
set "PROMPT=%~1"
set "VAR=%~2"
:ask_loop
set "INP="
set /p INP=%PROMPT%
rem Normaliza: toma primer carácter y pásalo a Y/N (acepta y/yes/si/s)
if "%INP%"=="" goto :ask_loop
set "C=!INP:~0,1!"
for %%Z in (y Y s S) do if "%%Z"=="!C!" set "%VAR%=Y" & goto :ask_ok
for %%Z in (n N) do if "%%Z"=="!C!" set "%VAR%=N" & goto :ask_ok
echo Respuesta no válida. Escribe Y/N (sí/no).
goto :ask_loop
:ask_ok
exit /b 0

:keepopen
echo.
echo --- FIN. La ventana seguira abierta. Pulsa una tecla para cerrar ---
pause
cmd /k
