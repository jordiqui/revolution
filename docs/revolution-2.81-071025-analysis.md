# Revolution 2.81-071025 Bullet Tournament Review

## Tournament Context
- **Event**: Cup season IJCRL 2025, hosted on an HP ProLiant DL360p Gen8.
- **GUI**: `cutechess-cli` running a 10s + 0.1s increment round-robin with two concurrent games, Syzygy 5-piece tablebases, and the *UHO_Lichess_4852_v1* opening suite.
- **Revolution configuration**: `Move Overhead = 40`, `Slow Mover = 100`, `Minimum Thinking Time = 50`.
- **Opponents**: ZurgaaPOLY 18.1AI, BrainLearn31, HypnoS 1.01, Killfish PB 200525, RapTora 6.0, ShashChess 39.1, Stockfish 17.1, Wordfish 2.81.

## Summary of Recorded Losses
Revolution 2.81-071025 scored losses in seven catalogued games across White and Black. Key turning points are summarized below.

### vs BrainLearn31 (White, Round 224)
- Accepted ...Nxe5 on move 11 and entered an IQP structure where Black activated pieces via ...Bg4 and an early exchange sacrifice on f3.
- The queen trade on move 21 simplified into a rook-and-bishop versus rook-and-knight ending where Black's rook penetration (…Rh4, …Rf4) fixed weaknesses on the light squares.
- Syzygy adjudication occurred after Black converted the passer created by ...g4–g3 and pawn promotion on move 64.

### vs RapTora 6.0 (Black, Round 221)
- From the given FEN, Black grabbed the d7-pawn and allowed `Ng5` followed by repeated knight invasions on e6/d4.
- The attempt to counter on the queenside with ...Ra4 and ...Rf4 allowed White to sacrifice on g4 and open the king, leading to material losses after `Nxg4+` and `Ne5+`.
- RapTora converted with precise rook activity, culminating in tablebase adjudication after securing two connected passers.

### vs ShashChess 39.1 (Black, Round 220)
- Early ...Bb4 exchange led to structural weaknesses and a queenside minority attack by White.
- After `40.c8=R`, Revolution faced a rook vs bishop imbalance and could not coordinate the minor pieces against advanced pawns on the a- and c-files.
- Tablebase adjudication confirmed the loss after White shepherded passers with rook and bishop coordination.

### vs Stockfish 17.1 (Black, Round 219)
- Faced a sharp kingside attack stemming from the early `h4–h6` advance and doubled rooks on the g-file.
- Despite trading queens on move 16, Black's defensive setup collapsed as Stockfish created multiple passers (`a5`, `h4`) and dominated the open files.
- Stockfish executed a textbook mating net beginning with `79.Ra8` and concluded with `92.Rxd8#`.

### vs ZurgaaPOLY 18.1AI (White, Round 216)
- From a symmetrical structure Revolution ceded central control after `16.fxe4 Nxe4` and subsequent trades.
- Failing to contain the queenside majority, Black converted a passed b-pawn supported by `...g4` and knight maneuvers (`...Ng5`, `...Nf3`).
- The endgame collapse was sealed by tablebase adjudication at move 72 following `72.Nxe1`.

### vs HypnoS 1.01 (White, Round 214)
- Allowed the aggressive thrusts `...d4–d3` and `...g5` out of the opening, yielding Black a space advantage and long-term passer on f3.
- Attempts to simplify via `Re3` and `Bc3` left the king exposed; HypnoS converted after creating connected passed pawns and a decisive `f2` promotion.

### vs Killfish PB 200525 (White, Round 213)
- The central break `16...d4` gave Killfish a protected passed pawn on d3 and a strong initiative.
- Trading queenside pieces left Black with connected passers (`a3`, `f2`) and an active queen; Revolution could not re-route pieces in time.
- Killfish forced mate by tablebase adjudication after promoting on `a1` and coordinating queen plus pawns.

### vs BrainLearn31 (Black, Round 206)
- Accepted structural concessions with `...axb5` and `...Rxa1`, leading to long-term queenside weaknesses.
- White’s knights dominated via `Nd6` and `Nf6+`, creating mating threats culminating in `53.Qfa8#`.

## Option Value Assessment
- **Move Overhead = 40 ms**: Suitable for stable networked play but may under-allocate buffer at 10s+0.1; repeated time-pressure sequences (e.g., vs Stockfish and BrainLearn) suggest experimenting with higher overhead (50–60 ms) to hedge against spikes.
- **Slow Mover = 100**: Neutral scaling keeps original time management profile. Given the long defensive tasks observed, lowering slightly (80–90) could force faster moves when under attack to preserve increment.
- **Minimum Thinking Time = 50 ms**: Ensures at least five plies of calculation; however, some tactical oversights (e.g., vs HypnoS and BrainLearn) happened in complex middlegames. Raising to 70–80 ms might improve tactical stability at the cost of occasional flag risk.

## Observed Strategic Themes
1. **Handling of Opponent Pawn Storms**: Multiple losses stem from allowing early pawn storms (`h4–h6`, `...g5`, `...f4`). Revolution often delayed counterplay, leading to cramped positions.
2. **Endgame Conversion Defense**: In several tablebase adjudications, Revolution reached technically lost endings with insufficient counterplay. Improved prophylaxis earlier (e.g., maintaining rook activity vs BrainLearn) could avoid such transitions.
3. **Knight Coordination**: Opponents frequently dominated with centralized knights (RapTora’s `Ne5`/`Ng5`, BrainLearn’s `Nd6`/`Nf6`). Revolution’s piece placement lacked flexibility to contest outposts.

## Recommendations
- Test alternative time-management settings: `Move Overhead = 55`, `Slow Mover = 85`, `Minimum Thinking Time = 70` as a starting point for bullet matches.
- Incorporate opening book refinements to avoid sharp lines where early pawn thrusts are common, especially against Stockfish derivatives.
- Analyze Revolution’s evaluation weights for dark-square control and knight outpost penalties; adjustments could mitigate recurring structural issues observed in these games.

