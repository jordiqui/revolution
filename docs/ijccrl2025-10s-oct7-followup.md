# Informe adicional de análisis IJCCRL 2025 (10+0.1)

## Partidas revisadas
- Ronda 34 vs **HypnoS 1.01** (negras)
- Ronda 32 vs **RapTora 6.0** (negras)
- Ronda 31 vs **ShashChess39.1** (negras)
- Ronda 30 vs **stockfish 17.1** (negras)
- Ronda 20 vs **wordfish-2.81** (negras)
- Ronda 17 vs **Brainlearn31** (negras)
- Ronda 15 vs **Killfish PB 200525** (negras)
- Ronda 7 vs **HypnoS 1.01** (negras)
- Ronda 32 vs **RapTora 6.0** (blancas)
- Ronda 27 vs **ZurgaaPOLY 18.1AI** (blancas)
- Ronda 26 vs **Brainlearn31** (blancas)
- Ronda 25 vs **HypnoS 1.01** (blancas)
- Ronda 21 vs **stockfish 17.1** (blancas)
- Ronda 18 vs **ZurgaaPOLY 18.1AI** (blancas)
- Ronda 17 vs **Brainlearn31** (blancas)
- Ronda 12 vs **stockfish 17.1** (blancas)
- Ronda 8 vs **Brainlearn31** (blancas)

Todas se disputaron con control de tiempo **10 segundos + 0.1 s de incremento**.

## Patrones detectados

1. **Gestión del tiempo demasiado conservadora**
   - En posiciones críticas (por ejemplo, R34 jugadas 48–65, R32 jugadas 41–55) Revolution responde en <0.1 s pese a tener más de 4 s en el reloj.
   - Esta política provoca que las decisiones en finales de torres con peones pasados (R34, R25) se tomen sin la profundidad necesaria, entregando tablas teóricas o incluso derrotas por adjudicación.

2. **Sobreextensión del ala de rey**
   - Repetidamente el motor empuja **g/h** con negras (R32, R31, R30, R15, R7) aun con el rey en g8 y sin piezas defendiendo. Esto abre columnas para sacrificios en h7/g7 y coincide con fuertes ataques rivales.
   - Con blancas ocurre el patrón espejo: se juegan g4/h4 sin haber asegurado el centro (R26, R25, R17, R12, R8), dejando debilidades permanentes que los rivales explotan con sacrificios directos.

3. **Evaluación optimista de carreras de peones**
   - En R34 y R25 el motor permite que un peón pasado rival avance sin resistencia (h-pawn de HypnoS), confiando en contrajuego lejano que no se concreta.
   - En R18 y R27, con blancas, Revolution subestima la coordinación rival contra su rey cuando existen peones enemigos en quinta/sexta fila.

4. **Falta de precisión en finales simplificados**
   - Varias derrotas llegan tras simplificar a finales de torres/menores que objetivamente eran defendibles: R32 (blancas) cambia a una posición con igualdad material pero estructura inferior; R21 y R12 transponen a finales inferiores sin activar el rey.
   - Indicador de que las heurísticas para `king_activity` y `rook_mobility` pierden peso cuando quedan pocas piezas.

## Sugerencias antes de modificar código

### A. Ajustes en gestión del tiempo
- Introducir un **tiempo mínimo por jugada** (p. ej. 80–100 ms) mientras queden >1.5 s en el reloj para evitar el modo instantáneo.
- Revisar parámetros de `move_overhead` y `slowmover` para este control de tiempo específico; se recomienda medir el uso medio en matches 1k partidas vs. Sparring rápido.
- Agregar heurísticas que detecten finales con peones pasados y asignen tiempo adicional (`late_move_bonus` o equivalente) para profundizar.

### B. Evaluación y seguridad del rey
- Penalizar de forma más agresiva la estructura **g/h** cuando el propio rey permanece en g8/g1 sin flanco desarrollado. Esto podría implementarse como un término dependiente de la ausencia de piezas menores defensoras o de columnas semiabiertas sobre el rey.
- Revisar el balance de bonificaciones por iniciativa: muchos sacrificios rivales llegan porque Revolution sobrevalora la ventaja material inmediata y subestima la iniciativa adversaria.

### C. Peones pasados y finales
- Ajustar la evaluación de peones pasados rivales para incrementar la penalización cuando alcanzan la quinta fila apoyados por piezas.
- Integrar extensiones selectivas en la búsqueda (o reducir podas agresivas) cuando haya peones pasados enemigos, especialmente si la casilla de coronación está a menos de cuatro movimientos.
- Afinar términos de finales: mayor peso a la actividad del rey propio y castigos a reyes pasivos en finales de torres.

### D. Preparación de pruebas
- Antes de tocar el código, preparar *matches* de verificación (p. ej. 1 000 partidas a 10+0.1 contra HypnoS y Brainlearn) registrando uso promedio de tiempo y tasa de conversión de ventajas (`eval <= -2.0` → victoria).
- Exportar `searchstats` en los puntos señalados para comprobar si la profundidad real cae por debajo de 18 plies cuando el reloj está por encima de 3 s; si es así, ajustar la función de reparto de tiempo.

Estas acciones deben priorizarse en el orden: **tiempo → seguridad del rey → evaluación de peones pasados**, ya que los errores observados tienen correlación directa con esos componentes. Ningún cambio debe implementarse sin pruebas A/B en `cutechess-cli` con el mismo hardware de referencia.
