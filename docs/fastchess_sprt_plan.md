# Propuestas de optimización para pruebas SPRT en Fastchess

Las siguientes propuestas recogen la nueva ola de optimizaciones planificadas para el motor **Revolution** tras incorporar y publicar los cambios aprobados en campañas anteriores (LMR adaptativo, vectorización NNUE AVX2, caché persistente de killers y sintonía automatizada de evaluación). Cada entrada describe el objetivo técnico, los archivos involucrados y la etiqueta (`tag`) sugerida para conservar una instantánea compilable que pueda usarse en las pruebas **SPRT** de [Fastchess](https://github.com/minhducsun2002/fastchess).

## Historial de propuestas completadas

- **LMR adaptativo en PVS (`tag: sprt-lmr-adaptive-v1`)**: ajusta las reducciones según la volatilidad de la evaluación y sirvió como base para los builds de referencia actuales.
- **Vectorización NNUE AVX2 (`tag: sprt-nnue-avx2-v1`)**: habilita rutas AVX2/SSE2 automáticas y elevó el NPS medio en los tests de `bench`.
- **Caché persistente de killers (`tag: sprt-killer-cache-v1`)**: reduce ruido en la poda y estabiliza la profundidad efectiva por iteración.
- **Sintonía automática de evaluación (`tag: sprt-eval-tuning-v1`)**: integra el pipeline de ajuste de pesos NNUE y publica paquetes de parámetros listos para Fastchess.

Estos resultados permanecen disponibles en el repositorio principal y constituyen la línea base para las nuevas campañas.

## Nuevas propuestas de optimización (ciclo actual)

| Propuesta | Objetivo | Cambios clave | Repositorio / Tag | Métrica primaria | Riesgos y contramedidas |
|-----------|----------|---------------|-------------------|------------------|-------------------------|
| **1. Historial y quiet moves con decaimiento adaptativo** | Reducir fallos tácticos en nodos tranquilos manteniendo la velocidad. | Ajustar el cálculo de `history`, `countermoves` y `follow-up moves` en `src/movepick.cpp` y `src/search.cpp`, aplicando un factor proporcional al margen de score y un decaimiento exponencial por iteración. | `github.com/revolution-engine/revolution` `tag: sprt-history-decay-v1` | Elo en SPRT STC 10+0.1 y tasa de nodos tranquilos revisitados. | Sobreajuste: validar con suites `tests/puzzles`, contrastar con una rama espejo sin escalado por margen y revisar estadísticas `MovesToMate`. |
| **2. Prefetch NNUE dependiente de topología** | Disminuir la latencia por `cache-miss` en la actualización de acumuladores NNUE. | Insertar instrucciones `prefetch` específicas (AVX2/AVX512) en `src/nnue/nnue_accumulator.cpp`, habilitadas tras detectar capacidades en `src/misc.cpp`; mantener ruta SSE2 limpia. | `github.com/revolution-engine/revolution` `tag: sprt-nnue-prefetch-v1` | NPS medio en `bench 16 1 8 default depth 18` y Elo en SPRT blitz 5+0.1. | Detección errónea de CPU: añadir pruebas en `tests/nnue/` que verifiquen la ruta seleccionada y permitir bandera `DisablePrefetch` en `ucioption.cpp`. |
| **3. Pruning asimétrico orientado a finales simplificados** | Mantener agresividad en medios juegos y mejorar precisión en finales reducidos. | Revisar umbrales de `futility pruning`, `razoring` y `late move pruning` en `src/search.cpp`, utilizando el número de piezas menores y la presencia de peones pasados como moduladores. | `github.com/revolution-engine/revolution` `tag: sprt-endgame-pruning-v1` | Elo en SPRT LTC 60+0.1 y porcentaje de finales perdidos tras 50 movimientos. | Complejidad adicional en evaluación: validar con `perft` (`tests/perft/`) y revisar la lógica de repetición para evitar tablas espurias. |
| **4. Paralelización de regresiones NNUE previas a SPRT** | Reducir el tiempo de validación antes de lanzar campañas Fastchess. | Extender `scripts/nnue_evaluator.py` (crear si no existe) para procesar lotes con `multiprocessing`, generando reportes en `docs/sprt-results/` con métricas RMSE y precisión WDL. | `github.com/revolution-engine/revolution-tuning` `tag: sprt-nnue-eval-parallel-v1` | Tiempo total de validación y Elo tras integrar nuevos pesos. | Incoherencia de datos: forzar semilla determinista, comparar con una ejecución secuencial semanal y versionar los datasets en `assets/selfplay/metadata.json`. |

## Configuración recomendada para la Propuesta 1 (historial adaptativo)

Para el primer test de la propuesta **“Historial y quiet moves con decaimiento adaptativo”** se utilizará el modo Stockfish de Fastchess con dos fases:

1. **STC (Short Time Control)**: `10s + 0.1s`, apuntando a 2k rondas con `concurrency=2` para validar rápidamente que la modificación es prometedora.
2. **LTC (Long Time Control)**: `60s + 0.1s`, sobre el mismo binario etiquetado si el STC resulta en aceptación positiva del SPRT.

El siguiente _launcher_ en Windows (Batch) está parametrizado con las rutas y opciones necesarias para la fase STC. Ajusta `ENGINE_DEV` y `ENGINE_BASE` a los binarios correspondientes al tag activo y a la versión de referencia.

```bat
@echo off
setlocal EnableExtensions EnableDelayedExpansion
title FastChess SPRT - Live (Elo/LLR/LOS)

rem ====== RUTAS ======
set "FASTCHESS=C:\\fastchess\\fastchess.exe"
set "DIR_DEV=C:\\fastchess\\revolution-device"
set "ENGINE_DEV=%DIR_DEV%\\revolution-history-decay.exe"
set "DIR_BASE=C:\\fastchess\\revolution-baseline"
set "ENGINE_BASE=%DIR_BASE%\\Revolution-3.0-011125.exe"
set "BOOK=C:\\fastchess\\Books\\UHO_Lichess_4852_v1.epd"
set "OUTDIR=C:\\fastchess\\out"

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
set "LOGTXT=%OUTDIR%\\fc_live_%TS%.log"
set "PGNOUT=%OUTDIR%\\fc_live_%TS%.pgn"

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

## Flujo sugerido de trabajo

1. Crear una rama dedicada por propuesta, aplicar los cambios descritos y etiquetar el commit estable con el `tag` indicado.
2. Compilar el motor asociado al `tag` y subir el binario resultante a la infraestructura de Fastchess para iniciar una prueba SPRT contra la versión base `master`.
3. Registrar en `docs/sprt-results/` (directorio recomendado) el log de Fastchess y el resumen del resultado (win/draw/loss, `LLR`, `elo` estimado).
4. Si la prueba pasa el umbral configurado (por ejemplo, `LLR ≥ 2.0` para aceptar), fusionar los cambios en `master`; de lo contrario, iterar aplicando ajustes menores.

## Recursos adicionales

- [Guía de Fastchess SPRT](https://github.com/minhducsun2002/fastchess#sprt) para configurar parámetros como `alpha`, `beta`, `elo0` y `elo1`.
- `scripts/fastchess_runner.sh`: script recomendado (no incluido) que invoque `fastchess` con parámetros homogéneos para cada propuesta.
- `tests/perft/` y `tests/regression/` deben ejecutarse antes de lanzar cualquier SPRT para asegurar estabilidad funcional.
