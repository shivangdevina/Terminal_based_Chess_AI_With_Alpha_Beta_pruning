<div align="center">

# ♟️ Chess-Bot

### A fully-featured terminal chess engine with a custom Minimax AI

[![Language](https://img.shields.io/badge/Language-C%2B%2B17-00599C?style=for-the-badge&logo=c%2B%2B)](https://isocpp.org/)
[![Platform](https://img.shields.io/badge/Platform-Windows%20%7C%20macOS%20%7C%20Linux-lightgrey?style=for-the-badge&logo=linux)](https://github.com)
[![Algorithm](https://img.shields.io/badge/Algorithm-Alpha--Beta%20Pruning-blueviolet?style=for-the-badge)](https://en.wikipedia.org/wiki/Alpha%E2%80%93beta_pruning)
[![Rules](https://img.shields.io/badge/Rules-100%25%20Standard%20Chess-green?style=for-the-badge)](https://www.fide.com/fide/handbook.html)
[![License](https://img.shields.io/badge/License-MIT-yellow?style=for-the-badge)](LICENSE)

> *Play chess against an AI that thinks several moves ahead — entirely in your terminal.*

</div>

---

## 🌟 Features

### Full Standard Chess Rules
Every rule you'd find in an official FIDE game is implemented:

| Rule | Details |
|------|---------|
| **En Passant** | Detected from the previous move's stored payload; correctly expires after one half-move |
| **Castling** | Both king-side and queen-side; rights revoked on first king or rook movement |
| **Pawn Promotion** | Human players choose interactively (R/K/B/Q); bots auto-queen (or randomise in random mode) |
| **50-Move Rule** | Counter tracked per half-move; resets on any pawn move or capture |
| **Threefold Repetition** | Board, castling rights, and en passant availability must all match — no shortcuts |
| **Check / Checkmate** | Every candidate move is play-tested and filtered if it leaves the king in check |

### 🎮 Three Game Modes
```
1. Player vs Bot  (PvE) — You play as white or black; bot gets a difficulty level
2. Player vs Player (PvP) — Two humans, same terminal, alternating input
3. Bot vs Bot    (EvE) — Watch any two difficulty levels compete autonomously
```

### 🤖 Three Bot Difficulty Levels
| Level | Minimax Depth | Behaviour |
|-------|--------------|-----------|
| Easy   (1) | 1-ply | Looks one move ahead |
| Medium (2) | 2-ply | Looks two moves ahead (full opponent reply) |
| Hard   (3) | 3-ply | Looks three moves ahead with full pruning |

### 🖥️ Cross-Platform Terminal UI
- **Windows**: Uses Win32 `SetConsoleCursorPosition` for in-place cell updates
- **macOS / Linux**: ANSI escape sequences (`ESC[y;xH`) for the same effect
- The board is redrawn in-place (no flicker) via targeted cursor positioning — only changed squares are updated on human moves

---

## 🧠 Bot Architecture & AI Logic

The chess bot is built on two interacting components: a game-tree search algorithm and a board evaluation function.

### Minimax with Alpha-Beta Pruning

The core AI is located in `PathNode::AlphaBeta()`. It performs a **depth-limited Minimax search** where:

- The **maximising player** (the bot's side) tries to maximise the board score.
- The **minimising player** (the opponent) tries to minimise it.
- **Alpha** (`α`) is the best score the maximiser is guaranteed so far. It starts at −∞.
- **Beta** (`β`) is the best score the minimiser is guaranteed so far. It starts at +∞.

When `α ≥ β` at any node, the remaining siblings cannot possibly improve the outcome for the relevant player — so the branch is **pruned immediately** with a `break`. This is the Alpha-Beta cut-off. In practice it allows the same search depth to be explored with far fewer node evaluations than plain Minimax (roughly from O(b^d) → O(b^(d/2)) in the best case).

```
AlphaBetaRoot()                   ← Root: iterates all legal moves, tracks max
  └─ AlphaBeta(depth-1, α, β)     ← Minimising (opponent's response)
       └─ AlphaBeta(depth-2, α, β) ← Maximising
            └─ ...
                 └─ EvaluateBoard()  ← Leaf: material + positional score
```

**King-capture short-circuit:** before recursing, the search checks if a move directly captures the opponent king. If so, it immediately returns ±9999 — a definitive win/loss that terminates the branch early without descending further.

**Move / Undo:** the search uses `MovePiece(false, false)` to apply a move without touching the display, and `MovePieceBack()` to restore the board exactly — including castling rights, en passant capture state, and promotion reversals.

---

### Evaluation Function: Material + Piece-Square Tables

The leaf evaluation (`EvaluateBoard`) sums `EvaluatePosition(x, y)` for every square. Each square's contribution is:

```
score(x, y) = sign(piece) × ( material_value + PST_bonus[piece_type][row][col] )
```

**Material values** (centipawn-like units):

| Piece | Value |
|-------|-------|
| Pawn | 10 |
| Rook | 50 |
| Knight | 30 |
| Bishop | 30 |
| Queen | 90 |
| King | 900 |

**Piece-Square Tables (PST)** — `PIECE_POS_POINTS[6][8][8]` — encode positional heuristics:

| Piece | Key Heuristic |
|-------|--------------|
| **King** | Heavy centre penalties (−5 near e4/d4), bonus at corner/edge (safety) |
| **Queen** | Near-zero bonuses; discourages premature development |
| **Bishop** | Long-diagonal bonuses, edge penalties (mobility) |
| **Knight** | *"A knight on the rim is dim"* — −5 at corners, +2 at the centre |
| **Rook** | 7th-rank bonus (+1 at rank 7), back-rank neutral |
| **Pawn** | Central bonuses (d4/e4 = +2), 7th-rank promotion-threat bonus (+5) |

The sign is automatically applied: black pieces carry a negative `ChessPieces` enum value, so the formula naturally penalises black pieces in white's favour. `EvaluateBoard` then flips the total by the initiating bot's colour to return a score "from the bot's perspective."

---

## 📁 Directory Structure

```
Chess-Bot/
└── Chess_Bot/
    ├── game.cpp          # Entire engine in a single self-contained file
    └── README.md         # This file
```

`game.cpp` was merged from the original multi-file project:
```
game.cpp ← merged from:
  ├── main.cpp        (entry point, game-mode selection)
  ├── chess.h / chess.cpp   (Chess class: board, rules, UI)
  ├── player.cpp      (Player base class)
  ├── path_node.cpp   (PathNode: AlphaBeta search)
  └── bot.cpp         (Bot: Player + AI entry point)
```

---

## 🔧 Build & Run

### Prerequisites
- A C++17-capable compiler: `g++` ≥ 7.0 or `clang++` ≥ 5.0
- No external dependencies — standard library only

### Compile

**macOS / Linux (g++):**
```bash
g++ -std=c++17 -O2 -o chessbot Chess_Bot/game.cpp
```

**macOS (clang++):**
```bash
clang++ -std=c++17 -O2 -o chessbot Chess_Bot/game.cpp
```

**Windows (MSVC, Developer Command Prompt):**
```cmd
cl /std:c++17 /O2 /EHsc Chess_Bot\game.cpp /Fe:chessbot.exe
```

**Windows (MinGW g++):**
```bash
g++ -std=c++17 -O2 -o chessbot.exe Chess_Bot/game.cpp
```

### Run
```bash
./chessbot          # macOS / Linux
chessbot.exe        # Windows
```

### Gameplay Controls

| Input | Effect |
|-------|--------|
| `e2 e4` | Move piece from e2 to e4 (space or newline separated) |
| `quit` / `exit` | Resign the current game |
| `r` / `k` / `b` / `q` | Choose promotion piece when prompted |
| `R` | Play again after game over |
| Any other key | Quit after game over |

---

## 🏗️ Technical Highlights

- **Single-file design** — the entire engine is `game.cpp` (≈1200 lines). No build system, no headers, no dependencies.
- **Compact board encoding** — pieces stored as signed `char`; sign encodes colour, magnitude encodes piece type. Allows `O(1)` colour checks: `piece < 0` = black, `piece > 0` = white.
- **Lazy tree generation** — `PathNode` generates child nodes on demand inside each `AlphaBeta` call, then clears them immediately after. Memory stays bounded regardless of search depth.
- **Move encoding** — each move is serialised as a 7-byte string: `[x1][y1][x2][y2][piece_moved][piece_captured][castling_flag]` plus optional promotion byte. This single string enables both display (algebraic notation) and full undo.
- **Threefold repetition** — correctly implements FIDE rules including castling rights and en passant availability as part of position identity.

---

## 👤 Author

**Mauryavardhan Singh**  
Built from scratch in C++17 — board representation, rules engine, terminal UI, and custom AI included.

---

<div align="center">
<sub>If this project impressed you, consider leaving a ⭐ on GitHub!</sub>
</div>