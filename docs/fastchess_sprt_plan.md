# Propuestas de optimización para pruebas SPRT en Fastchess

Las siguientes propuestas describen optimizaciones realistas para el motor **revolution-dv1-011125**. Cada propuesta incluye el repositorio en el que se almacenará el código fuente y la etiqueta (`tag`) sugerida para conservar una instantánea compilable que pueda usarse en las pruebas **SPRT** de [Fastchess](https://github.com/minhducsun2002/fastchess).

| Propuesta | Objetivo | Cambios clave | Repositorio / Tag | Métrica primaria | Riesgos y contramedidas |
|-----------|----------|---------------|-------------------|------------------|-------------------------|
 codex/sugerir-optimizaciones-para-fasthchess-lewco2
| **1. Ajuste dinámico de reducciones en PVS** | Reducir fallos en posiciones tácticas manteniendo la velocidad. | Ajustar la lógica de **Late Move Reductions** en `src/search.cpp`, introduciendo un factor adaptativo basado en la volatilidad de la evaluación reciente y calibrado con los campos de `Stack`. | `github.com/revolution-engine/revolution` `tag: sprt-lmr-adaptive-v1` | Incremento del Elo a ritmo blitz en 5+0.1 y confirmación posterior en LTC 60+0.1. | Riesgo de sobreajuste: validar con suites `tests/puzzles` y revisar impacto en la tabla TT para detectar inestabilidades. |
| **2. Vectorización de evaluaciones de características** | Disminuir la latencia de la evaluación NNUE. | Reescribir el núcleo de acumulación en `src/nnue/nnue_accumulator.cpp` usando intrínsecos AVX2 con ruta de fallback SSE2 controlada desde `src/nnue/nnue_common.h`. | `github.com/revolution-engine/revolution` `tag: sprt-nnue-avx2-v1` | NPS y Elo en pruebas rápidas. | Compatibilidad: añadir autodetección en `src/misc.cpp` al inicializar `CPU::init()` y ejecutar CI en x86 sin AVX2. |
| **3. Caché de movimientos killers persistente** | Mejorar la consistencia de poda. | Guardar killers entre iteraciones principales en `src/movepick.cpp`, con límites por profundidad y limpieza al cambiar de raíz o tras `null move` fallido. | `github.com/revolution-engine/revolution` `tag: sprt-killer-cache-v1` | Profundidad promedio alcanzada (ply) y Elo. | Riesgo de contaminación: confirmar que `Stack::killers` se reinicia correctamente y añadir aserciones en builds de depuración. |
| **4. Sintonía automática de parámetros de evaluación** | Refinar heurísticas sin intervención manual. | Integrar `scripts/tune_eval.py` para ajustar pesos en `src/evaluate.cpp` y `src/tune.cpp` mediante gradiente estocástico sobre `assets/selfplay`. | `github.com/revolution-engine/revolution-tuning` `tag: sprt-eval-tuning-v1` | RMSE contra dataset y Elo posterior. | Sobreajuste al dataset: dividir en train/validation y repetir SPRT tras cada ciclo. |

### Configuración recomendada para la Propuesta 1 (LMR adaptativo)

Para el primer test de la propuesta **“Ajuste dinámico de reducciones en PVS”** se utilizará el modo Stockfish de FastChess con dos fases:

1. **STC (Short Time Control)**: `10s + 0.1s`, apuntando a 2k rondas con `concurrency=2` para validar rápidamente que la modificación es prometedora.
2. **LTC (Long Time Control)**: `60s + 0.1s`, sobre el mismo binario etiquetado si el STC resulta en aceptación positiva del SPRT.

El siguiente _launcher_ en Windows (Batch) ya está parametrizado con las rutas y opciones necesarias para la fase STC:

```bat
@echo off
setlocal EnableExtensions EnableDelayedExpansion
title FastChess SPRT - Live (Elo/LLR/LOS)

rem ====== RUTAS ======
set "FASTCHESS=C:\fastchess\fastchess.exe"
set "DIR_DEV=C:\fastchess\revolution-device"
set "ENGINE_DEV=%DIR_DEV%\revolution-PVS.exe"
set "DIR_BASE=C:\fastchess\revolution-baseline"
set "ENGINE_BASE=%DIR_BASE%\revolution-dv1-011125.exe"
set "BOOK=C:\fastchess\Books\UHO_Lichess_4852_v1.epd"
set "OUTDIR=C:\fastchess\out"

rem ====== PARÁMETROS ======
set "TC=10+0.1"
set "THREADS=1"
set "HASH=64"
set "CONCURRENCY=2"   
set "ROUNDS=2000"
set "GAMES=2"
set "REPEAT=1"        
set "MAXMOVES=200"    
set "ELO0=0"
set "ELO1=2.5"
set "ALPHA=0.05"
set "BETA=0.05"
set "RATINGINT=10"
set "SRAND=12345"

rem RECOVER:
rem   0 -> run limpio (recomendado si cambiaste cualquier cosa del protocolo)
rem   1 -> intentar reanudar si relanzas con las MISMAS rutas de log/PGN
set "RECOVER=0"

if not exist "%OUTDIR%" mkdir "%OUTDIR%"

rem ====== TIMESTAMP ======
for /f "tokens=1-6 delims=/:. " %%a in ("%date% %time%") do set TS=%%f%%e%%d_%%a%%b%%c
set "TS=%TS: =0%"
set "LOGTXT=%OUTDIR%\fc_live_%TS%.log"
set "PGNOUT=%OUTDIR%\fc_live_%TS%.pgn"

rem ====== CHEQUEOS RÁPIDOS ======
if not exist "%FASTCHESS%" echo [ERR] FASTCHESS not found: "%FASTCHESS%" & goto :end
if not exist "%ENGINE_DEV%" echo [ERR] ENGINE_DEV not found: "%ENGINE_DEV%" & goto :end
if not exist "%ENGINE_BASE%" echo [ERR] ENGINE_BASE not found: "%ENGINE_BASE%" & goto :end
if not exist "%BOOK%" echo [ERR] BOOK not found: "%BOOK%" & goto :end

rem ====== BANDERA -recover SEGÚN RECOVER ======
set "RECOVER_FLAG="
if "%RECOVER%"=="1" set "RECOVER_FLAG=-recover"

rem ====== LIVE TAIL: abre otra ventana PowerShell con filtros amplios ======
start "SPRT Live (Elo/LLR/LOS)" powershell -NoLogo -NoExit -Command ^
  "$p='%LOGTXT%'; while(-not (Test-Path $p)){ Start-Sleep -Milliseconds 200 }; " ^
  "Get-Content $p -Wait | Select-String -Pattern '(?i)Elo|LLR|LOS|SPRT|Results of|Ptnml|PairsRatio|Score|rating'"

echo ====== COMANDO QUE SE EJECUTA (SPRT) ======
echo "%FASTCHESS%" ^
 -engine cmd="%ENGINE_DEV%"  name=DEV  dir="%DIR_DEV%"  option.Threads=%THREADS% option.Hash=%HASH% option.Ponder=false option.MultiPV=1 option."Move Overhead"=80 option."Slow Mover"=100 option."Minimum Thinking Time"=100 ^
 -engine cmd="%ENGINE_BASE%" name=BASE dir="%DIR_BASE%" option.Threads=%THREADS% option.Hash=%HASH% option.Ponder=false option.MultiPV=1 option."Move Overhead"=80 option."Slow Mover"=100 option."Minimum Thinking Time"=100 ^
 -each tc=%TC% proto=uci ^
 -openings file="%BOOK%" format=epd order=random ^
 -games %GAMES% -rounds %ROUNDS% -repeat %REPEAT% -maxmoves %MAXMOVES% -concurrency %CONCURRENCY% ^
 -sprt elo0=%ELO0% elo1=%ELO1% alpha=%ALPHA% beta=%BETA% ^
 -ratinginterval %RATINGINT% -srand %SRAND% %RECOVER_FLAG% -report penta=true ^
 -pgnout "%PGNOUT%" -log file="%LOGTXT%" level=info
echo.


rem ====== LANZAR FASTCHESS ======
"%FASTCHESS%" ^
 -engine cmd="%ENGINE_DEV%"  name=DEV  dir="%DIR_DEV%"  option.Threads=%THREADS% option.Hash=%HASH% option.Ponder=false option.MultiPV=1 option."Move Overhead"=80 option."Slow Mover"=100 option."Minimum Thinking Time"=100 ^
 -engine cmd="%ENGINE_BASE%" name=BASE dir="%DIR_BASE%" option.Threads=%THREADS% option.Hash=%HASH% option.Ponder=false option.MultiPV=1 option."Move Overhead"=80 option."Slow Mover"=100 option."Minimum Thinking Time"=100 ^
 -each tc=%TC% proto=uci ^
 -openings file="%BOOK%" format=epd order=random ^
 -games %GAMES% -rounds %ROUNDS% -repeat %REPEAT% -maxmoves %MAXMOVES% -concurrency %CONCURRENCY% ^
 -sprt elo0=%ELO0% elo1=%ELO1% alpha=%ALPHA% beta=%BETA% ^
 -ratinginterval %RATINGINT% -srand %SRAND% %RECOVER_FLAG% -report penta=true ^
 -pgnout "%PGNOUT%" -log file="%LOGTXT%" level=info

:end
echo.
echo [LOG ] "%LOGTXT%"
echo [PGN ] "%PGNOUT%"
echo --- FIN. Pulsa una tecla para cerrar ---
pause
```

**Opciones recomendadas para STC 10+0.1** (aplican a ambos motores salvo que se indique lo contrario):

- `Threads = 1`: reproduce el modelo de referencia de Stockfish y evita interacciones con _smp scaling_.
- `Hash = 64 MB`: balance entre consistencia y consumo de memoria en máquinas modestas; subir a 128 MB solo si hay disponibilidad y ambos binarios lo soportan.
- `Ponder = false`: reduce ruido durante los emparejamientos y evita bloqueos cuando un motor finaliza antes.
- `Move Overhead = 80 ms` y `Minimum Thinking Time = 100 ms`: mitigan pérdidas por _lag_ en TC cortos.
- `Slow Mover = 100`: mantiene la agresividad estándar de Stockfish; experimentar ±10 solo si el STC muestra inestabilidad.
- `concurrency = 2`: suficiente para 2k rondas en tiempos razonables sin saturar CPUs con 4 hilos físicos.
- `elo0 = 0`, `elo1 = 2.5`, `alpha = 0.05`, `beta = 0.05`: parámetros SPRT simétricos que equilibran riesgo de falsos positivos/negativos en regresiones pequeñas.

Para la fase **LTC 60+0.1**, mantener el mismo _launcher_ cambiando únicamente `TC=60+0.1`, `ROUNDS=1200` (aprox. 1200 juegos con repetición 1) y, si el hardware lo permite, `Hash=256` para aprovechar partidas más largas.
=======
codex/sugerir-optimizaciones-para-fasthchess-vli8r0
| **1. Ajuste dinámico de reducciones en PVS** | Reducir fallos en posiciones tácticas manteniendo la velocidad. | Ajustar la lógica de **Late Move Reductions** en `src/search.cpp`, introduciendo un factor adaptativo basado en la volatilidad de la evaluación reciente y calibrado con los campos de `Stack`. | `github.com/revolution-engine/revolution` `tag: sprt-lmr-adaptive-v1` | Incremento del Elo a ritmo blitz en 5+0.1. | Riesgo de sobreajuste: validar con suites `tests/puzzles` y revisar impacto en la tabla TT para detectar inestabilidades. |
| **2. Vectorización de evaluaciones de características** | Disminuir la latencia de la evaluación NNUE. | Reescribir el núcleo de acumulación en `src/nnue/nnue_accumulator.cpp` usando intrínsecos AVX2 con ruta de fallback SSE2 controlada desde `src/nnue/nnue_common.h`. | `github.com/revolution-engine/revolution` `tag: sprt-nnue-avx2-v1` | NPS y Elo en pruebas rápidas. | Compatibilidad: añadir autodetección en `src/misc.cpp` al inicializar `CPU::init()` y ejecutar CI en x86 sin AVX2. |
| **3. Caché de movimientos killers persistente** | Mejorar la consistencia de poda. | Guardar killers entre iteraciones principales en `src/movepick.cpp`, con límites por profundidad y limpieza al cambiar de raíz o tras `null move` fallido. | `github.com/revolution-engine/revolution` `tag: sprt-killer-cache-v1` | Profundidad promedio alcanzada (ply) y Elo. | Riesgo de contaminación: confirmar que `Stack::killers` se reinicia correctamente y añadir aserciones en builds de depuración. |
| **4. Sintonía automática de parámetros de evaluación** | Refinar heurísticas sin intervención manual. | Integrar `scripts/tune_eval.py` para ajustar pesos en `src/evaluate.cpp` y `src/tune.cpp` mediante gradiente estocástico sobre `assets/selfplay`. | `github.com/revolution-engine/revolution-tuning` `tag: sprt-eval-tuning-v1` | RMSE contra dataset y Elo posterior. | Sobreajuste al dataset: dividir en train/validation y repetir SPRT tras cada ciclo. |
=======
| **1. Ajuste dinámico de reducciones en PVS** | Reducir fallos en posiciones tácticas manteniendo la velocidad. | Ajustar la lógica de **Late Move Reductions** (`src/search/reductions.rs`), añadiendo un factor adaptativo basado en la volatilidad de la evaluación reciente. | `github.com/revolution-engine/revolution` `tag: sprt-lmr-adaptive-v1` | Incremento del Elo a ritmo blitz en 5+0.1. | Riesgo de sobreajuste: validar con suites `tests/puzzles` y análisis de nulos. |
| **2. Vectorización de evaluaciones de características** | Disminuir la latencia de la evaluación NNUE. | Reescribir el kernel de acumulación en `src/nnue/accumulator.rs` usando intrínsecos AVX2 con ruta de fallback SSE2. | `github.com/revolution-engine/revolution` `tag: sprt-nnue-avx2-v1` | NPS y Elo en pruebas rápidas. | Compatibilidad: añadir autodetección en `build.rs` y ejecutar CI en x86 sin AVX2. |
| **3. Cache de movimientos killers persistente** | Mejorar la consistencia de poda. | Guardar killers entre iteraciones principales en `src/search/killer.rs`, con límites por profundidad. | `github.com/revolution-engine/revolution` `tag: sprt-killer-cache-v1` | Profundidad promedio alcanzada (ply) y Elo. | Riesgo de contaminación: limpiar cache al cambiar raíz o tras `null move` fallido. |
| **4. Sintonía automática de parámetros de evaluación** | Refinar heurísticas sin intervención manual. | Integrar `scripts/tune_eval.py` para ajustar pesos en `src/eval/params.rs` usando gradiente estocástico y dataset `assets/selfplay`. | `github.com/revolution-engine/revolution-tuning` `tag: sprt-eval-tuning-v1` | RMSE contra dataset y Elo posterior. | Sobreajuste al dataset: dividir en train/validation y repetir SPRT tras cada ciclo. |
 main
 main

## Flujo sugerido de trabajo

1. Crear una rama dedicada por propuesta, aplicar los cambios descritos y etiquetar el commit estable con el `tag` indicado.
2. Compilar el motor asociado al `tag` y subir el binario resultante a la infraestructura de Fastchess para iniciar una prueba SPRT contra la versión base `master`.
3. Registrar en `docs/sprt-results/` (nuevo directorio recomendado) el log de Fastchess y el resumen del resultado (win/draw/loss, `LLR`, `elo` estimado).
4. Si la prueba pasa el umbral configurado (por ejemplo, `LLR ≥ 2.0` para aceptar), fusionar los cambios en `master`; de lo contrario, iterar aplicando ajustes menores.

## Recursos adicionales

- [Guía de Fastchess SPRT](https://github.com/minhducsun2002/fastchess#sprt) para configurar parámetros como `alpha`, `beta`, `elo0` y `elo1`.
- `scripts/fastchess_runner.sh`: script recomendado (no incluido) que invoque `fastchess` con parámetros homogéneos para cada propuesta.
- `tests/perft/` y `tests/regression/` deben ejecutarse antes de lanzar cualquier SPRT para asegurar estabilidad funcional.
 codex/sugerir-optimizaciones-para-fasthchess-lewco2
=======
 codex/sugerir-optimizaciones-para-fasthchess-vli8r0

 main
 main
