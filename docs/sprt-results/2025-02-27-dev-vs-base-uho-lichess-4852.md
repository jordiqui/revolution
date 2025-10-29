# DEV vs BASE — UHO_Lichess_4852_v1 (10+0.1, 2 threads, 128 MB)

## Resumen del match

| Métrica | Valor |
| --- | --- |
| Partidas | 3 080 |
| Ritmo | 10+0.1 |
| Hilos | 2 |
| Hash | 128 MB |
| Libro | `UHO_Lichess_4852_v1.epd` |
| Elo (sprt) | −13.20 ± 6.10 |
| nElo | −26.60 ± 12.27 |
| Ratio de victorias | 23.64 % |
| Ratio de tablas | 52.34 % |
| Ratio de derrotas | 27.42 % |
| Puntuación total | 1 481.5 / 3 080 (48.10 %) |
| Ptnml (0–2) | [8, 415, 806, 308, 3] |
| LOS | 0.00 % |
| LLR | −1.77 (umbral de aceptación 2.50) |

## Interpretación

- El intervalo de confianza del Elo se mantiene completamente por debajo de cero, lo que indica que la versión DEV es **inferior** al baseline en este ritmo de juego.
- El `LOS` en 0 % y el `LLR` negativo descartan la posibilidad de que la prueba alcance el umbral de aceptación sin una reversión drástica de la tendencia.
- La puntuación final (48.10 %) también se sitúa por debajo del 50 %, reforzando la ausencia de ganancia.

## Conclusión y pasos sugeridos

1. **No promover el cambio actual.** Con la evidencia disponible, el parche debe considerarse perdedor.
2. **Realizar un diff funcional.** Revisar los commits de la rama DEV para aislar qué heurística o ajuste produjo la pérdida. Priorizar funciones relacionadas con el nodo de búsqueda principal.
3. **Repetir pruebas tras ajustes.** Una vez identificadas las causas, preparar un nuevo binario y lanzar un nuevo match SPRT partiendo de `h = 0`.
4. **Registrar futuros intentos.** Añadir nuevos resultados en este directorio (`docs/sprt-results/`) para dar seguimiento histórico a las iteraciones.

> Nota: Este informe resume los datos publicados tras 3 080 partidas, antes de que la prueba alcanzara el límite `h = 0`. Ninguna métrica sugiere una reversión favorable en las rondas restantes, por lo que detener el test y revisar el código es la opción más eficiente.
