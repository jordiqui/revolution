# Propuestas de optimización para pruebas SPRT en Fastchess

Las siguientes propuestas describen optimizaciones realistas para el motor **Revolution**. Cada propuesta incluye el repositorio en el que se almacenará el código fuente y la etiqueta (`tag`) sugerida para conservar una instantánea compilable que pueda usarse en las pruebas **SPRT** de [Fastchess](https://github.com/minhducsun2002/fastchess).

| Propuesta | Objetivo | Cambios clave | Repositorio / Tag | Métrica primaria | Riesgos y contramedidas |
|-----------|----------|---------------|-------------------|------------------|-------------------------|
| **1. Ajuste dinámico de reducciones en PVS** | Reducir fallos en posiciones tácticas manteniendo la velocidad. | Ajustar la lógica de **Late Move Reductions** en `src/search.cpp`, introduciendo un factor adaptativo basado en la volatilidad de la evaluación reciente y calibrado con los campos de `Stack`. | `github.com/revolution-engine/revolution` `tag: sprt-lmr-adaptive-v1` | Incremento del Elo a ritmo blitz en 5+0.1. | Riesgo de sobreajuste: validar con suites `tests/puzzles` y revisar impacto en la tabla TT para detectar inestabilidades. |
| **2. Vectorización de evaluaciones de características** | Disminuir la latencia de la evaluación NNUE. | Reescribir el núcleo de acumulación en `src/nnue/nnue_accumulator.cpp` usando intrínsecos AVX2 con ruta de fallback SSE2 controlada desde `src/nnue/nnue_common.h`. | `github.com/revolution-engine/revolution` `tag: sprt-nnue-avx2-v1` | NPS y Elo en pruebas rápidas. | Compatibilidad: añadir autodetección en `src/misc.cpp` al inicializar `CPU::init()` y ejecutar CI en x86 sin AVX2. |
| **3. Caché de movimientos killers persistente** | Mejorar la consistencia de poda. | Guardar killers entre iteraciones principales en `src/movepick.cpp`, con límites por profundidad y limpieza al cambiar de raíz o tras `null move` fallido. | `github.com/revolution-engine/revolution` `tag: sprt-killer-cache-v1` | Profundidad promedio alcanzada (ply) y Elo. | Riesgo de contaminación: confirmar que `Stack::killers` se reinicia correctamente y añadir aserciones en builds de depuración. |
| **4. Sintonía automática de parámetros de evaluación** | Refinar heurísticas sin intervención manual. | Integrar `scripts/tune_eval.py` para ajustar pesos en `src/evaluate.cpp` y `src/tune.cpp` mediante gradiente estocástico sobre `assets/selfplay`. | `github.com/revolution-engine/revolution-tuning` `tag: sprt-eval-tuning-v1` | RMSE contra dataset y Elo posterior. | Sobreajuste al dataset: dividir en train/validation y repetir SPRT tras cada ciclo. |

## Flujo sugerido de trabajo

1. Crear una rama dedicada por propuesta, aplicar los cambios descritos y etiquetar el commit estable con el `tag` indicado.
2. Compilar el motor asociado al `tag` y subir el binario resultante a la infraestructura de Fastchess para iniciar una prueba SPRT contra la versión base `master`.
3. Registrar en `docs/sprt-results/` (nuevo directorio recomendado) el log de Fastchess y el resumen del resultado (win/draw/loss, `LLR`, `elo` estimado).
4. Si la prueba pasa el umbral configurado (por ejemplo, `LLR ≥ 2.0` para aceptar), fusionar los cambios en `master`; de lo contrario, iterar aplicando ajustes menores.

## Recursos adicionales

- [Guía de Fastchess SPRT](https://github.com/minhducsun2002/fastchess#sprt) para configurar parámetros como `alpha`, `beta`, `elo0` y `elo1`.
- `scripts/fastchess_runner.sh`: script recomendado (no incluido) que invoque `fastchess` con parámetros homogéneos para cada propuesta.
- `tests/perft/` y `tests/regression/` deben ejecutarse antes de lanzar cualquier SPRT para asegurar estabilidad funcional.
