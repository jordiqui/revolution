# Informe de derrotas (9 de octubre de 2025)

## Contexto
- **Motor**: Revolution 2.81 (build IJCCRL Cup).
- **Formato**: Round-robin, 10+0.1 s, 1 hilo, Syzygy 5 piezas.
- **Rivales**: Motores top (Stockfish 17.1, ShashChess39.1, Brainlearn31, etc.).
- **Objetivo del informe**: detectar patrones en ocho derrotas recientes (todas con negras) y proponer cambios concretos en búsqueda, evaluación y gestión del tiempo.

## Resumen ejecutivo
| Problema | Impacto observado | Componentes a revisar |
| --- | --- | --- |
| Conversión deficiente con ventaja grande | Partidas vs *HypnoS 1.01* y *ShashChess39.1*: ventaja de -5/-8 convertida en derrota | `search.cpp` (extensiones y recortes), `evaluate.cpp` (peones pasados y rey) |
| Optimismo con el rey expuesto | *ZurgaaPOLY*, *Brainlearn31* y *Killfish* atacan al rey negro debilitado | `evaluate.cpp` (king safety, patrón h-peón), `search.cpp` (reducciones selectivas) |
| Heurística insuficiente para carreras de peones | Varias coronaciones blancas pese a material de menos | `evaluate.cpp` (passed pawns, king tropism), `search.cpp` (QSearch/SEE) |
| Gestión agresiva del tiempo en finales | Jugadas instantáneas <0.05 s durante carreras críticas | `timeman.cpp` |

## Análisis por partida

### 1. HypnoS 1.01 vs Revolution (R88)
- **Claves tácticas**: tras 28...Na6? 29.Qxb4 Nxb4 30.a3, la secuencia ...Na6? permite a las blancas fijar caballos pasivos. A pesar de -5 en eval, la máquina cambia damas y entra en final de peones donde no valora correctamente la corona de peones blancos.
- **Síntomas**:
  - Sobreestimación de la fortaleza de caballos en c5/a6; `evaluate.cpp::king_attacks()` no penaliza suficiente el alejamiento del rey negro (Kd7?!).
  - Falta de extensión cuando el peón h avanza con jaques (54.Kg6 d7 55.g8=Q+); el motor no detecta el mate por Syzygy.
- **Recomendaciones**:
  1. Añadir bonificación negativa por caballos propios que necesiten dos movimientos para volver a casillas centrales (`OutpostPenalty` en `evaluate.cpp`).
  2. Activar extensión de medio ply cuando el rival promueve en la 7ª con jaque (`search.cpp`, sección de `checkExtensions`).
  3. Ajustar `passed_pawn()` para valorar más las coronas soportadas por rey activo rival.

### 2. ShashChess39.1 vs Revolution (R85)
- **Secuencia crítica**: ventaja posicional después de 36...Bg7 pero se repiten movimientos sin plan y se permite expansión blanca en flanco de dama (80.Qd5?!). Tras 108...Rc3?? el motor crea zugzwang propio y permite coronaciones múltiples.
- **Síntomas**:
  - El motor juega ...Rc3 sin calcular la carrera de peones resultante (evaluación -9 → mate). SEE/QSearch insuficientes para múltiples coronas simultáneas.
  - Falta de conocimiento sobre "king distance to passer": el rey negro queda en g4 mientras peones blancos coronan.
- **Recomendaciones**:
  1. Elevar la penalización en `evaluate.cpp::passed_pawns()` cuando el rey rival está más cerca que el propio (usar distancia Manhattan).
  2. Introducir en `search.cpp` una extensión o reducción condicionada que limite `LMR` cuando hay múltiples peones en 6ª/7ª.
  3. En `timeman.cpp`, incrementar el mínimo de tiempo por jugada (`TimeManager::move_importance`) al detectar finales con pocas piezas.

### 3. ZurgaaPOLY 18.1AI vs Revolution (R81)
- **Claves**: la defensa contra avalancha de peones g/h falla; Revolution permite Rxh3 seguido de coronación pese a material de más.
- **Síntomas**:
  - Valoración muy optimista de estructura tras 37.Rb4 Rc8 38.Rbxb7; el motor subestima peones conectados en sexta (h3/g4).
  - QSearch omite sacrificios en h3 que abren al rey.
- **Recomendaciones**:
  1. Incrementar la penalización de peones rivales en 6ª/7ª sostenidos por torres detrás (ver `evaluate.cpp::passed_pawn_blockers`).
  2. En `search.cpp`, revisar condiciones de `futility pruning` para posiciones con ataques directos al rey.
  3. Añadir test unitario en `tests/perft` para sacrificios Rxh3 con seguimiento de mate.

### 4. Brainlearn31 vs Revolution (R80)
- **Claves**: apertura escogida lleva a rey negro sin enroque y peones adelantados (...g5 ...h5). La eval cae tras 24...f5?
- **Síntomas**:
  - Libro de aperturas permite líneas con rey expuesto (variante `g4/h4`).
  - King safety no detecta la debilidad de casillas oscuras: la dama blanca entra por a4-c6.
- **Recomendaciones**:
  1. Ajustar ponderaciones de `kingDanger` cuando los peones f/g/h se mueven dos veces.
  2. Revisar libro `polybook.cpp` para vetar secuencias con ...g5/...h5 antes del enroque.
  3. Añadir heurística en `move_picker` para priorizar `0-0`/`...O-O` cuando la evaluación propia es inferior y enroque sigue disponible.

### 5. ShashChess39.1 vs Revolution (R76)
- **Claves**: se permite sacrificio en h6 seguido de avalancha de peones h; la defensa con ...Nd5 omite 30.Bxh6!.
- **Síntomas**:
  - Falta de extensión en búsquedas cuando las piezas pesadas apuntan al rey con peones avanzados.
  - Valoración de "rooks on 7th" para las blancas insuficiente.
- **Recomendaciones**:
  1. Reducir `LMR` en nodos PV cuando existe ataque directo al rey (piezas atacantes ≥ 3).
  2. Añadir bonus en `evaluate.cpp` para torres rivales en séptima fila con piezas propias restringidas.

### 6. Brainlearn31 vs Revolution (R71)
- **Claves**: la apertura con 1...Nd7 y ...Qg6 produce estructura pasiva; Revolution entrega la pareja de alfiles sin contrajuego.
- **Síntomas**:
  - Selección de apertura deficiente; `ExperienceBook` elige líneas de baja expectativa.
  - Tras 47.Rd7+ Kh8 la evaluación -4 pero la máquina no encuentra defensa ante Nh6/Qg7.
- **Recomendaciones**:
  1. Actualizar ponderaciones en `experience.cpp` para penalizar variantes con alto score negativo.
  2. Añadir patrón táctico en `evaluate.cpp` para "mat net" cuando dama y caballo apuntan a h7/g7 con torre de apoyo.

### 7. Killfish PB 200525 vs Revolution (R69)
- **Claves**: desaprovecha ventaja clara tras 16...gxf6; se permite a7-a8=Q sin resistencia.
- **Síntomas**:
  - Peón pasado a7 controlado por torre blanca: Revolution no prioriza bloquearlo.
  - Jugadas ...Re4? y ...d4? aceleran el peón.
- **Recomendaciones**:
  1. Implementar en `search.cpp` una regla que priorice capturas/bloqueos sobre peones en séptima (ordenar movimientos según `passedThreat`).
  2. Ajustar `evaluate.cpp::passed_pawn()` para añadir gran penalización si casilla de coronación está controlada por rival y no por nosotros.

### 8. RapTora 6.0 vs Revolution (R68)
- **Claves**: sacrificio largo en g6/g5; Revolution sobrevive materialmente pero pierde por zugzwang.
- **Síntomas**:
  - Valoración de iniciativa insuficiente: permite entregas que abren columnas contra el rey.
  - En el final, el motor rehúsa repetir con ...Rg3+, optando por plan sin tiempo.
- **Recomendaciones**:
  1. Revisar `history` y `countermoves` para que las repeticiones con ventaja significativa tengan mayor prioridad.
  2. Incrementar bonus de "red squares" alrededor del rey enemigo cuando hay ataques con peones g/h.

### 9. Stockfish 17.1 vs Revolution (R66)
- **Claves**: Stockfish ejecuta plan de expansión en el flanco de dama; Revolution permite cambio de damas malo seguido de coronación del peón h.
- **Síntomas**:
  - En posiciones con piezas pesadas, el motor no mide bien la reducción de actividad tras cambiar damas.
  - Promoción h8=Q ocurre sin que la búsqueda detecte que el peón es imparable.
- **Recomendaciones**:
  1. Penalizar en `evaluate.cpp` el cambio de damas cuando el rey rival gana actividad y hay peones pasados propios atrasados.
  2. Ajustar `search.cpp` para ampliar búsqueda (extender) si el rival tiene peón en sexta sin bloqueo.

## Prioridad de implementación
1. **Evaluación de peones pasados** (impacta ≥5 partidas).
2. **Seguridad del rey y ataques laterales**.
3. **Gestión del tiempo y extensiones en finales**.
4. **Curación del libro y experiencia openings**.

## Plan de prueba sugerido
- Ejecutar `cutechess-cli` 2k partidas vs *HypnoS 1.01* a 3+0.5 tras cada grupo de cambios.
- Añadir posiciones críticas a `tests/regression` (FEN + PV objetivo) para validar mejoras.
- Documentar resultados en `docs/tuning-log.md` (crear en PR asociado si aún no existe).

## Seguimiento
- Programar revisión semanal en la rama `dev-base-oct2025` para integrar ajustes.
- Sincronizar con equipo de tiempo real para incorporar nuevas métricas de uso de reloj.
