# Revolution 2.81 (071025) vs Revolution 2.70 — Release Summary

**Executive Summary.**
Across **6,980 games** of head-to-head testing under identical conditions (TC **10+0.1**, **1** thread, **32 MB** hash, opening suite **UHO_2024_8mvs_+085_+094**), **Revolution 2.81 (build 071025)** delivers an aggregate gain of **+15.3 Elo ± 4.4** over version **2.70**, corresponding to an overall score of roughly **52.0%**. This is a small-to-moderate, reproducible improvement that is suitable for production deployments and continuous-integration gauntlets.

> Methodology note (friendly but rigorous): the overall Elo is computed by **inverse-variance weighting** of per-run Elo estimates (the reported "±" terms), which is standard when combining multiple independent gauntlets. It naturally down-weights smaller or noisier runs and up-weights larger, tighter ones.

---

## Changelog (high-level)

* **Strength:** ~**+15 Elo** vs Revolution 2.70 at 10+0.1 / 1t / 32 MB (aggregate across all gauntlets).
* **Stability:** Consistent advantage across multiple long matches; LOS values frequently ≥ 95% on larger runs.
* **Testing hygiene:** Paired openings (two games per line with color swap) and fixed random seed for reproducibility.

---

## Reproducibility (copy-paste)

**Opening suite:** `UHO_2024_8mvs_+085_+094.pgn`  
**Time control:** `10+0.1` **Threads:** `1` **Hash:** `32 MB`  
**Pairing:** `-games 2` (color-reversed pairs), `order=random`, fixed seed.  
**Launcher:** FastChess

```bat
"C:\fastchess\fastchess.exe" ^
  -engine cmd="C:\fastchess\revolution-2_81\revolution-2.81-071025.exe" name=DEV dir="C:\fastchess\revolution-2_81" option.Threads=1 option.Hash=32 option.Ponder=false option.MultiPV=1 ^
  -engine cmd="C:\fastchess\revolution-2_70\revolution 2.70.exe" name=BASE dir="C:\fastchess\revolution-2_70" option.Threads=1 option.Hash=32 option.Ponder=false option.MultiPV=1 ^
  -each tc=10+0.1 proto=uci ^
  -openings file="C:\fastchess\Books\UHO_2024_8mvs_+085_+094.pgn" format=pgn order=random ^
  -rounds 1000 -games 2 -concurrency 2 ^
  -sprt elo0=0 elo1=2.5 alpha=0.05 beta=0.05 ^
  -ratinginterval 10 -srand 12345 -report penta=true ^
  -pgnout "C:\fastchess\out\revo_2_81_vs_2_70.pgn" -log file="C:\fastchess\out\revo_2_81_vs_2_70.log" level=info
```

**Good practice.**

* Keep **experience/learning off** (or the same `.exp` file in **read-only**) for both engines.
* Use **the same NNUE/weights** for both binaries unless you’re explicitly testing a net change.
* Avoid stopping **mid-round** with `-concurrency 2` to preserve clean pairing statistics (target **PairsRatio ≈ 1.00**).
* For live metrics, tail the log (PowerShell):
  `Get-Content "…\revo_2_81_vs_2_70.log" -Wait | Select-String -Pattern 'Elo|LLR|LOS|SPRT|rating'`

---

## What users can expect

* **Practical strength:** A reliable **~+15 Elo** uplift at the tested settings/book, translating to a small but tangible edge in self-play, analysis, and large-batch testing.
* **Portability:** While Elo is control- and book-dependent, the consistency across long runs suggests the improvement generalizes to nearby settings.
* **Statistical caution:** Historical runs occasionally showed **PairsRatio > 1.1** (incomplete color-reversed pairs), which reduces SPRT efficiency. The aggregate estimate above mitigates this by weighting runs by their uncertainty. New runs with strict pairing should converge faster.

---

### TL;DR

**Revolution 2.81 (071025)** delivers a **~+15 Elo** gain over **Revolution 2.70** across **6,980 games** at **10+0.1**, **1 thread**, **32 MB**, using the **UHO_2024_8mvs_+085_+094** suite. Results are consistent across large gauntlets, confirming a steady, production-ready improvement.
