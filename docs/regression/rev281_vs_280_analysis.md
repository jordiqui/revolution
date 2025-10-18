# Análisis de la regresión Revolution 2.81 vs 2.80

## Resumen del dataset
- Total de posiciones analizadas: 145 FEN consecutivos extraídos de partidas problemáticas de la build 2.81.【F:docs/regression/rev281_regression_fens.txt†L1-L145】
- Solo 14 posiciones distintas aparecen en la traza, lo que evidencia fuertes ciclos repetitivos.【bf269d†L2-L8】
- La posición `r1bqkbnr/2p1p1pp/P7/3p1p2/5P2/N2n3P/P2PP1P1/R1BQKBNR w KQkq - 1 8` se repite 120 veces seguidas, bloqueando el progreso del motor.【bf269d†L4-L12】

## Indicadores de regresión
1. **Estancamiento temprano.** Ya desde el arranque aparecen repeticiones triples del FEN inicial antes de que el motor juegue un movimiento diferente, lo que sugiere reinicios de búsqueda o fallos al aceptar el mejor movimiento calculado.【bf269d†L13-L17】
2. **Bucle profundo en la jugada 8.** La repetición masiva del mismo FEN en la jugada 8 muestra que la 2.81 mantiene la misma línea sin detectar ni castigar el ciclo, algo que la 2.80 no hacía según la regresión reportada.【bf269d†L4-L12】

## Hipótesis técnicas
- **Regresión en el cálculo de `st->repetition`.** `Position::do_move` y `Position::is_repetition` actualizan la heurística de repetición, pero cualquier cambio reciente en la lógica de historial (por ejemplo, el uso de la cola circular en `position.cpp:937-1282`) puede haber roto la detección temprana de tres repeticiones.【F:src/position.cpp†L937-L1286】
- **Heurística de evitar tablas por repetición.** El árbol de búsqueda consulta `pos.upcoming_repetition(ss->ply)` para podar opciones. Si la 2.81 cambió la condición `alpha < VALUE_DRAW` o la actualización de `ss->ply`, el motor podría favorecer movimientos que repiten posiciones sin penalización.【F:src/search.cpp†L676-L681】【F:src/search.cpp†L1658-L1666】
- **Historial / TT incoherente.** Una entrada de tabla de transposición inconsistente puede estar proponiendo el mismo movimiento una y otra vez; conviene revisar los cambios en `tt.cpp` y en la generación de claves Zobrist usada por la detección de repeticiones.【F:src/tt.cpp†L1-L342】【F:src/position.cpp†L99-L124】

## Recomendaciones
1. **Instrumentación específica.** Añadir un contador de repeticiones al registro de búsqueda (por ejemplo en `search.cpp` dentro de `search<>()`) para capturar cuándo se acepta un movimiento que deja `st->repetition` positivo y por qué heurística se eligió.【F:src/search.cpp†L640-L900】【F:src/position.cpp†L937-L1098】
2. **Test de regresión automatizado.** Crear una prueba en `tests/` que reproduzca la secuencia FEN y falle si el motor genera la misma posición más de N veces consecutivas (por ejemplo 10). Esto evitará que futuras optimizaciones reintroduzcan el ciclo.【F:docs/regression/rev281_regression_fens.txt†L1-L145】
3. **Revisión de TT y historial.** Verificar que las entradas de la TT se invalidan correctamente cuando `st->repetition` es distinto de cero y que `movegen.cpp` no devuelve pseudo-movimientos ilegales que reintroduzcan la repetición.【F:src/tt.cpp†L1-L342】【F:src/movegen.cpp†L1-L487】
4. **Comparativa dirigida con 2.80.** Ejecutar partidas espejo con ambas versiones activando logs detallados (`ucioption` -> `Debug Log File`) para identificar exactamente qué heurística cambió la elección en la jugada 8.【F:src/ucioption.cpp†L1-L266】【F:src/engine.cpp†L1-L302】

## Próximos pasos sugeridos
- Registrar una partida 2.80 vs 2.81 sobre este FEN inicial midiendo `st->repetition`, valor estático y movimiento seleccionado en cada iteración para aislar la heurística responsable.
- Ajustar temporalmente los umbrales de repetición (por ejemplo forzar `alpha <= VALUE_DRAW` en los nodos problemáticos) y comprobar si la repetición desaparece; esto confirmará si el bug está en la evaluación o en la poda.
