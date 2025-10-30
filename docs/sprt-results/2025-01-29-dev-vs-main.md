# Informe SPRT: DEV vs BASE (10+0.1, 2 threads, 128MB)

**Fecha del match:** 29 de enero de 2025  
**Control de tiempo:** 10+0.1  
**Threads / Hash:** 2 hilos, 128 MB  
**Libro:** `UHO_Lichess_4852_v1.epd`

## Resumen numérico

| Métrica | Valor |
| --- | --- |
| Elo (confiabilidad 95 %) | -0.96 ± 5.56 |
| nElo | -1.85 ± 10.77 |
| Los (Likelihood of Superiority) | 36.81 % |
| Ratio de tablas | 50.40 % |
| Pairs ratio | 0.97 |
| Partidas | 4000 |
| Resultado | 1000 victorias / 1011 derrotas / 1989 tablas |
| Puntuación | 1994.5 / 4000 (49.86 %) |
| Ptnml(0–2) | [11, 492, 1008, 475, 14] |
| LLR | -0.26 (umbral [-2.94, 2.94]) |

## Interpretación

- El `LLR` negativo y cercano a cero indica que la prueba todavía no ha reunido evidencia suficiente para aceptar o rechazar la hipótesis alternativa; el match se mantiene dentro de los márgenes indeterminados configurados ([-2.94, 2.94]).
- El `LOS` del 36.81 % está por debajo del 50 %, lo que sugiere que, con los datos actuales, la versión `DEV` tiene más probabilidad de ser inferior a la `BASE`. Sin embargo, el intervalo de confianza del Elo (-6.52, +4.60) incluye el cero, por lo que no se puede afirmar una regresión significativa.
- El `PairsRatio` de 0.97 indica que la asignación de colores fue casi equilibrada; no se detectan señales de sesgo por emparejamiento.
- El vector `Ptnml` muestra que la mayoría de las partidas terminaron en resultados moderados (los bins centrales), típico de un match con muchas tablas; no se observan colas gruesas que indiquen inestabilidad extrema.

## Recomendaciones

1. **Prolongar el test**: Dado que el `LLR` no alcanzó los límites de decisión, se recomienda continuar la misma prueba hasta, al menos, 8000 partidas para intentar forzar una conclusión bajo el mismo control de tiempo.
2. **Repetir a ritmo más largo (opcional)**: Si se sospecha que los cambios afectan al juego en ritmos más lentos, programar un SPRT adicional a 60+0.1 con límites de `LLR` ajustados (por ejemplo ±2.94) puede aportar evidencia con menor varianza, aunque requerirá más tiempo de cómputo.
3. **Verificar logs**: Conservar el archivo `fc_live_22,68542_29102025.log` y el PGN asociado como respaldo; revisar si hay outliers o desconexiones.
4. **Monitorizar métricas adicionales**: Registrar NPS promedio y profundidad media en futuras corridas para detectar si existe un impacto de rendimiento que pudiera explicar la leve caída de Elo.

> **Conclusión:** El match actual no es concluyente; la ligera desventaja observada en Elo y LOS podría deberse al ruido estadístico. Se aconseja continuar la prueba existente o lanzar un match a 60+0.1 si se necesita una evaluación con mayor confianza.
