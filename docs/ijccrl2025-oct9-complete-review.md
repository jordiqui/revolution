# IJCCRL Cup 2025 (10+0.1) – Informe integral de partidas y opciones

## Resumen ejecutivo
- **Motor**: Revolution 2.81 (compilación torneo IJCCRL).
- **Control de tiempo**: 10 segundos + 0.1 incremento, 1 hilo, Syzygy 5 piezas.
- **Resultado agregado**: 11 partidas analizadas (7 con negras, 4 con blancas). Puntuación parcial 1.5/11, incluyendo conversiones fallidas con ventaja > -5 en dos ocasiones.
- **Clasificación del evento**: tercer lugar provisional (Elo 25, 55% tras 400 partidas), detrás de *Killfish PB 200525* y *HypnoS 1.01*.
- **Tendencias principales**:
  1. **Gestión de peones pasados** deficiente: cinco derrotas llegan tras subestimar coronaciones enemigas.
  2. **Seguridad del rey** vulnerable al empuje de peones laterales (g/h) tanto en ataque como defensa.
  3. **Selección de aperturas y uso del libro** permiten estructuras con rey sin enroque o peones debilitados.
  4. **Configuración de opciones** mantiene parámetros conservadores que no corrigen los problemas anteriores (p. ej., `Revolution Conservative Search`, experiencia habilitada sin limpieza previa).

## Opciones UCI relevantes
Las opciones por defecto se extraen de `src/engine.cpp`:

| Opción | Valor actual | Observaciones |
| --- | --- | --- |
| `Revolution Conservative Search` | `true` | Promueve podas conservadoras pero no evita los sacrificios y coronaciones detectados. Considerar experimento con valor `false` para incrementar profundidad efectiva y verificar impacto en finales. |
| `Experience Enabled` | `true` (archivo `experience.exp`) | Varias aperturas muestran patrones repetidos con rey sin enrocar. Recomendado purgar o reemplazar experiencia antes de la próxima sesión. |
| `Move Overhead` | `10` ms | A 10+0.1s esto deja <20 ms reales en fase tardía; en finales críticos el motor responde instantáneamente. Sugiere aumentar a 30–40 ms y subir `Minimum Thinking Time` a ≥50 ms. |
| `SyzygyProbeDepth` | `1` | Correcto, pero se observó falta de consulta a tablas en finales ganados. Revisar `SyzygyProbeLimit=7` para asegurar acceso en posiciones con múltiples piezas. |
| `Book1/Book2` | `false` | La sesión parece usar libro externo; validar que el torneo active `Book1` con archivo curado. |

## Análisis por partida

### 1. Killfish PB 200525 vs Revolution (negras)
- **Momento crítico**: 35...Rxh5 36.Re1 Rxc5 37.dxc5 permite final con peón pasado libre; el motor ignora bloqueo con ...e5.
- **Síntomas**: `evaluate.cpp::passed_pawns()` no castiga suficiente al rey pasivo en g7 y al peón h libre. Ausencia de extensión cuando rival llega a 7ª fila.
- **Acciones**:
  - Añadir penalización si el peón enemigo en sexta/ séptima está apoyado por torre y nuestro rey está a ≥3 movimientos.
  - Revisar heurística de ordenamiento para priorizar bloqueos antes de capturas laterales.

### 2. RapTora 6.0 vs Revolution (negras)
- **Claves**: tras 46...Re4? las blancas generan coronaciones múltiples (h6, a7). Evaluación -7 ignorada por la búsqueda.
- **Síntomas**: Futility pruning agresivo cuando existen amenazas de promoción dobles; `timeman` concede jugadas <0.05 s.
- **Acciones**: Reducir `LMR` y `futility` cuando haya ≥2 peones rivales en sexta; subir `Minimum Thinking Time` para forzar reflexión.

### 3. Brainlearn31 vs Revolution (negras)
- **Claves**: 28...Qxh5 29.Rxd7 Qxd7 30.Qg6 abre línea directa al rey. El motor acepta ataques con rey en g8 sin contrajuego.
- **Síntomas**: `kingDanger` no reconoce batería dama+torre en columna g; libro lleva a esquema ...g5 ...h5 antes de enrocar.
- **Acciones**: Penalizar avances dobles de peones del flanco rey sin enroque; ajustar libro/experiencia para vetar variantes con rey en el centro.

### 4. Killfish PB 200525 vs Revolution (negras, R33)
- **Claves**: ventaja decisiva tras 40...Rd7 se esfuma por plan pasivo; peón a7 corona tras secuencia 61.a6.
- **Síntomas**: Falta de algoritmo para bloquear peones pasados conectados; `history` prioriza maniobras pasivas.
- **Acciones**: Introducir bonus negativo en evaluación si una torre enemiga controla casilla de coronación sin oposición; extender búsqueda cuando un peón llega a séptima con soporte.

### 5. ShashChess39.1 vs Revolution (negras)
- **Claves**: se permite avance 65.g5 sin reacción; coronación múltiple tras 70.g7.
- **Síntomas**: `evaluate.cpp` subvalora peones pasados vinculados a reyes activos; `search` mantiene reducciones profundas pese a amenazas directas.
- **Acciones**: Desactivar `Revolution Conservative Search` en regresiones para medir si las reducciones conservadoras están tapando amenazas; agregar extensión cuando un peón pasado avanza con jaque o con soporte de rey.

### 6. Brainlearn31 vs Revolution (negras, R44)
- **Claves**: secuencia larga conduce a final técnicamente ganado (-8) que termina en mate por coronaciones múltiples.
- **Síntomas**: Gestión de tiempo y reconocimiento de tablebases ineficiente; `Tablebases::rank_root_moves` no se invoca porque `SyzygyProbeLimit` =7 y hay 8 piezas.
- **Acciones**: Elevar `SyzygyProbeLimit` a 6 piezas reales (permitir consulta antes); introducir comprobación manual de TB en nodos PV cuando eval < -5.

### 7. RapTora 6.0 vs Revolution (negras, R50)
- **Claves**: tras 35.g4 y 38.Qd3 el motor no neutraliza iniciativa; final con sacrificio prolongado.
- **Síntomas**: Falta de valoración de iniciativa; heurística de repetición (draw) desprioriza jaques perpetuos.
- **Acciones**: Ajustar `evaluate.cpp` para añadir bonificación a iniciativa (material en jaque) y `search.cpp` para subir orden de movimientos repetitivos cuando eval favorable.

### 8. Revolution vs HypnoS 1.01 (blancas, R7)
- **Claves**: sacrificio de peón en apertura lleva a rey negro atacado pero Revolution pierde control tras 39.Qc7+.
- **Síntomas**: Tiempo gastado en maniobras sin plan; `Revolution Conservative Search` recorta contragolpes; se permite ...a2 sin reacción.
- **Acciones**: Revisar heurísticas de "king race" cuando ambos reyes expuestos; permitir búsquedas más profundas en finales con damas y peones pasados.

### 9. Revolution vs ZurgaaPOLY 18.1AI (blancas)
- **Claves**: Pérdida tras 71...Rg2+: la defensa frente a peones conectados h/g vuelve a fallar.
- **Síntomas**: `evaluate` no castiga rey en g5 con peones rivales en g/h; se entrega torre por coronación sin ver mate.
- **Acciones**: Añadir patrón táctico "torre detrás del peón pasado" y penalización por casillas débiles alrededor del rey cuando peón rival alcanza h2/g2.

### 10. Revolution vs ShashChess39.1 (blancas)
- **Claves**: se permite ...g4 con rey en el centro; sacrificios temáticos llevan a mate en la banda.
- **Síntomas**: `Experience` conduce a líneas donde el rey queda en e1 sin desarrollo; `evaluate` optimista sobre estructura c3/d4.
- **Acciones**: Curar experiencia para evitar sistemas con Qf4 temprano; reforzar penalización de rey en el centro con piezas menores desarrolladas del rival.

### 11. Revolution vs HypnoS 1.01 (blancas, R16)
- **Claves**: elección de plan con 15.Qf3?! 16.Rf1?! conduce a debilidad de casillas negras; caballo en d2 queda fuera de juego.
- **Síntomas**: Tras 48...Nh4! la defensa colapsa; `search` no detecta coronación forzada.
- **Acciones**: Revisar `NNUE` para pesos en estructuras con peones g/h fijos; en `search`, limitar reducciones cuando el rival tiene peón libre apoyado por rey.

## Tareas priorizadas para afinación del código
1. **Reescribir la evaluación de peones pasados** (`evaluate.cpp`): integrar distancia del rey, soporte de torres y conectividad. Añadir tests unitarios con posiciones derivadas de partidas 1, 4, 5 y 6.
2. **Ajustar heurísticas de seguridad del rey** (`evaluate.cpp`, `search.cpp`): incrementar penalizaciones por empuje de peones g/h y añadir extensión específica para jaques de coronación.
3. **Actualizar gestión del tiempo** (`timeman.cpp`): elevar `Move Overhead` y `Minimum Thinking Time`, con pruebas A/B en 3+0.5 y 10+0.1.
4. **Curación de experiencia/libro** (`experience.cpp`, `polybook.cpp`): limpiar entradas que conduzcan a esquemas sin enroque o con flanco rey debilitado.
5. **Revisión de opciones por defecto**: preparar script de regresión que pruebe `Revolution Conservative Search = false` y `Experience Enabled = false` para medir mejora neta antes de cambios de código.

## Plan de validación
- Ejecutar mini-match (500 partidas) vs *HypnoS 1.01* a 5+0.1 con nuevas evaluaciones de peones.
- Añadir posiciones FEN clave a `tests/regression` para coronaciones múltiples y ataques g/h.
- Documentar resultados en `docs/regression-testing.md` y actualizar seguimiento semanal.
