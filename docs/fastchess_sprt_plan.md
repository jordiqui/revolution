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
| **1. Extensiones selectivas guiadas por amenazas en QSearch** | Mitigar efectos horizonte sin disparar el factor de ramificación. | Añadir en `src/search.cpp` y `src/qsearch.cpp` extensiones controladas cuando el rey propio esté bajo ataque directo o existan recapturas forzadas, apoyándose en `SEE` para limitar profundidad. | `github.com/revolution-engine/revolution` `tag: sprt-threat-extensions-v1` | Elo en SPRT STC 10+0.05 y porcentaje de `fail-high` correctos en nodos tácticos. | Árbol explosivo: monitorizar extensiones por nodo y abortar si superan un umbral dependiente de la profundidad efectiva. |
| **2. Política adaptativa de reemplazo en la tabla de transposición** | Aumentar la calidad de las entradas conservadas en partidas largas. | Ajustar `src/tt.cpp` y `src/search.cpp` para incorporar buckets por edad y una métrica híbrida (profundidad + calidad de score) en la lógica `TT::probe`, exponiendo un parámetro `TTAgeMargin` en `ucioption.cpp`. | `github.com/revolution-engine/revolution` `tag: sprt-tt-aging-v1` | Elo en SPRT LTC 60+0.1 y ratio de `TT hits` con entradas válidas. | Thrashing en hardware reducido: registrar reemplazos forzados y auditar consumo de memoria. |
| **3. Margen de futility dinámico basado en NNUE** | Evitar descartar movimientos prometedores en posiciones agudas. | Recalibrar en `src/search.cpp` los márgenes de `futility` para que dependan de la desviación estándar de la evaluación NNUE de los hijos inmediatos, con una pasada previa en la frontera. | `github.com/revolution-engine/revolution` `tag: sprt-nnue-futility-v1` | Elo en SPRT STC 15+0.1 y variación en nodos visitados respecto a la base. | Latencia adicional: cachear evaluaciones auxiliares y abortar si el presupuesto de nodos aumenta >8% en `bench`. |
| **4. Balance dinámico de tiempo y movimientos seguros** | Reducir derrotas por apuros de tiempo en TC cortos. | Incorporar en `src/timeman.cpp` una heurística que combine histórico de `MoveOverhead` y volatilidad del score para ajustar `time_for_move`, exponiendo estadísticas en `go`. | `github.com/revolution-engine/revolution` `tag: sprt-dynamic-timeman-v1` | Elo en SPRT blitz 3+0.01 y número de banderas frente a la base. | Tiempo mal invertido: fijar un límite inferior del 10% del reloj y validar con suites de finales. |
| **5. Canal incremental de presión sobre el rey en NNUE** | Capturar mejor las amenazas directas a los reyes en posiciones abiertas. | Extender `src/nnue/nnue_architecture.cpp` y `assets/nnue/features/` con un canal de atacantes por anillo y fase, reentrenando pesos y añadiendo validaciones en `tests/nnue/feature_sanity.cpp`. | `github.com/revolution-engine/revolution` `tag: sprt-king-pressure-v1` | Elo en SPRT STC 10+0.1 y precisión del evaluador en posiciones `mate-in-x`. | Sobreajuste: congelar el dataset y comparar RMSE contra la red actual antes de lanzar campañas. |

## Configuración recomendada para la Propuesta 1 (extensiones guiadas por amenazas)

Para el primer test de la propuesta **“Extensiones selectivas guiadas por amenazas en QSearch”** se utilizará el modo Stockfish de Fastchess con dos fases:

1. **STC (Short Time Control)**: `10s + 0.05s`, apuntando a 2400 rondas con `concurrency=2` para capturar rápidamente variaciones en la tasa de aciertos tácticos.
2. **LTC (Long Time Control)**: `60s + 0.1s`, sobre el mismo binario etiquetado si el STC resulta en aceptación positiva del SPRT.

El siguiente _launcher_ en Windows (Batch) está parametrizado con las rutas y opciones necesarias para la fase STC. Ajusta `ENGINE_DEV` y `ENGINE_BASE` a los binarios correspondientes al tag activo y a la versión de referencia.

```bat
@echo off
setlocal EnableExtensions EnableDelayedExpansion
title FastChess SPRT - Live (Elo/LLR/LOS)

rem ====== RUTAS ======
set "FASTCHESS=C:\\fastchess\\fastchess.exe"
set "DIR_DEV=C:\\fastchess\\revolution-device"
set "ENGINE_DEV=%DIR_DEV%\\revolution-threat-extensions.exe"
set "DIR_BASE=C:\\fastchess\\revolution-baseline"
set "ENGINE_BASE=%DIR_BASE%\\Revolution 3.20-Dev-071125.exe"
set "BOOK=C:\\fastchess\\Books\\UHO_Lichess_4852_v1.epd"
set "OUTDIR=C:\\fastchess\\out"

rem ====== PARÁMETROS ======
set "TC=10+0.05"
set "THREADS=1"
set "HASH=64"
set "CONCURRENCY=2"
set "ROUNDS=2400"
set "GAMES=2"
set "REPEAT=1"
set "MAXMOVES=200"
set "ELO0=0"
set "ELO1=3"
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

**Opciones recomendadas para STC 10+0.05** (aplican a ambos motores salvo que se indique lo contrario):

- `Threads = 1`: reproduce el modelo de referencia de Stockfish y evita interacciones con _smp scaling_.
- `Hash = 64 MB`: balance entre consistencia y consumo de memoria en máquinas modestas; subir a 128 MB solo si hay disponibilidad y ambos binarios lo soportan.
- `Ponder = false`: reduce ruido durante los emparejamientos y evita bloqueos cuando un motor finaliza antes.
- `Move Overhead = 80 ms` y `Minimum Thinking Time = 100 ms`: mitigan pérdidas por _lag_ en TC cortos.
- `Slow Mover = 100`: mantiene la agresividad estándar de Stockfish; experimentar ±10 solo si el STC muestra inestabilidad.
- `concurrency = 2`: adecuado para unas 2400 rondas sin saturar CPUs con 4 hilos físicos.
- `elo0 = 0`, `elo1 = 3`, `alpha = 0.05`, `beta = 0.05`: parámetros SPRT simétricos que elevan el umbral mínimo esperado para aceptar mejoras tácticas.

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
