# Informe de análisis IJCCRL 2025

## Condiciones de juego
- **Torneo**: Round-robin (9 rivales) ejecutado con `cutechess-cli`.
- **Control de tiempo**: 10+0.1 (10 minutos con incremento de 0.1 s).
- **Hardware**: HP Proliant DL360P Gen8, 1 hilo por motor, hash 32 MB, Syzygy 5 piezas.
- **Aperturas**: Libro `UHO_Lichess_4852_v1.epd` con orden aleatorio, 2 partidas por cruce y repetición 2x.

Estas condiciones ponen énfasis en:
- **Rapidez en incrementos mínimos**: penaliza motores con mala gestión del reloj.
- **Variabilidad de aperturas**: exige solidez en múltiples estructuras.
- **Syzygy limitado**: finales de 6 o más piezas dependen totalmente de la evaluación propia.

## Síntomas observados en derrotas recientes
Se revisaron múltiples PGN de la sesión del 6 de octubre de 2025. Los patrones más repetidos son:

1. **Conversión deficiente de ventajas significativas**
   - Contra *ZurgaaPOLY 18.1AI* (R45) se alcanzó -8 en evaluación pero se permitió el avance libre de peones pasados y la coordinación de piezas blancas, culminando en mate.
   - Frente a *Killfish PB 200525* (R42) y *RapTora 6.0* (R41) la ventaja posicional colapsó tras repetidas decisiones pasivas que devolvieron la iniciativa.

2. **Evaluación optimista en defensas pasivas**
   - El motor mantiene evaluaciones negativas (ventaja negra) mientras acepta cambios que abren columnas contra su rey (por ejemplo, R41 y R40), lo que sugiere heurísticas de seguridad insuficientes cuando el rey queda expuesto o hay ruptura de peones entorno a él.

3. **Mala gestión del tiempo con incremento mínimo**
   - En los finales largos (R43 vs *HypnoS* y R34 vs *HypnoS*) se observan secuencias con 0.03–0.10 s por jugada, señal de que el motor entra en modo "movimiento instantáneo" y pierde precisión táctica. Esto es crítico con 0.1 s de incremento.

4. **Debilidades en la evaluación de peones pasados y carreras de peones**
   - Varias derrotas llegan tras no frenar peones libres (R36, R35, R25). La heurística actual subvalora la necesidad de bloquear/neutralizar peones avanzados o sobrevalora contrajuego lejano.

5. **Selección de aperturas con posiciones técnicas complejas**
   - El libro UHO introduce estructuras raras donde la comprensión estratégica del motor parece insuficiente, resultando en posiciones inferiores rápidamente (R33, R31).

## Recomendaciones de afinado

### 1. Ajustes en evaluación
- **Reforzar la seguridad del rey**: incrementar los términos que penalizan peones propios avanzados frente al rey y columnas abiertas dirigidas hacia él. Integrar términos específicos para debilidades como *h6–g5* o *h4–g4* repetidas en las partidas.
- **Peones pasados**: revisar `passed_pawn_score` y condiciones de bloqueo. Proponer una bonificación dinámica que crezca al avanzar y penalice al rival si la casilla de bloqueo es controlada por nuestras piezas.
- **Movilidad en piezas menores**: en varias derrotas los caballos quedaron en los bordes (…Na5–c4, …Nc3a4), evaluar un castigo mayor por caballos atrapados y recompensar ocupaciones centrales estables.
- **Evaluación de finales sin tablas Syzygy**: añadir heurísticas para finales de rey y peones (por ejemplo, distancia del rey al peón pasado rival, oposición) para evitar fallos como en R25.

### 2. Gestión del tiempo
- Revisar `TimeManager` para evitar valores de `move_overhead` demasiado altos que disparan jugadas instantáneas. Considerar:
  - Establecer un umbral mínimo de tiempo por jugada (p.e. 50–70 ms) incluso con tiempo bajo.
  - Detectar fases tranquilas (sin táctica inmediata) y emplear búsqueda más profunda.
  - Guardar estadísticas de tiempo por jugada para calibrar en matches de prueba.

### 3. Búsqueda
- **Extensiones selectivas**: activar extensiones para peones pasados en la sexta/séptima fila del rival y para amenazas de mate, evitando que la búsqueda las "corte" demasiado pronto.
- **Reducir NMP (Null Move Pruning)** cuando la estructura del rey está debilitada o cuando el rival tiene peones pasados peligrosos.
- **Tuning automático**: ejecutar una sesión de *Texel tuning* con las partidas más recientes para ajustar pesos de evaluación, incorporando las partidas perdidas como dataset negativo.

### 4. Aperturas y preparación
- Revisar la lista `UHO_Lichess_4852_v1`: filtrar líneas donde Revolution obtiene evaluaciones > +0.30 tras la apertura (indicando problemas).  
- Introducir un subconjunto de aperturas con estructuras más familiares para el motor (e.g., Sveshnikov, Semi-Eslava) para reducir sorpresas.

### 5. Infraestructura de pruebas
- Configurar matches cortos (p.e. 2k partidas a 3+0.5) tras cada cambio para validar mejoras tácticas.
- Automatizar la recolección de métricas: tasa de conversión de ventaja (porcentaje de posiciones con -2.00 que acaban en victoria), uso medio de tiempo por jugada y número de derrotas por abandono del reloj.

### 6. Próximos pasos propuestos
1. Implementar y probar ajustes de evaluación (seguridad del rey y peones pasados).  
2. Afinar parámetros de tiempo (`move_overhead`, `slowMover`) con pruebas `cutechess-cli` en incrementos bajos.  
3. Realizar nueva tanda de partidas de referencia contra *stockfish 17.1* y *Killfish PB 200525* para validar.

## Beneficios esperados
- Mayor resiliencia defensiva al enfrentar ataques directos sobre el rey.
- Mejor conversión de ventajas materiales o espaciales al priorizar el control de peones pasados.
- Menor número de derrotas por errores en zeitnot, aumentando la puntuación final en torneos tipo IJCCRL.

## Seguimiento
Documentar cada iteración en `docs/tuning-log.md` (crear si no existe) con parámetros modificados, resultados y análisis. Esto permitirá retroceder rápidamente si un cambio degrada el rendimiento.
