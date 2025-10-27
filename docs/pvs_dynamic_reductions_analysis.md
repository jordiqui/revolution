# DEV vs BASE: Ajuste dinámico de reducciones en PVS

## Parámetros del match
- Ritmo: 10+0.1
- Hash: 128 MB
- Test suite: `UHO_Lichess_4852_v1.epd`
- Partidas: 4000 (pares 2T)

## Resultados agregados
- Elo: −2.69 ± 5.53
- nElo: −5.24 ± 10.77
- LOS: 16.99 %
- Proporción de tablas: 50.25 %
- Ratio WL/DD: 1.11
- Puntuación total: 1984.5 / 4000 (49.61 %)
- Ptnml(0-2): [9, 505, 1005, 470, 11]
- LLR: −0.54 dentro del marco SPRT [0.00, 2.50]

## Interpretación
La distribución de resultados muestra un comportamiento casi idéntico al baseline, pero con una ligera desventaja media para el motor DEV. El LLR negativo no aporta evidencia de mejora y se inclina hacia la hipótesis de regresión. Con estos datos, la recomendación actual es revisar o descartar el ajuste antes de destinar más recursos de testing.

## Próximos pasos sugeridos
- Revisar la implementación del ajuste dinámico y verificar posibles interacciones con otros heurísticos de búsqueda.
- Solo considerar más tests (p. ej. 60+0.1) si existe una hipótesis concreta de que el ritmo más largo pueda cambiar la tendencia observada.
