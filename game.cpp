/**
 * @file game.cpp
 * @brief Single-file Terminal Chess Game with an AI Bot.
 *
 * This file contains the complete implementation of a terminal-based chess game
 * merged from several original translation units (main.cpp, chess.h, chess.cpp,
 * player.cpp, path_node.cpp, bot.cpp). It supports:
 *   - Full standard chess rules: En Passant, Castling, Pawn Promotion,
 *     50-Move Rule, Threefold Repetition, and Checkmate detection.
 *   - Three game modes: Player vs Bot (PvE), Player vs Player (PvP),
 *     and Bot vs Bot (EvE).
 *   - Three bot difficulty levels via Minimax search depth (1 = Easy,
 *     2 = Medium, 3 = Hard).
 *   - Cross-platform terminal UI (Windows via Win32 API / macOS+Linux via ANSI).
 *
 * @author  Mauryavardhan Singh
 */
// game.cpp - Single-file ChessBot
// Merged from main.cpp, chess.h, chess.cpp, player.cpp, path_node.cpp, bot.cpp

#include <iostream>
#include <string>
#include <forward_list>
#include <vector>
#include <map>
#include <algorithm>
#include <time.h>

// Platform-specific includes and functions
#ifdef _WIN32
#include <conio.h>
#include <windows.h>
#else // macOS/Linux
#include <termios.h>
#include <unistd.h>
#include <cstdlib>
#include <cstdio>
#endif

// --- Macros and Constants ---
#define BOARD_SIZE 8
#define BOX_WIDTH 10
#define DOWN 3
#define RIGHT 10
#define TO_DOWN std::string(DOWN, '\n')
#define TO_RIGHT std::string(RIGHT, ' ')
#define CLEAR_LINE std::string(100, ' ')
#define MOVES_PER_LINE 5

// --- Enums and Types ---

/**
 * @brief Enumeration of all chess piece types using signed integer values.
 *
 * Black pieces are represented by negative values [-6..-1], EMPTY is 0,
 * and white pieces are positive [1..6]. This scheme lets the code use sign
 * checks (< 0 = black, > 0 = white) throughout move generation and evaluation.
 *
 * Mapping:
 *   B_KING=-6, B_QUEEN=-5, B_BISHOP=-4, B_KNIGHT=-3, B_ROOK=-2, B_PAWN=-1,
 *   EMPTY=0,
 *   W_KING=1,  W_QUEEN=2,  W_BISHOP=3,  W_KNIGHT=4,  W_ROOK=5,  W_PAWN=6
 */
typedef enum {
    B_KING = -6, B_QUEEN, B_BISHOP, B_KNIGHT, B_ROOK, B_PAWN, EMPTY,
    W_KING, W_QUEEN, W_BISHOP, W_KNIGHT, W_ROOK, W_PAWN
} ChessPieces;

/**
 * @brief Classifies each board move into one of four special categories.
 *
 * NORMAL     - A standard piece move or capture.  
 * CASTLING   - King-side or queen-side castling move.  
 * PROMOTION  - A pawn that has reached the back rank and is being promoted.  
 * EN_PASSANT - A pawn capturing an adjacent enemy pawn en passant.
 */
typedef enum {
    NORMAL, CASTLING, PROMOTION, EN_PASSANT
} Moves;

/**
 * @brief Represents the possible game-ending conditions.
 *
 * CHECKMATE      - The current player has no legal moves and is in check.  
 * FIFTY_MOVES    - 50 moves without a pawn move or capture (draw).  
 * THREEFOLD_REP  - The same board position has occurred 3 times (draw).  
 * QUIT           - The human player typed "quit" or "exit".
 */
typedef enum {
    CHECKMATE, FIFTY_MOVES, THREEFOLD_REP, QUIT
} Endgame;

/**
 * @brief The canonical chess starting position encoded as a 2D char array.
 *
 * Row 0 (top)    = Black's back rank  (Rook, Knight, Bishop, Queen, King, ...)
 * Row 1          = Black's pawn rank
 * Rows 2-5       = Empty squares (EMPTY = 0)
 * Row 6          = White's pawn rank
 * Row 7 (bottom) = White's back rank
 *
 * Columns are indexed 0-7 mapping to files a-h (left to right from white's POV).
 */
const char STARTING_BOARD[BOARD_SIZE][BOARD_SIZE] = {
    {B_ROOK, B_KNIGHT, B_BISHOP, B_QUEEN, B_KING, B_BISHOP, B_KNIGHT, B_ROOK},
    {B_PAWN, B_PAWN, B_PAWN, B_PAWN, B_PAWN, B_PAWN, B_PAWN, B_PAWN},
    {EMPTY, EMPTY, EMPTY, EMPTY, EMPTY, EMPTY, EMPTY, EMPTY},
    {EMPTY, EMPTY, EMPTY, EMPTY, EMPTY, EMPTY, EMPTY, EMPTY},
    {EMPTY, EMPTY, EMPTY, EMPTY, EMPTY, EMPTY, EMPTY, EMPTY},
    {EMPTY, EMPTY, EMPTY, EMPTY, EMPTY, EMPTY, EMPTY, EMPTY},
    {W_PAWN, W_PAWN, W_PAWN, W_PAWN, W_PAWN, W_PAWN, W_PAWN, W_PAWN},
    {W_ROOK, W_KNIGHT, W_BISHOP, W_QUEEN, W_KING, W_BISHOP, W_KNIGHT, W_ROOK}
};

// --- Utility Functions ---

/**
 * @brief Moves the terminal cursor to the given (x, y) position.
 *
 * On Windows, uses the Win32 SetConsoleCursorPosition API.  
 * On macOS/Linux, emits the ANSI escape sequence ESC[y+1;x+1H.
 * This is the foundation of the in-place board refresh mechanism — instead of
 * reprinting the entire screen on every update, specific cells are redrawn by
 * positioning the cursor directly over them.
 *
 * @param x  Column index (0-based, left to right).
 * @param y  Row index    (0-based, top to bottom).
 */
#ifdef _WIN32
void MoveCursorToXY(const short &x, const short &y) noexcept {
    SetConsoleCursorPosition(GetStdHandle(STD_OUTPUT_HANDLE), (COORD){x, y});
}
#else
void MoveCursorToXY(const short &x, const short &y) noexcept {
    printf("\033[%d;%dH", y + 1, x + 1); // ANSI escape code, 1-based
    fflush(stdout);
}
#endif

/**
 * @brief Returns a lowercase copy of the given string.
 * @param s  The input string to convert.
 * @return   A new std::string with all characters converted to lowercase.
 */
std::string ToLowerString(std::string s) noexcept {
    std::transform(s.begin(), s.end(), s.begin(), [](const unsigned char &c){ return tolower(c); });
    return s;
}

/**
 * @brief Cross-platform single-character read without echo or line buffering.
 *
 * On Windows, the CRT <conio.h> `getch()` is used directly.  
 * On macOS/Linux, raw terminal mode is enabled via `termios`, a single
 * character is read, and then the terminal is restored to canonical mode.
 * Used for promotion selection and the "play again" prompt.
 */
#ifdef _WIN32
// getch is available via <conio.h> on Windows — no custom implementation needed.
#else
int getch() {
    struct termios oldt, newt;
    int ch;
    tcgetattr(STDIN_FILENO, &oldt);
    newt = oldt;
    newt.c_lflag &= ~(ICANON | ECHO);
    tcsetattr(STDIN_FILENO, TCSANOW, &newt);
    ch = getchar();
    tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
    return ch;
}
#endif

/**
 * @brief Returns a uniformly distributed random integer in [min, max] (inclusive).
 *
 * Uses the C standard `rand()` scaled to the desired range. The caller must
 * have seeded the RNG with `srand()` before first use.
 *
 * @tparam T   An arithmetic type (e.g., unsigned short, int).
 * @param min  Lower bound of the range (inclusive).
 * @param max  Upper bound of the range (inclusive).
 * @return     A random value T in [min, max].
 */
template<class T> T GetRandomNumber(const T &min, const T &max) noexcept {
    return min + T(static_cast<double>(rand()) / static_cast<double>(RAND_MAX+1.0) * (max-min+1));
}

// --- Forward Declarations ---
class Chess;
class Player;
class PathNode;
class Bot;

// =============================================================================
// --- Player Class ---
// =============================================================================

/**
 * @class Player
 * @brief Base class representing a chess player with a name, score, and
 *        castling eligibility.
 *
 * Both human and bot players are derived from (or use) this class. Key state:
 *   - @c name      : Display name shown in the terminal UI.
 *   - @c score     : Accumulated material captured, tracked in points.
 *   - @c castling  : Whether this player is still eligible to castle.
 *                   Set to `false` the first time either the king or a rook
 *                   moves, so castling rights are correctly revoked.
 *
 * The `castling` flag is used both in legal-move generation (to offer castling
 * as an option) and in the Threefold Repetition check (position identity
 * requires identical castling rights).
 */
class Player {
protected:
    std::string name;
    unsigned short score = 0;
    bool castling = true;
public:
    Player(const std::string &name) noexcept : name(name) {}
    std::string GetName() const noexcept { return name; }
    unsigned short GetScore() const noexcept { return score; }
    bool GetCastling() const noexcept { return castling; }
    void SetCastling(const bool &castling) noexcept { this->castling = castling; }
    void IncreaseScore(const unsigned short &inc) noexcept { score += inc; }
    void Reset() noexcept { score = 0; castling = true; }
    bool operator== (const Player &p) const noexcept { return !name.compare(p.name); }
};

// =============================================================================
// --- PathNode Class ---
// =============================================================================

/**
 * @class PathNode
 * @brief Represents a single node in the Minimax game-tree search.
 *
 * Each PathNode corresponds to one board state in the Alpha-Beta search tree.
 * The node lazily generates its children (legal moves) on demand via
 * `CreateSubtree()`. After each recursive call the child list is cleared to
 * keep memory usage bounded.
 *
 * Public interface:
 *   - `AlphaBetaRoot()` — entry point called by `Bot::GetIdealMove()`. Performs
 *     the root-level Minimax iteration and returns the best move string.
 *
 * Private helpers:
 *   - `CreateSubtree()` — populates `child_node_list` with all legal moves for
 *     the current board state.
 *   - `AlphaBeta()`     — the recursive Minimax function with Alpha-Beta pruning.
 */
class PathNode {
private:
    std::map<std::string, PathNode> child_node_list;
    void CreateSubtree(Chess &c) noexcept;
    float AlphaBeta(Chess &c, unsigned short &depth, float alpha, float beta, const bool &maximizing_player, const bool &initial_turn) noexcept;
public:
    std::string AlphaBetaRoot(Chess &c, unsigned short &difficulty) noexcept;
};

// =============================================================================
// --- Bot Class ---
// =============================================================================

/**
 * @class Bot
 * @brief A chess player controlled by the Minimax AI engine.
 *
 * Inherits from `Player` and adds:
 *   - `difficulty` — the search depth passed to `AlphaBeta` (1=Easy,
 *     2=Medium, 3=Hard). A higher depth means the bot looks further ahead
 *     but takes longer to move.
 *   - `root`       — the `PathNode` instance that initiates the tree search.
 *
 * `GetIdealMove()` is the primary entry point: it delegates to
 * `PathNode::AlphaBetaRoot()`, which returns the best move encoded as a
 * 4-character string (e.g., "e2e4").
 *
 * The `Bot` class also supports a "random" mode (used internally for the
 * easiest difficulty or bot-pitting scenarios) where moves are chosen
 * uniformly at random via `Chess::GetRandomMove()`.
 */
class Bot : public Player {
private:
    PathNode root;
    unsigned short difficulty;
public:
    Bot(const std::string &name, const unsigned short &difficulty) noexcept : Player(name), difficulty(difficulty) {}
    unsigned short GetDifficulty() const noexcept { return difficulty; }
    std::string GetIdealMove(Chess &c) noexcept { return root.AlphaBetaRoot(c, difficulty); }
    std::string GetIdealMove(Chess &c, unsigned short difficulty) noexcept { return root.AlphaBetaRoot(c, difficulty); }
    bool operator== (const Bot &b) const noexcept { return !name.compare(b.name); }
};

// =============================================================================
// --- Chess Class Declaration (Implementation Follows) ---
// =============================================================================

/**
 * @class Chess
 * @brief The central game engine: board state, rules engine, and UI renderer.
 *
 * `Chess` owns the 8x8 board (`char board[BOARD_SIZE][BOARD_SIZE]`), both
 * players, and the full game-history vector. It exposes a clean public API:
 *
 *   - `PrintBoard()`   — Full screen redraw of the board + UI chrome.
 *   - `PlayersTurn()`  — Handles human input, validates the move, and executes it.
 *   - `BotsTurn()`     — Asks the `Bot` for its best move and executes it.
 *   - `GameOver()`     — Post-game prompt: replay or quit.
 *   - `AllMoves()`     — Returns all legal moves for the current turn,
 *                        filtering out moves that leave the king in check.
 *   - `EvaluateBoard()`— Scores the board for the bot's evaluation function.
 *   - `MovePiece()`    — Executes a move, handling special rules.
 *   - `MovePieceBack()`— Undoes a move (used during tree search).
 *
 * ### Board Representation
 * The board is stored as `char board[8][8]` where each cell holds a
 * `ChessPieces` enum value. Row 0 = rank 8 (black's back rank), row 7 = rank 1
 * (white's back rank). Column 0 = file a, column 7 = file h.
 * Negative values are black pieces; positive values are white pieces; 0 = EMPTY.
 *
 * ### Game History
 * `all_game_moves` is a `std::vector<std::pair<Moves, std::string>>` that
 * records every move made. Each entry stores the move type and a payload
 * string encoding coordinates, piece moved, piece captured, castling rights,
 * and promotion target — enabling both algebraic-notation display and full move
 * reversal (undo) needed by the AI search.  
 */
class Chess {
private:
    char board[BOARD_SIZE][BOARD_SIZE];
    Bot white, black;
    std::vector<std::pair<Moves, std::string>> all_game_moves;
    bool whites_turn = true;
    unsigned short moves_after_last_pawn_move_or_capture = 0;
    bool white_bot_random;
    bool black_bot_random;
    static bool WithinBounds(const short &coord) noexcept;
    static void ChangeToString(char &x1, char &y1, char &x2, char &y2) noexcept;
    static std::string ToString(const short &x1, const short &y1, const short &x2, const short &y2) noexcept;
    static std::string PieceNameToString(const char &piece) noexcept;
    static float EvaluatePiece(const char &piece) noexcept;
    static void ClearAllMoves(const unsigned short &n) noexcept;
    static void PrintSeparator(const char &ch) noexcept;
    static void CopyBoard(const char from[BOARD_SIZE][BOARD_SIZE], char to[BOARD_SIZE][BOARD_SIZE]) noexcept;
    static bool AreBoardsEqual(const char board1[BOARD_SIZE][BOARD_SIZE], const char board2[BOARD_SIZE][BOARD_SIZE]) noexcept;
    static bool CanMovePiece(const short &x1, const short &y1, const short &x2, const short &y2, const std::forward_list<std::string> &all_moves) noexcept;
    Bot& GetCurrentPlayer() noexcept;
    Bot GetCurrentPlayerConst() const noexcept;
    Bot& GetOtherPlayer() noexcept;
    Bot GetOtherPlayerConst() const noexcept;
    void ChangeTurn() noexcept;
    void AppendToAllGameMoves(const short &x1, const short &y1, const short &x2, const short &y2) noexcept;
    void Reset() noexcept;
    void CheckCoordinates(const short &x, const short &y, const std::string &func_name) const noexcept(false);
    bool EndGameText(const unsigned short &n, const Endgame &end_game) const noexcept;
    short GetEnPassant(const short &x, const short &y) const noexcept;
    template<class Iterator> short GetEnPassant(const char board[BOARD_SIZE][BOARD_SIZE], const Iterator &it) const noexcept;
    bool ThreefoldRepetition() const noexcept;
    bool IsCheck(const bool &turn) const noexcept;
    bool IsCheck(std::string &move) noexcept;
    std::forward_list<std::string> PawnMoves(const short &x, const short &y) const noexcept;
    std::forward_list<std::string> RookMoves(const short &x, const short &y) const noexcept;
    std::forward_list<std::string> KnightMoves(const short &x, const short &y) const noexcept;
    std::forward_list<std::string> BishopMoves(const short &x, const short &y) const noexcept;
    std::forward_list<std::string> QueenMoves(const short &x, const short &y) const noexcept;
    std::forward_list<std::string> KingMoves(const short &x, const short &y) const noexcept;
    std::string GetRandomMove() noexcept;
    void ManuallyPromotePawn(const short &x, const short &y) noexcept;
    void UpdateBoard(const short &x, const short &y) const noexcept;
    void UpdateScore(const Bot &p) const noexcept;
    float EvaluatePosition(const short &x, const short &y) const noexcept;
    void PrintAllMovesMadeInOrder() const noexcept;
    bool CheckEndgame(const unsigned short &n = 0) noexcept;
public:
    Chess(const std::string &player1, const unsigned short &difficulty1, const std::string &player2, const unsigned short &difficulty2, bool white_bot_random = false, bool black_bot_random = false) noexcept;
    static void ChangeToRealCoordinates(char &x1, char &y1, char &x2, char &y2) noexcept;
    char GetPiece(const short &x, const short &y) const noexcept;
    bool GetTurn() const noexcept;
    std::forward_list<std::string> AllMoves() noexcept;
    void MovePiece(const short &x1, const short &y1, const short &x2, const short &y2, const bool &manual_promotion, const bool &update_board) noexcept;
    void MovePieceBack(const short &x1, const short &y1, const short &x2, const short &y2) noexcept;
    float EvaluateBoard(const bool &turn) const noexcept;
    void PrintBoard() const noexcept;
    bool PlayersTurn() noexcept;
    bool BotsTurn() noexcept;
    bool GameOver() noexcept;
};

// =============================================================================
// --- PathNode Implementation ---
// =============================================================================

/**
 * @brief Populates `child_node_list` with all legal moves for the current board.
 *
 * Calls `Chess::AllMoves()` to enumerate every legal move for the side to move,
 * then inserts each move (in ASCII notation, e.g. "e2e4") as a key into the
 * `child_node_list` map. The value for each key is a default-constructed
 * `PathNode` — its children are generated lazily on the next recursive call.
 *
 * @param c  The current game state from which legal moves are generated.
 */
void PathNode::CreateSubtree(Chess &c) noexcept {
    auto all_moves = c.AllMoves();
    for(auto &move : all_moves) {
        Chess::ChangeToRealCoordinates(move[0], move[1], move[2], move[3]);
        child_node_list.emplace(move, PathNode());
    }
}

/**
 * @brief Recursive Minimax search with Alpha-Beta pruning.
 *
 * This is the core AI algorithm. It performs a depth-limited, negamax-style
 * Minimax search where the maximizing player seeks the highest board score and
 * the minimizing player seeks the lowest.
 *
 * ### Alpha-Beta Pruning
 * `alpha` is the best score the maximizing player is guaranteed so far.  
 * `beta`  is the best score the minimizing player is guaranteed so far.  
 * If at any node `alpha >= beta`, we know the opponent will never allow this
 * branch to be reached, so we prune it immediately (the `break` statement).
 * This reduces the effective branching factor dramatically, allowing the same
 * depth to be searched much faster than plain Minimax.
 *
 * ### King Capture Short-Circuit
 * Before recursing, the code checks if a child move directly captures the
 * opponent king. If so, there is no need to search further — a king capture is
 * an immediate terminal win/loss, so ±9999 is returned directly.
 *
 * ### Move / Undo
 * `Chess::MovePiece(..., false, false)` applies the move without updating the
 * terminal display. `Chess::MovePieceBack(...)` undoes it, restoring the exact
 * board state (including en passant and castling flags) for backtracking.
 *
 * @param c                  The current game state (modified in place, then restored).
 * @param depth              Remaining search depth; decremented on each recursive call.
 * @param alpha              Best value the maximizer can guarantee (−∞ initially).
 * @param beta               Best value the minimizer can guarantee (+∞ initially).
 * @param maximizing_player  True if this node is a maximizing node.
 * @param initial_turn       The colour (turn) of the bot calling the search, used
 *                           so `EvaluateBoard` returns the score relative to that side.
 * @return                   The heuristic value of best reachable position.
 */
float PathNode::AlphaBeta(Chess &c, unsigned short &depth, float alpha, float beta, const bool &maximizing_player, const bool &initial_turn) noexcept {
    // Base case: depth exhausted — evaluate the current board position.
    if(!depth)
        return c.EvaluateBoard(initial_turn);
    CreateSubtree(c);
    // Initialise to worst-case value for this player:
    //   maximizer starts at -∞, minimizer starts at +∞.
    float points = maximizing_player ? -9999 : 9999;
    for(auto &node : child_node_list) {
        // Short-circuit: if this move captures the king, it is an immediate win.
        // Return the best possible value without deeper search.
        if(c.GetPiece(node.first[2], node.first[3]) == W_KING - 7*c.GetTurn()) {
            child_node_list.clear();
            return maximizing_player ? 9999 : -9999;
        }
        // Apply the move without rendering the board (false, false).
        c.MovePiece(node.first[0], node.first[1], node.first[2], node.first[3], false, false);
        // Recurse: alternate maximizer/minimizer, decrement depth.
        points = maximizing_player ? std::max(points, node.second.AlphaBeta(c, --depth, alpha, beta, false, initial_turn))
        : std::min(points, node.second.AlphaBeta(c, --depth, alpha, beta, true, initial_turn));
        // Update alpha (maximizer's floor) or beta (minimizer's ceiling).
        maximizing_player ? alpha = std::max(alpha, points) : beta = std::min(beta, points);
        ++depth;  // Restore depth counter after returning from recursion.
        c.MovePieceBack(node.first[0], node.first[1], node.first[2], node.first[3]);
        // Alpha-Beta pruning cut-off: remaining siblings cannot improve the outcome.
        if(alpha >= beta)
            break;
    }
    child_node_list.clear(); // Free child nodes; they are regenerated each call.
    return points;
}

/**
 * @brief Entry point for the bot's move selection; calls AlphaBeta for each root move.
 *
 * Unlike the recursive `AlphaBeta()`, this function always acts as the
 * maximizing player at depth 0 (the root). It iterates over all legal moves,
 * calls `AlphaBeta()` to evaluate each one at the requested depth, and
 * collects all moves that achieve the maximum score.
 *
 * If multiple moves share the top score, one is chosen at random (via
 * `GetRandomNumber`). This mild randomisation prevents the bot from always
 * playing the same opening and makes it harder to exploit at easy difficulties.
 *
 * @param c           The current game state.
 * @param difficulty  Search depth (ply). 1=Easy, 2=Medium, 3=Hard.
 * @return            The best move as a 4-char string (ASCII file+rank notation).
 */
std::string PathNode::AlphaBetaRoot(Chess &c, unsigned short &difficulty) noexcept {
    CreateSubtree(c);
    std::vector<std::string> ideal_moves;
    float max_move_score = -9999;
    for(auto &node : child_node_list) {
        if(c.GetPiece(node.first[2], node.first[3]) == W_KING - 7*c.GetTurn()) {
            child_node_list.clear();
            return node.first;
        }
        c.MovePiece(node.first[0], node.first[1], node.first[2], node.first[3], false, false);
        float move_score = node.second.AlphaBeta(c, difficulty, -10000, 10000, false, !c.GetTurn());
        if(move_score > max_move_score) {
            max_move_score = move_score;
            ideal_moves.clear();
            ideal_moves.emplace_back(node.first);
        }
        else if(move_score == max_move_score)
            ideal_moves.emplace_back(node.first);
        c.MovePieceBack(node.first[0], node.first[1], node.first[2], node.first[3]);
    }
    child_node_list.clear();
    auto move = ideal_moves.cbegin();
    advance(move, GetRandomNumber<unsigned short>(0, ideal_moves.size()-1));
    return *move;
}

// =============================================================================
// --- Chess Implementation ---
// =============================================================================

/**
 * @brief Constructs a Chess game with two named players and their bot settings.
 *
 * Copies the canonical starting position into `board`, and initialises
 * both `Bot` objects with their names and search depths. The
 * `white_bot_random` / `black_bot_random` flags are set by the game-mode
 * selection in `main()` for the EvE scenario.
 *
 * @param player1           Display name for the white side.
 * @param difficulty1       Minimax search depth for white (0 = human/random).
 * @param player2           Display name for the black side.
 * @param difficulty2       Minimax search depth for black.
 * @param white_bot_random  If true, white moves randomly instead of using Minimax.
 * @param black_bot_random  If true, black moves randomly instead of using Minimax.
 */
Chess::Chess(const std::string &player1, const unsigned short &difficulty1, const std::string &player2, const unsigned short &difficulty2, bool white_bot_random, bool black_bot_random) noexcept
: white(player1, difficulty1), black(player2, difficulty2), white_bot_random(white_bot_random), black_bot_random(black_bot_random) {
    CopyBoard(STARTING_BOARD, board);
}

/**
 * @brief Returns true if `coord` is a valid board index [0, BOARD_SIZE).
 * @param coord  A row or column index.
 * @return       True if the index is on the board.
 */
bool Chess::WithinBounds(const short &coord) noexcept {
    return coord>=0 && coord<BOARD_SIZE;
}

/**
 * @brief Converts ASCII move notation to integer board coordinates (in-place).
 *
 * The standard user-input format uses algebraic file letters and rank digits,
 * e.g. "d3". This function converts them to (col, row) array indices:
 *   - x: 'a'=0 .. 'h'=7  (subtract ASCII 'a')
 *   - y: '8'=0 .. '1'=7  (subtract from '8', since row 0 is the top of the array)
 *
 * @param x1,y1  Starting square (modified in place).
 * @param x2,y2  Ending square   (modified in place).
 */
void Chess::ChangeToRealCoordinates(char &x1, char &y1, char &x2, char &y2) noexcept {
    x1 -= 'a', x2 -= 'a';
    y1 = '8'-y1, y2 = '8'-y2;
}

/**
 * @brief Converts integer board coordinates back to ASCII notation (in-place).
 *
 * The inverse of `ChangeToRealCoordinates`. Used when building user-visible
 * move strings and when restoring a move after `IsCheck` calls.
 *
 * @param x1,y1  Starting square integer coords (modified in place to ASCII).
 * @param x2,y2  Ending square integer coords (modified in place to ASCII).
 */
void Chess::ChangeToString(char &x1, char &y1, char &x2, char &y2) noexcept {
    x1 += 'a', x2 += 'a';
    y1 = '8'-y1, y2 = '8'-y2;
}

/**
 * @brief Encodes four integer board coordinates into a 4-char std::string.
 *
 * Produces strings like "e2e4" (file a-h, rank 8-1). Used throughout the
 * engine as the canonical move serialisation format.
 *
 * @param x1,y1  Source square (column, row) in integer [0..7] coords.
 * @param x2,y2  Dest   square (column, row) in integer [0..7] coords.
 * @return       A 4-character string e.g. "d1h5".
 */
std::string Chess::ToString(const short &x1, const short &y1, const short &x2, const short &y2) noexcept {
    return {static_cast<char>(x1+'a'), static_cast<char>('8'-y1), static_cast<char>(x2+'a'), static_cast<char>('8'-y2)};
}

/**
 * @brief Maps a piece enum value to its display name string.
 *
 * Returns strings like "W_PAWN", "B_KING", etc., which are used both in the
 * terminal board rendering (centred inside each cell) and in the move history
 * log printed at game over.
 *
 * @param piece  A `ChessPieces` enum value.
 * @return       Display name, or an empty string for EMPTY squares.
 */
std::string Chess::PieceNameToString(const char &piece) noexcept {
    switch(piece) {
        case W_PAWN:    return "W_PAWN";
        case B_PAWN:    return "B_PAWN";
        case W_ROOK:    return "W_ROOK";
        case B_ROOK:    return "B_ROOK";
        case W_KNIGHT:  return "W_KNIGHT";
        case B_KNIGHT:  return "B_KNIGHT";
        case W_BISHOP:  return "W_BISHOP";
        case B_BISHOP:  return "B_BISHOP";
        case W_QUEEN:   return "W_QUEEN";
        case B_QUEEN:   return "B_QUEEN";
        case W_KING:    return "W_KING";
        case B_KING:    return "B_KING";
        default:        return "";
    }
}

/**
 * @brief Returns the material value of a piece in centipawn-like units.
 *
 * These classic Shannon/Reinfeld values are used in two places:
 *   1. Adding to a player's displayed score when a piece is captured.
 *   2. As the base component of `EvaluatePosition()` before positional bonuses.
 *
 * Standard values:
 *   Pawn=10, Rook=50, Knight=Bishop=30, Queen=90, King=900.
 *
 * @param piece  A `ChessPieces` enum value.
 * @return       The material worth of the piece (0 for EMPTY).
 */
float Chess::EvaluatePiece(const char &piece) noexcept {
    switch(piece) {
        case W_PAWN:
        case B_PAWN:    return 10;
        case W_ROOK:
        case B_ROOK:    return 50;
        case W_KNIGHT:
        case B_KNIGHT:
        case W_BISHOP:
        case B_BISHOP:  return 30;
        case W_QUEEN:
        case B_QUEEN:   return 90;
        case W_KING:
        case B_KING:    return 900;
        default:        return 0;
    }
}

void Chess::ClearAllMoves(const unsigned short &n) noexcept {
    MoveCursorToXY(0, DOWN + 3*BOARD_SIZE + 9);
    for(unsigned short i=0;i<n;++i)
        std::cout << CLEAR_LINE << std::endl;
}

void Chess::PrintSeparator(const char &ch) noexcept {
    for(unsigned short i=1;i<BOARD_SIZE;++i)
        std::cout << std::string(BOX_WIDTH, ch) << "|";
    std::cout << std::string(BOX_WIDTH, ch) << std::endl << TO_RIGHT;
}

void Chess::CopyBoard(const char from[BOARD_SIZE][BOARD_SIZE], char to[BOARD_SIZE][BOARD_SIZE]) noexcept {
    std::copy(*from, *from + BOARD_SIZE*BOARD_SIZE, *to);
}

bool Chess::AreBoardsEqual(const char board1[BOARD_SIZE][BOARD_SIZE], const char board2[BOARD_SIZE][BOARD_SIZE]) noexcept {
    return std::equal(*board1, *board1 + BOARD_SIZE*BOARD_SIZE, *board2);
}

bool Chess::CanMovePiece(const short &x1, const short &y1, const short &x2, const short &y2, const std::forward_list<std::string> &all_moves) noexcept {
    return std::find(all_moves.cbegin(), all_moves.cend(), ToString(x1, y1, x2, y2)) != all_moves.cend();
}

char Chess::GetPiece(const short &x, const short &y) const noexcept {
    return board[y][x];
}

bool Chess::GetTurn() const noexcept {
    return whites_turn;
}

Bot& Chess::GetCurrentPlayer() noexcept {
    return whites_turn ? white : black;
}

Bot Chess::GetCurrentPlayerConst() const noexcept {
    return whites_turn ? white : black;
}

Bot& Chess::GetOtherPlayer() noexcept {
    return whites_turn ? black : white;
}

Bot Chess::GetOtherPlayerConst() const noexcept {
    return whites_turn ? black : white;
}

void Chess::ChangeTurn() noexcept {
    whites_turn = !whites_turn;
}

void Chess::AppendToAllGameMoves(const short &x1, const short &y1, const short &x2, const short &y2) noexcept {
    if(GetCurrentPlayerConst().GetCastling() && (board[y1][x1] == B_KING + 7*whites_turn) && (x2 == 2 || x2 == 6))
        all_game_moves.emplace_back(CASTLING, std::string(1, x2));
    else
        all_game_moves.emplace_back(NORMAL, ToString(x1, y1, x2, y2) + board[y1][x1] + board[y2][x2]);
}

void Chess::Reset() noexcept {
    CopyBoard(STARTING_BOARD, board);
    white.Reset();
    black.Reset();
    all_game_moves.clear();
    whites_turn = true;
    moves_after_last_pawn_move_or_capture = 0;
#ifdef _WIN32
    system("cls");
#else
    system("clear");
#endif
}

void Chess::CheckCoordinates(const short &x, const short &y, const std::string &func_name) const noexcept(false) {
    try {
        if(!WithinBounds(x))        throw x;
        if(!WithinBounds(y))        throw y;
    }
    catch(const short &coord) {
        std::cerr << std::endl << std::endl << TO_RIGHT << "!ERROR!\t\tInvalid coordinate: '" << coord << "'.\t\t!ERROR!";
        std::cerr << std::endl << TO_RIGHT << "      \t\tException occurred in \"" << func_name << "\".";
        PrintAllMovesMadeInOrder();
        exit(1);
    }
}

bool Chess::EndGameText(const unsigned short &n, const Endgame &end_game) const noexcept {
    ClearAllMoves(n);
    MoveCursorToXY(RIGHT, DOWN + 3*BOARD_SIZE + 7);
    switch(end_game) {
        case CHECKMATE:
            std::cout << "!!!Checkmate!!!" << CLEAR_LINE << std::endl << TO_RIGHT << GetOtherPlayerConst().GetName() << " wins!";
            return true;
        default:
            std::cout << "!!!Draw!!!" << CLEAR_LINE << std::endl << TO_RIGHT;
            switch(end_game) {
                case FIFTY_MOVES:
                    std::cout << "Fifty-move rule: No capture has been made and no pawn has been moved in the last 50 moves.";
                    return true;
                case THREEFOLD_REP:
                    std::cout << "Threefold repetition: Last position occured 3 times during the game.";
                    return true;
                default:
                    return false;
            }
    }
}

/**
 * @brief Returns the x-coordinate of the pawn that can be taken en passant, or -1.
 *
 * En passant is only legal if:
 *   1. The previous move was a NORMAL pawn move (not castling/promotion/EP).
 *   2. The opponent's pawn moved two squares from its starting rank in that move.
 *   3. The attacking pawn (at position x,y) is on the correct rank (rank 4 or 5).
 *   4. The opponent's pawn landed on an adjacent file (|dx| == 1).
 *
 * Uses the last entry of `all_game_moves` which stores the full move payload
 * (source square, dest square, piece moved, piece captured, castling flag).
 *
 * @param x  File of the attacking pawn (0-7).
 * @param y  Rank of the attacking pawn (0-7, 0=rank8).
 * @return   File of the pawn that can be captured en passant, or -1 if none.
 */
short Chess::GetEnPassant(const short &x, const short &y) const noexcept {
    if(all_game_moves.empty())
        return -1;
    if(all_game_moves.back().first != NORMAL)
        return -1;
    auto last_move = all_game_moves.back().second;
    ChangeToRealCoordinates(last_move[0], last_move[1], last_move[2], last_move[3]);
    return ((last_move[4] == W_PAWN - 7*whites_turn) && (abs(last_move[0] - x) == 1) && (last_move[3]-last_move[1] == 2*(whites_turn ? 1 : -1)) && (y == 4 - whites_turn)) ? last_move[0] : -1;
}

/**
 * @brief Template overload of GetEnPassant for use during Threefold Repetition checking.
 *
 * When reconstructing past board states to check for repeated positions,
 * `ThreefoldRepetition()` needs to determine en passant availability at
 * historical positions (not just the current one). This overload accepts an
 * arbitrary `board` array and a history iterator to check against a past move.
 *
 * @tparam Iterator  A const reverse/forward iterator over `all_game_moves`.
 * @param board  The historical board state to test.
 * @param it     Iterator pointing to the move to examine.
 * @return       File of the pawn capturable en passant, or -1 if none.
 */
template<class Iterator> short Chess::GetEnPassant(const char board[BOARD_SIZE][BOARD_SIZE], const Iterator &it) const noexcept {
    if(it->first != NORMAL)
        return -1;
    auto last_move = it->second;
    ChangeToRealCoordinates(last_move[0], last_move[1], last_move[2], last_move[3]);
    for(short x=0;x<BOARD_SIZE;++x)
        if(board[3 + whites_turn][x] == W_PAWN - 7*whites_turn)
            if((last_move[4] == B_PAWN + 7*whites_turn) && (abs(last_move[0] - x) == 1) && (last_move[3] - last_move[1] == 2*(whites_turn ? -1 : 1)))
                return last_move[0];
    return -1;
}

/**
 * @brief Checks whether the current position has occurred at least 3 times.
 *
 * Walks backward through `all_game_moves` two half-moves at a time (undoing
 * one full round per iteration), reconstructing past board states in
 * `prev_board`. For each reconstructed state it checks strict position identity:
 *   - The 64 squares must match the current board.
 *   - Castling rights of the player-to-move must match.
 *   - En passant availability (if any) must match.
 *
 * Early exits:
 *   - Any castling move encountered — the board state before castling is not
 *     comparable to the current state in standard threefold repetition.
 *   - A pawn move or capture — these are irreversible, so no earlier position
 *     can repeat the current one.
 *
 * @return True if the current position has occurred 3 or more times.
 */
bool Chess::ThreefoldRepetition() const noexcept {
    static char prev_board[BOARD_SIZE][BOARD_SIZE];
    CopyBoard(board, prev_board);
    unsigned short position_count = 1;
    auto it = all_game_moves.crbegin();
    auto last_move = it->second;
    while(true) {
        for(unsigned short i=0;i<2;++i)    {
            switch(it->first) {
                case CASTLING:
                    return false;
                default:
                    if(last_move[4] == W_PAWN || last_move[4] == B_PAWN || last_move[5] != EMPTY)
                        return false;
                    ChangeToRealCoordinates(last_move[0], last_move[1], last_move[2], last_move[3]);
                    prev_board[short(last_move[1])][short(last_move[0])] = last_move[4], prev_board[short(last_move[3])][short(last_move[2])] = EMPTY;
                    if(it->first == EN_PASSANT)
                        prev_board[short(last_move[1])][short(last_move[2])] = i == whites_turn ? B_PAWN : W_PAWN;
            }
            if((++it) == all_game_moves.crend())
                return false;
            last_move = it->second;
        }
        if(AreBoardsEqual(prev_board, board))
            if(GetOtherPlayerConst().GetCastling() == (it->first == CASTLING ? false : last_move[6 + (it->first == PROMOTION)]))
                if((all_game_moves.size() > 1 ? GetEnPassant(board, prev(all_game_moves.cend(), 2)) : -1)
                == (next(it) == all_game_moves.crend() ? -1 : GetEnPassant(prev_board, next(it))))
                    if((++position_count) == 3)
                        return true;
    }
}

/**
 * @brief Tests whether the king of colour `turn` is in check on the current board.
 *
 * Implements the classic attack-ray approach:
 *   1. Locate the king for the given colour by scanning the board.
 *   2. Cast rays in 4 orthogonal directions for rook/queen threats.
 *   3. Cast rays in 4 diagonal directions for bishop/queen threats.
 *   4. Check all 8 king-adjacent squares for an opposing king.
 *   5. Check all 8 knight-jump squares for an opposing knight.
 *   6. Check the two pawn-attack diagonals (direction depends on colour).
 *
 * The formula `W_ROOK - 7*turn` selects the opponent's piece type: when
 * `turn==true` (white's king), it yields the black rook (W_ROOK-7=B_ROOK).
 *
 * @param turn  True = check if WHITE king is under attack; false = BLACK king.
 * @return      True if the king is currently in check.
 */
bool Chess::IsCheck(const bool &turn) const noexcept {
    short x = -1, y = -1;
    for(short i=0;x==-1;++i)
        for(short j=0;j<BOARD_SIZE;++j)
            if(board[j][i] == B_KING + 7*turn) {
                x = i, y = j;
                break;
            }
    for(short i=x+1;i<BOARD_SIZE;++i)
        if(board[y][i] == W_ROOK - 7*turn)            return true;
        else if(board[y][i] == W_QUEEN - 7*turn)    return true;
        else if(board[y][i] != EMPTY)    break;
    for(short i=x-1;i>=0;--i)
        if(board[y][i] == W_ROOK - 7*turn)            return true;
        else if(board[y][i] == W_QUEEN - 7*turn)    return true;
        else if(board[y][i] != EMPTY)    break;
    for(short i=y+1;i<BOARD_SIZE;++i)
        if(board[i][x] == W_ROOK - 7*turn)            return true;
        else if(board[i][x] == W_QUEEN - 7*turn)    return true;
        else if(board[i][x] != EMPTY)    break;
    for(short i=y-1;i>=0;--i)
        if(board[i][x] == W_ROOK - 7*turn)            return true;
        else if(board[i][x] == W_QUEEN - 7*turn)    return true;
        else if(board[i][x] != EMPTY)    break;
    for(short i=x-1, j=y-1; i>=0 && j>=0; --i, --j)
        if(board[j][i] == W_BISHOP - 7*turn)        return true;
        else if(board[j][i] == W_QUEEN - 7*turn)    return true;
        else if(board[j][i] != EMPTY)    break;
    for(short i=x-1, j=y+1; i>=0 && j<BOARD_SIZE; --i, ++j)
        if(board[j][i] == W_BISHOP - 7*turn)        return true;
        else if(board[j][i] == W_QUEEN - 7*turn)    return true;
        else if(board[j][i] != EMPTY)    break;
    for(short i=x+1, j=y-1; i<BOARD_SIZE && j>=0; ++i, --j)
        if(board[j][i] == W_BISHOP - 7*turn)        return true;
        else if(board[j][i] == W_QUEEN - 7*turn)    return true;
        else if(board[j][i] != EMPTY)    break;
    for(short i=x+1, j=y+1; i<BOARD_SIZE && j<BOARD_SIZE; ++i, ++j)
        if(board[j][i] == W_BISHOP - 7*turn)        return true;
        else if(board[j][i] == W_QUEEN - 7*turn)    return true;
        else if(board[j][i] != EMPTY)    break;
    for(short i=x-1;i<x+2;++i)
        for(short j=y-1;j<y+2;++j)
            if((board[j][i] == W_KING - 7*turn) && WithinBounds(i) && WithinBounds(j))            return true;
    if((board[y-1][x-2] == W_KNIGHT - 7*turn) && (y > 0) && (x > 1))                            return true;
    else if((board[y-1][x+2] == W_KNIGHT - 7*turn) && (y > 0) && (x < BOARD_SIZE-2))            return true;
    else if((board[y+1][x-2] == W_KNIGHT - 7*turn) && (y < BOARD_SIZE-1) && (x > 1))            return true;
    else if((board[y+1][x+2] == W_KNIGHT - 7*turn) && (y < BOARD_SIZE-1) && (x < BOARD_SIZE-2))    return true;
    else if((board[y-2][x-1] == W_KNIGHT - 7*turn) && (y > 1) && (x > 0))                        return true;
    else if((board[y-2][x+1] == W_KNIGHT - 7*turn) && (y > 1) && (x < BOARD_SIZE-1))            return true;
    else if((board[y+2][x-1] == W_KNIGHT - 7*turn) && (y < BOARD_SIZE-2) && (x > 0))            return true;
    else if((board[y+2][x+1] == W_KNIGHT - 7*turn) && (y < BOARD_SIZE-2) && (x < BOARD_SIZE-1))    return true;
    else if((board[y + (turn ? -1 : 1)][x+1] == W_PAWN - 7*turn) && (x < BOARD_SIZE-1))            return true;
    else if((board[y + (turn ? -1 : 1)][x-1] == W_PAWN - 7*turn) && (x > 0))                    return true;
    return false;
}

/**
 * @brief Validates a candidate move string by playing it and checking for check.
 *
 * Converts the ASCII move string to board coordinates, makes the move
 * (without display update), tests `IsCheck` from the perspective of the
 * now-previous player (i.e., the player who just moved), then undoes the move
 * and restores the string to ASCII. Used by `AllMoves()` to filter out moves
 * that would leave the player's own king in check.
 *
 * @param move  4-char ASCII move string, modified in place during the check
 *              (coordinates converted then restored).
 * @return      True if the move leaves the mover's king in check (illegal).
 */
bool Chess::IsCheck(std::string &move) noexcept {
    ChangeToRealCoordinates(move[0], move[1], move[2], move[3]);
    MovePiece(move[0], move[1], move[2], move[3], false, false);
    const bool &is_check = IsCheck(!whites_turn);
    MovePieceBack(move[0], move[1], move[2], move[3]);
    ChangeToString(move[0], move[1], move[2], move[3]);
    return is_check;
}

/**
 * @brief Generates all pseudo-legal pawn moves from square (x, y).
 *
 * Considers:
 *   - Single push forward (if the destination is empty).
 *   - Double push from the starting rank (rows 6 for white, row 1 for black).
 *   - Diagonal captures (only onto squares occupied by the opponent).
 *   - En passant capture (detected via `GetEnPassant()`).
 *
 * Note: These are pseudo-legal — moves that leave the king in check are
 * filtered out later in `AllMoves()`.
 *
 * @param x  File of the pawn (0-7).
 * @param y  Rank of the pawn (0-7, 0=rank8).
 * @return   List of legal-candidate move strings for this pawn.
 */
std::forward_list<std::string> Chess::PawnMoves(const short &x, const short &y) const noexcept {
    const auto &IsValid = whites_turn ? [](const char &ch){ return ch < 0; } : [](const char &ch){ return ch > 0; };
    const short &inc = whites_turn ? -1 : 1;
    std::forward_list<std::string> all_moves;
    if(board[y+inc][x] == EMPTY) {
        all_moves.emplace_front(ToString(x, y, x, y+inc));
        if((y == 1 + 5*whites_turn) && (board[y + 2*inc][x] == EMPTY))
            all_moves.emplace_front(ToString(x, y, x, y + 2*inc));
    }
    if(GetEnPassant(x, y) != -1)
        all_moves.emplace_front(ToString(x, y, GetEnPassant(x, y), y+inc));
    if(IsValid(board[y+inc][x+1]) && (x < BOARD_SIZE-1))
        all_moves.emplace_front(ToString(x, y, x+1, y+inc));
    if(IsValid(board[y+inc][x-1]) && (x > 0))
        all_moves.emplace_front(ToString(x, y, x-1, y+inc));
    return all_moves;
}

/**
 * @brief Generates all pseudo-legal rook moves from square (x, y).
 *
 * Casts rays in 4 orthogonal directions (right, left, down, up), adding each
 * empty square. The ray stops when it hits any piece; if that piece belongs to
 * the opponent it is added as a capture and then the ray stops.
 *
 * @param x  File of the rook (0-7).
 * @param y  Rank of the rook (0-7).
 * @return   List of legal-candidate moves for this rook.
 */
std::forward_list<std::string> Chess::RookMoves(const short &x, const short &y) const noexcept {
    const auto &IsValid = whites_turn ? [](const char &ch){ return ch < 0; } : [](const char &ch){ return ch > 0; };
    std::forward_list<std::string> all_moves;
    for(short i=x+1;i<BOARD_SIZE;++i)
        if(board[y][i] == EMPTY)
            all_moves.emplace_front(ToString(x, y, i, y));
        else {
            if(IsValid(board[y][i]))
                all_moves.emplace_front(ToString(x, y, i, y));
            break;
        }
    for(short i=x-1;i>=0;--i)
        if(board[y][i] == EMPTY)
            all_moves.emplace_front(ToString(x, y, i, y));
        else {
            if(IsValid(board[y][i]))
                all_moves.emplace_front(ToString(x, y, i, y));
            break;
        }
    for(short i=y+1;i<BOARD_SIZE;++i)
        if(board[i][x] == EMPTY)
            all_moves.emplace_front(ToString(x, y, x, i));
        else {
            if(IsValid(board[i][x]))
                all_moves.emplace_front(ToString(x, y, x, i));
            break;
        }
    for(short i=y-1;i>=0;--i)
        if(board[i][x] == EMPTY)
            all_moves.emplace_front(ToString(x, y, x, i));
        else {
            if(IsValid(board[i][x]))
                all_moves.emplace_front(ToString(x, y, x, i));
            break;
        }
    return all_moves;
}

/**
 * @brief Generates all pseudo-legal knight moves from square (x, y).
 *
 * Checks all 8 L-shaped offsets. Bounds checks are applied before accessing
 * the board to prevent out-of-bounds reads. A knight can land on any empty
 * square or any opponent-occupied square (it jumps over pieces).
 *
 * @param x  File of the knight (0-7).
 * @param y  Rank of the knight (0-7).
 * @return   List of legal-candidate moves for this knight.
 */
std::forward_list<std::string> Chess::KnightMoves(const short &x, const short &y) const noexcept {
    const auto &IsValid = whites_turn ? [](const char &ch){ return ch <= 0; } : [](const char &ch){ return ch >= 0; };
    std::forward_list<std::string> all_moves;
    if(IsValid(board[y-1][x-2]) && (y > 0) && (x > 1))
        all_moves.emplace_front(ToString(x, y, x-2, y-1));
    if(IsValid(board[y-1][x+2]) && (y > 0) && (x < BOARD_SIZE-2))
        all_moves.emplace_front(ToString(x, y, x+2, y-1));
    if(IsValid(board[y+1][x-2]) && (y < BOARD_SIZE-1) && (x > 1))
        all_moves.emplace_front(ToString(x, y, x-2, y+1));
    if(IsValid(board[y+1][x+2]) && (y < BOARD_SIZE-1) && (x < BOARD_SIZE-2))
        all_moves.emplace_front(ToString(x, y, x+2, y+1));
    if(IsValid(board[y-2][x-1]) && (y > 1) && (x > 0))
        all_moves.emplace_front(ToString(x, y, x-1, y-2));
    if(IsValid(board[y-2][x+1]) && (y > 1) && (x < BOARD_SIZE-1))
        all_moves.emplace_front(ToString(x, y, x+1, y-2));
    if(IsValid(board[y+2][x-1]) && (y < BOARD_SIZE-2) && (x > 0))
        all_moves.emplace_front(ToString(x, y, x-1, y+2));
    if(IsValid(board[y+2][x+1]) && (y < BOARD_SIZE-2) && (x < BOARD_SIZE-1))
        all_moves.emplace_front(ToString(x, y, x+1, y+2));
    return all_moves;
}

/**
 * @brief Generates all pseudo-legal bishop moves from square (x, y).
 *
 * Casts rays in 4 diagonal directions, stopping rules identical to `RookMoves`.
 *
 * @param x  File of the bishop (0-7).
 * @param y  Rank of the bishop (0-7).
 * @return   List of legal-candidate moves for this bishop.
 */
std::forward_list<std::string> Chess::BishopMoves(const short &x, const short &y) const noexcept {
    const auto &IsValid = whites_turn ? [](const char &ch){ return ch < 0; } : [](const char &ch){ return ch > 0; };
    std::forward_list<std::string> all_moves;
    for(short i=x-1, j=y-1; i>=0 && j>=0; --i, --j)
        if(board[j][i] == EMPTY)
            all_moves.emplace_front(ToString(x, y, i, j));
        else {
            if(IsValid(board[j][i]))
                all_moves.emplace_front(ToString(x, y, i, j));
            break;
        }
    for(short i=x-1, j=y+1; i>=0 && j<BOARD_SIZE; --i, ++j)
        if(board[j][i] == EMPTY)
            all_moves.emplace_front(ToString(x, y, i, j));
        else {
            if(IsValid(board[j][i]))
                all_moves.emplace_front(ToString(x, y, i, j));
            break;
        }
    for(short i=x+1, j=y-1; i<BOARD_SIZE && j>=0; ++i, --j)
        if(board[j][i] == EMPTY)
            all_moves.emplace_front(ToString(x, y, i, j));
        else {
            if(IsValid(board[j][i]))
                all_moves.emplace_front(ToString(x, y, i, j));
            break;
        }
    for(short i=x+1, j=y+1; i<BOARD_SIZE && j<BOARD_SIZE; ++i, ++j)
        if(board[j][i] == EMPTY)
            all_moves.emplace_front(ToString(x, y, i, j));
        else {
            if(IsValid(board[j][i]))
                all_moves.emplace_front(ToString(x, y, i, j));
            break;
        }
    return all_moves;
}

/**
 * @brief Generates all pseudo-legal queen moves from square (x, y).
 *
 * The queen combines the movement of a rook and a bishop, so this function
 * simply merges the results of both helpers.
 *
 * @param x  File of the queen (0-7).
 * @param y  Rank of the queen (0-7).
 * @return   Merged list of rook + bishop candidate moves.
 */
std::forward_list<std::string> Chess::QueenMoves(const short &x, const short &y) const noexcept {
    auto all_moves = RookMoves(x, y);
    all_moves.merge(BishopMoves(x, y));
    return all_moves;
}

/**
 * @brief Generates all pseudo-legal king moves from square (x, y).
 *
 * Checks the 8 adjacent squares and adds any that are empty or enemy-occupied
 * and within board bounds.
 *
 * Additionally, if castling is still permitted for the current player and the
 * king is not currently in check, offers:
 *   - Queen-side castling (king moves to c-file, x=2): requires a-rook present
 *     and squares b,c,d empty.
 *   - King-side  castling (king moves to g-file, x=6): requires h-rook present
 *     and squares f,g empty.
 *
 * Legality (not moving through check) is enforced later by the `IsCheck` filter
 * in `AllMoves()`.
 *
 * @param x  File of the king (0-7).
 * @param y  Rank of the king (0-7).
 * @return   List of legal-candidate king moves (including castling if available).
 */
std::forward_list<std::string> Chess::KingMoves(const short &x, const short &y) const noexcept {
    const auto &IsValid = whites_turn ? [](const char &ch){ return ch <= 0; } : [](const char &ch){ return ch >= 0; };
    std::forward_list<std::string> all_moves;
    for(short i=x-1;i<x+2;++i)
        for(short j=y-1;j<y+2;++j)
            if(IsValid(board[j][i]) && WithinBounds(i) && WithinBounds(j))
                all_moves.emplace_front(ToString(x, y, i, j));
    if(GetCurrentPlayerConst().GetCastling())
        if(!IsCheck(whites_turn)) {
            const short &line = (BOARD_SIZE-1)*whites_turn;
            if((board[line][0] == B_ROOK + 7*whites_turn) && board[line][1] == EMPTY && board[line][2] == EMPTY && board[line][3] == EMPTY)
                all_moves.emplace_front(ToString(4, line, 2, line));
            else if((board[line][7] == B_ROOK + 7*whites_turn) && board[line][5] == EMPTY && board[line][6] == EMPTY)
                all_moves.emplace_front(ToString(4, line, 6, line));
        }
    return all_moves;
}

/**
 * @brief Returns the full set of strictly legal moves for the current player.
 *
 * Steps:
 *   1. Iterates over all 64 squares; skips squares belonging to the opponent.
 *   2. Dispatches to the appropriate piece-specific move generator.
 *   3. Merges all candidate moves into a single list.
 *   4. Filters out any move that would leave the player's OWN king in check
 *      (using the `IsCheck(std::string&)` overload which makes/undoes the move).
 *
 * The filter in step 4 ensures that no illegal moves are ever returned to the
 * user or to the bot's tree search — it is the authoritative legality checker.
 *
 * @return  A `forward_list` of 4-char move strings representing all legal moves.
 */
std::forward_list<std::string> Chess::AllMoves() noexcept {
    std::forward_list<std::string> all_moves;
    for(short y=0;y<BOARD_SIZE;++y)
        for(short x=0;x<BOARD_SIZE;++x) {
            if((board[y][x] < 0) == whites_turn)
                continue;
            switch(board[y][x]) {
                case W_PAWN:
                case B_PAWN:
                    all_moves.merge(PawnMoves(x, y));
                    break;
                case W_ROOK:
                case B_ROOK:
                    all_moves.merge(RookMoves(x, y));
                    break;
                case W_KNIGHT:
                case B_KNIGHT:
                    all_moves.merge(KnightMoves(x, y));
                    break;
                case W_BISHOP:
                case B_BISHOP:
                    all_moves.merge(BishopMoves(x, y));
                    break;
                case W_QUEEN:
                case B_QUEEN:
                    all_moves.merge(QueenMoves(x, y));
                    break;
                case W_KING:
                case B_KING:
                    all_moves.merge(KingMoves(x, y));
            }
        }
    for(auto it = all_moves.begin(), prev = all_moves.before_begin(); it != all_moves.cend();)        // if the possible move makes me checkmate after the opponent's turn, remove it from the list
        if(IsCheck(*it))
            it = all_moves.erase_after(prev);
        else
            ++it, ++prev;
    return all_moves;
}

/**
 * @brief Returns a random legal move for the current player in integer coord format.
 *
 * Used when a `Bot` is in random mode (the simplest "Easy" AI fallback). Picks
 * a uniformly random move from `AllMoves()` and converts it to real coordinates.
 *
 * @return  4-byte move string with integer board coordinates (not ASCII).
 */
std::string Chess::GetRandomMove() noexcept {
    auto all_moves = AllMoves();
    auto move = all_moves.begin();
    advance(move, GetRandomNumber<unsigned short>(0, distance(all_moves.cbegin(), all_moves.cend()) - 1));
    ChangeToRealCoordinates((*move)[0], (*move)[1], (*move)[2], (*move)[3]);
    return *move;
}

/**
 * @brief Prompts the human player to choose a promotion piece via a keypress.
 *
 * Called during `MovePiece()` when a human player's pawn reaches the back rank
 * and `manual_promotion` is true. Reads a single character ('r', 'k', 'b', 'q')
 * via `getch()` (no Enter key needed) and immediately sets the pawn's square to
 * the chosen piece type.
 *
 * @param x  File of the pawn (pre-move source, used to update `board`).
 * @param y  Rank of the pawn (source rank, not yet moved).
 */
void Chess::ManuallyPromotePawn(const short &x, const short &y) noexcept {
    MoveCursorToXY(RIGHT, DOWN + 3*BOARD_SIZE + 7);
    std::cout << "Enter your choice of promotion [(r)ook, (k)night, (b)ishop, (q)ueen]";
    char key = getch();
    while(true)
        switch(key = tolower(key)) {
            case 'r':    board[y][x] = whites_turn ? W_ROOK : B_ROOK;        return;
            case 'k':    board[y][x] = whites_turn ? W_KNIGHT : B_KNIGHT;    return;
            case 'b':    board[y][x] = whites_turn ? W_BISHOP : B_BISHOP;    return;
            case 'q':    board[y][x] = whites_turn ? W_QUEEN : B_QUEEN;        return;
            default:    key = getch();
        }
}

/**
 * @brief Executes a single half-move on the board, handling all special rules.
 *
 * This is the central mutation function. After recording the move in
 * `all_game_moves`, it applies special-case logic before performing the
 * standard square swap (`board[y2][x2] = board[y1][x1]; board[y1][x1] = EMPTY`):
 *
 *   ### Pawn Promotion
 *   If the pawn reaches the opponent's back rank (row 0 for white, row 7 for
 *   black), the pawn is replaced by a queen by default, or a random piece in
 *   random-bot mode, or the human's choice if `manual_promotion` is true.
 *
 *   ### En Passant
 *   If a pawn moves diagonally to an empty square, the captured pawn (which
 *   sits on the same rank as the attacker, not the target square) is removed
 *   at `board[y1][x2]`.
 *
 *   ### Castling
 *   When the king moves two squares sideways, the corresponding rook is
 *   teleported to the square the king passed through. Castling rights are
 *   revoked for both king and rook moves.
 *
 * @param x1,y1          Source square (integer coords).
 * @param x2,y2          Destination square (integer coords).
 * @param manual_promotion  If true, prompt the human player for promotion choice.
 * @param update_board   If true, update the terminal display and player scores.
 */
void Chess::MovePiece(const short &x1, const short &y1, const short &x2, const short &y2, const bool &manual_promotion, const bool &update_board) noexcept {
    AppendToAllGameMoves(x1, y1, x2, y2);
    switch(board[y1][x1]) {
        case W_PAWN:
        case B_PAWN:
            if(y2 == ((BOARD_SIZE-1) * !whites_turn)) {
                if(manual_promotion) {
                    ManuallyPromotePawn(x1, y1);
                    MoveCursorToXY(RIGHT, DOWN + 3*BOARD_SIZE + 7);
                    std::cout << "All possible moves:" << CLEAR_LINE;
                }
                else if(whites_turn ? white_bot_random : black_bot_random)
                    board[y1][x1] = (whites_turn ? 1 : -1) * GetRandomNumber(2, 5);
                else
                    board[y1][x1] = whites_turn ? W_QUEEN : B_QUEEN;
                all_game_moves.back().first = PROMOTION;
                all_game_moves.back().second.push_back(board[y1][x1]);
            }
            else if(x1 != x2 && board[y2][x2] == EMPTY) {
                board[y1][x2] = EMPTY;
                if(update_board) {
                    GetCurrentPlayer().IncreaseScore(EvaluatePiece(W_PAWN));
                    UpdateScore(GetCurrentPlayerConst());
                    UpdateBoard(x2, y1);
                }
                all_game_moves.back().first = EN_PASSANT;
            }
            break;
        case W_KING:
        case B_KING:
            if(GetCurrentPlayerConst().GetCastling()) {
                const short &line = (BOARD_SIZE-1) * whites_turn;
                switch(x2) {
                    case 2:
                        board[line][3] = board[line][0], board[line][0] = EMPTY;
                        if(update_board) {
                            UpdateBoard(0, line);
                            UpdateBoard(3, line);
                        }
                        break;
                    case 6:
                        board[line][5] = board[line][7], board[line][7] = EMPTY;
                        if(update_board) {
                            UpdateBoard(7, line);
                            UpdateBoard(5, line);
                        }
                }
            }
        case W_ROOK:
        case B_ROOK:
            GetCurrentPlayer().SetCastling(false);
    }
    if(all_game_moves.back().first != CASTLING)                all_game_moves.back().second.push_back(GetCurrentPlayerConst().GetCastling());
    board[y2][x2] = board[y1][x1], board[y1][x1] = EMPTY;
    if(update_board) {
        if(all_game_moves.back().first != CASTLING)
            if(all_game_moves.back().second[5] != EMPTY) {
                GetCurrentPlayer().IncreaseScore(EvaluatePiece(all_game_moves.back().second[5]));
                UpdateScore(GetCurrentPlayerConst());
                moves_after_last_pawn_move_or_capture = 0;
            }
        UpdateBoard(x1, y1);
        UpdateBoard(x2, y2);
    }
    ChangeTurn();
}

/**
 * @brief Undoes the most recent half-move, restoring the board to its prior state.
 *
 * The inverse of `MovePiece()`. Uses the last entry of `all_game_moves` to:
 *   - Move the piece back from (x2,y2) to (x1,y1).
 *   - Restore any captured piece at (x2,y2).
 *   - Reverse en passant (restore the captured pawn).
 *   - Reverse promotions (reset a queen back to a pawn).
 *   - Reverse castling (move the rook back, re-enable castling rights).
 *   - Restore the previous player's castling eligibility.
 *
 * This function is exclusively called by the AI search (`AlphaBeta`) after
 * evaluating a branch, so it deliberately never writes to the terminal display.
 *
 * @param x1,y1  Source square of the original move (piece returns here).
 * @param x2,y2  Destination square of the original move.
 */
void Chess::MovePieceBack(const short &x1, const short &y1, const short &x2, const short &y2) noexcept {
    ChangeTurn();
    board[y1][x1] = board[y2][x2], board[y2][x2] = all_game_moves.back().first == CASTLING ? static_cast<char>(EMPTY) : all_game_moves.back().second[5];
    switch(board[y1][x1]) {
        case W_PAWN:
        case B_PAWN:
            if(x1 != x2 && board[y2][x2] == EMPTY)
                board[y1][x2] = whites_turn ? B_PAWN : W_PAWN;
            break;
        case W_ROOK:
        case B_ROOK:
            if(prev(all_game_moves.cend(), 3)->first != CASTLING)
                if(prev(all_game_moves.cend(), 3)->second[6 + (prev(all_game_moves.cend(), 3)->first == PROMOTION)])
                    GetCurrentPlayer().SetCastling(true);
            break;
        case W_QUEEN:
        case B_QUEEN:
            if(all_game_moves.back().first == PROMOTION)
                board[y1][x1] = whites_turn ? W_PAWN : B_PAWN;
            break;
        case W_KING:
        case B_KING:
            if(all_game_moves.back().first == CASTLING) {
                GetCurrentPlayer().SetCastling(true);
                const short line = (BOARD_SIZE-1) * whites_turn;
                switch(x2) {
                    case 2:
                        board[line][0] = board[line][3], board[line][3] = EMPTY;
                        break;
                    case 6:
                        board[line][5] = board[line][7], board[line][7] = EMPTY;
                }
            }
            else if(prev(all_game_moves.cend(), 3)->first != CASTLING)
                if(prev(all_game_moves.cend(), 3)->second[6 + (prev(all_game_moves.cend(), 3)->first == PROMOTION)])
                    GetCurrentPlayer().SetCastling(true);
    }
    all_game_moves.pop_back();
}

/**
 * @brief Redraws a single board cell in the terminal at its correct cursor position.
 *
 * Calculates the centred padding for the piece name string (e.g., "W_QUEEN"
 * has 7 chars; the cell is BOX_WIDTH=10 chars wide, so it gets 1.5 chars of
 * padding on each side rounded appropriately). Positions the cursor using
 * `MoveCursorToXY` and overwrites the old content in place, avoiding a full
 * screen repaint for every piece movement.
 *
 * @param x  File (column) index of the square to redraw (0-7).
 * @param y  Rank (row) index of the square to redraw (0-7).
 */
void Chess::UpdateBoard(const short &x, const short &y) const noexcept {
    const unsigned short &diff = BOX_WIDTH - PieceNameToString(board[y][x]).length();
    MoveCursorToXY(RIGHT + (BOX_WIDTH+1)*x, DOWN + 3*y + 1);
    std::cout << std::string(diff/2, ' ') << PieceNameToString(board[y][x]) << std::string(diff/2, ' ');
    if(diff%2)    std::cout << " ";
}

/**
 * @brief Updates the displayed score for player `p` in the terminal UI.
 *
 * Positions the cursor over the existing score number, blanks it with spaces
 * (to handle digit-count changes, e.g., "9" -> "10"), then reprints. White's
 * score is displayed on the left, black's on the right of the score line.
 *
 * @param p  The player whose score line should be refreshed.
 */
void Chess::UpdateScore(const Bot &p) const noexcept {
    const unsigned short &dx = p==white ? white.GetName().length() + 2 : (BOX_WIDTH+1)*BOARD_SIZE - 5;
    MoveCursorToXY(RIGHT+dx, DOWN + 3*BOARD_SIZE + 2);
    std::cout << std::string(std::to_string(p.GetScore()).length(), ' ');
    MoveCursorToXY(RIGHT+dx, DOWN + 3*BOARD_SIZE + 2);
    std::cout << p.GetScore();
}

/**
 * @brief Returns the combined material + positional evaluation for the piece at (x,y).
 *
 * The evaluation is the sum of two components:
 *
 *   1. **Material value** (`EvaluatePiece`): Pawn=10, Rook=50, Bishop/Knight=30,
 *      Queen=90, King=900.
 *
 *   2. **Positional bonus** (`PIECE_POS_POINTS`): A 6×8×8 lookup table of
 *      floating-point bonuses/penalties indexed by [piece_type][row][col].
 *      This is a classic implementation of **Piece-Square Tables (PST)**, which
 *      encode domain knowledge about good and bad squares for each piece type:
 *
 *      - **King (index 0):** Heavy penalties in the centre (unsafe) and bonuses
 *        at the back row edges/corners (safe from early attacks). Values are
 *        stored from white's perspective (row 0 = rank 8 = black's back rank).
 *
 *      - **Queen (index 1):** Slight penalties for early queen development away
 *        from the back rank; near-zero bonuses for central positions to avoid
 *        exposing the queen unnecessarily in the opening.
 *
 *      - **Bishop (index 2):** Bonuses for long diagonals and central squares,
 *        penalties at edges (reduced mobility on the rim).
 *
 *      - **Knight (index 3):** Large penalties at corners/edges (-5 at a1/h1
 *        type squares), increasing bonuses toward the centre (max 2.0 at d4/e4).
 *        This is the most pronounced PST — "a knight on the rim is dim".
 *
 *      - **Rook (index 4):** Bonus for the 7th-rank (penultimate rank offset
 *        BOARD_SIZE-2) and central files; slight penalties on the back rank
 *        (a passive rook). Minor differences overall since rooks dominate by
 *        activity rather than position alone.
 *
 *      - **Pawn (index 5):** Bonus for central pawns (d4/e4 = +2.0), penalties
 *        for doubled/blocked central pawns (-2.0 at d2/e2). The 7th rank gets a
 *        large bonus (+5.0) reflecting the promotion threat.
 *
 * The sign convention: black pieces have a negative `ChessPieces` value, so\n * the expression `(board[y][x] < 0 ? -1 : 1)` maps the table result to
 * negative scores for black and positive for white, appropriate for a
 * zero-sum evaluation from white's perspective aligned with `EvaluateBoard`.
 *
 * The PST is stored `static` to avoid repeated stack allocation (6×8×8 floats).
 *
 * @param x  File of the square to evaluate (0-7).
 * @param y  Rank of the square to evaluate (0-7; row 0 = rank 8).
 * @return   Signed evaluation score: positive = good for white, negative = good for black.
 */
float Chess::EvaluatePosition(const short &x, const short &y) const noexcept {
    if(board[y][x] == EMPTY)
        return 0;
    static float PIECE_POS_POINTS[6][BOARD_SIZE][BOARD_SIZE] =
    {{{-3.0, -4.0, -4.0, -5.0, -5.0, -4.0, -4.0, -3.0},
    {-3.0, -4.0, -4.0, -5.0, -5.0, -4.0, -4.0, -3.0},
    {-3.0, -4.0, -4.0, -5.0, -5.0, -4.0, -4.0, -3.0},
    {-3.0, -4.0, -4.0, -5.0, -5.0, -4.0, -4.0, -3.0},
    {-2.0, -3.0, -3.0, -4.0, -4.0, -3.0, -3.0, -2.0},
    {-1.0, -2.0, -2.0, -2.0, -2.0, -2.0, -2.0, -1.0},
    {2.0, 2.0, 0.0, 0.0, 0.0, 0.0, 2.0, 2.0},
    {2.0, 3.0, 1.0, 0.0, 0.0, 1.0, 3.0, 2.0}}
    ,
    {{-2.0, -1.0, -1.0, -0.5, -0.5, -1.0, -1.0, -2.0},
    {-1.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, -1.0},
    {-1.0, 0.0, 0.5, 0.5, 0.5, 0.5, 0.0, -1.0},
    {-0.5, 0.0, 0.5, 0.5, 0.5, 0.5, 0.0, -0.5},
    {0.0, 0.0, 0.5, 0.5, 0.5, 0.5, 0.0, -0.5},
    {-1.0, 0.5, 0.5, 0.5, 0.5, 0.5, 0.0, -1.0},
    {-1.0, 0.0, 0.5, 0.0, 0.0, 0.0, 0.0, -1.0},
    {-2.0, -1.0, -1.0, -0.5, -0.5, -1.0, -1.0, -2.0}}
    ,
    {{-2.0, -1.0, -1.0, -1.0, -1.0, -1.0, -1.0, -2.0},
    {-1.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, -1.0},
    {-1.0, 0.0, 0.5, 1.0, 1.0, 0.5, 0.0, -1.0},
    {-1.0, 0.5, 0.5, 1.0, 1.0, 0.5, 0.5, -1.0},
    {-1.0, 0.0, 1.0, 1.0, 1.0, 1.0, 0.0, -1.0},
    {-1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0, -1.0},
    {-1.0, 0.5, 0.0, 0.0, 0.0, 0.0, 0.5, -1.0},
    {-2.0, -1.0, -1.0, -1.0, -1.0, -1.0, -1.0, -2.0}}
    ,
    {{-5.0, -4.0, -3.0, -3.0, -3.0, -3.0, -4.0, -5.0},
    {-4.0, -2.0, 0.0, 0.0, 0.0, 0.0, -2.0, -4.0},
    {-3.0, 0.0, 1.0, 1.5, 1.5, 1.0, 0.0, -3.0},
    {-3.0, 0.5, 1.5, 2.0, 2.0, 1.5, 0.5, -3.0},
    {-3.0, 0.0, 1.5, 2.0, 2.0, 1.5, 0.0, -3.0},
    {-3.0, 0.5, 1.0, 1.5, 1.5, 1.0, 0.5, -3.0},
    {-4.0, -2.0, 0.0, 0.5, 0.5, 0.0, -2.0, -4.0},
    {-5.0, -4.0, -3.0, -3.0, -3.0, -3.0, -4.0, -5.0}}
    ,
    {{0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0},
    {0.5, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 0.5},
    {-0.5, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, -0.5},
    {-0.5, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, -0.5},
    {-0.5, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, -0.5},
    {-0.5, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, -0.5},
    {-0.5, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, -0.5},
    {0.0, 0.0, 0.0, 0.5, 0.5, 0.0, 0.0, 0.0}}
    ,
    {{0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0},
    {5.0, 5.0, 5.0, 5.0, 5.0, 5.0, 5.0, 5.0},
    {1.0, 1.0, 2.0, 3.0, 3.0, 2.0, 1.0, 1.0},
    {0.5, 0.5, 1.0, 2.5, 2.5, 1.0, 0.5, 0.5},
    {0.0, 0.0, 0.0, 2.0, 2.0, 0.0, 0.0, 0.0},
    {0.5, -0.5, -1.0, 0.0, 0.0, -1.0, -0.5, 0.5},
    {0.5, 1.0, 1.0, -2.0, -2.0, 1.0, 1.0, 0.5},
    {0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0}}};
    return (board[y][x]<0 ? -1 : 1) * (EvaluatePiece(board[y][x]) + PIECE_POS_POINTS[board[y][x] + 7*(board[y][x]<0) - 1][board[y][x]<0 ? BOARD_SIZE-y-1 : y][x]);
}

/**
 * @brief Sums EvaluatePosition() over all 64 squares and returns a signed total.
 *
 * Returns a positive value when the `turn` side has a material/positional
 * advantage, negative when the opponent is ahead. This is the terminal leaf
 * value returned by `AlphaBeta()` when search depth reaches zero.
 *
 * The flip via `(turn ? 1 : -1)` ensures the score is always relative to the
 * bot that initiated the search (stored as `initial_turn` in `AlphaBeta`):
 *   - If it's white's search and white is ahead, the raw total is positive
 *     and the flip keeps it positive.
 *   - If it's black's search and black is ahead, the raw total is negative
 *     (black pieces contribute negatively) but the flip negates it to positive.
 *
 * @param turn  The side whose perspective the evaluation is from (true = white).
 * @return      Heuristic board score from `turn`'s perspective.
 */
float Chess::EvaluateBoard(const bool &turn) const noexcept {
    float total_evaluation = 0.0;
    for(short y=0;y<BOARD_SIZE;++y)
        for(short x=0;x<BOARD_SIZE;++x)
            total_evaluation += EvaluatePosition(x, y);
    return (turn ? 1 : -1) * total_evaluation;
}

/**
 * @brief Performs a full terminal screen redraw of the board and UI chrome.
 *
 * Clears the terminal (`cls`/`clear`), then prints the board from top
 * (rank 8) to bottom (rank 1) using box-drawing characters and `PrintSeparator`.
 * After the board, prints rank numbers (8..1) on the left and file letters (a..h)
 * below, followed by both players' score lines, and the move-input prompt.
 *
 * Called once at game start and after every bot move (bot moves replace a single
 * cell update with a full redraw to ensure consistency after every search).
 * Human moves update only the changed cells via `UpdateBoard` for performance.
 */
void Chess::PrintBoard() const noexcept {
#ifdef _WIN32
    system("cls");
#else
    system("clear");
#endif
    std::cout << TO_DOWN << TO_RIGHT;
    for(short y=0;y<BOARD_SIZE;++y) {
        PrintSeparator(' ');
        std::cout << "\b\b\b" << BOARD_SIZE-y << "  ";
        for(short x=0;x<BOARD_SIZE;++x) {
            const unsigned short &diff = BOX_WIDTH - PieceNameToString(board[y][x]).length();
            std::cout << std::string(diff/2, ' ') << PieceNameToString(board[y][x]) << std::string(diff/2, ' ');
            if(diff%2)                std::cout << " ";
            if(x < BOARD_SIZE-1)    std::cout << "|";
        }
        if(y < BOARD_SIZE-1) {
            std::cout << std::endl << TO_RIGHT;
            PrintSeparator('_');
        }
    }
    std::cout << std::endl << TO_RIGHT;
    PrintSeparator(' ');
    for(char ch='a';ch<'a'+BOARD_SIZE;++ch)
        std::cout << std::string(BOX_WIDTH/2, ' ') << ch << std::string(BOX_WIDTH/2, ' ');
    std::cout << std::endl << std::endl << TO_RIGHT << white.GetName() << ": 0";
    std::cout << std::string((BOX_WIDTH+1)*BOARD_SIZE - white.GetName().length() - black.GetName().length() - 10, ' ') << black.GetName() << ": 0";
    std::cout << std::endl << std::endl << TO_RIGHT << white.GetName() << "'s turn...";
    std::cout << std::endl << TO_RIGHT << "Enter move coordinates (x1,y1)->(x2,y2):";
    std::cout << std::endl << std::endl << TO_RIGHT << "All possible moves:" << std::endl;
}

void Chess::PrintAllMovesMadeInOrder() const noexcept {
    std::cout << std::endl << std::endl << TO_RIGHT << "All moves made in order:" << std::endl;
    bool turn = true;
    for(const auto &game_move : all_game_moves) {
        std::cout << std::endl << TO_RIGHT << (turn ? white : black).GetName() << ": ";
        switch(game_move.first) {
            case CASTLING:
                std::cout << "castling " << (game_move.second[0] == 2 ? "long" : "short");    break;
            default:
                std::cout << ToLowerString(PieceNameToString(game_move.second[4])).substr(2) << " '" << game_move.second.substr(0, 2) << "' to ";
                if(game_move.second[5] != EMPTY)
                    std::cout << ToLowerString(PieceNameToString(game_move.second[5])).substr(2) + " ";
                std::cout << "'" << game_move.second.substr(2, 2) << "'";
                switch(game_move.first) {
                    case PROMOTION:
                        std::cout << " promoted to " << ToLowerString(PieceNameToString(game_move.second[6])).substr(2);
                        break;
                    case EN_PASSANT:
                        std::cout << " (en passant)";
                    default:
                        break;
                }
        }
        turn = !turn;
    }
}

bool Chess::CheckEndgame(const unsigned short &n) noexcept {
    if(AllMoves().empty()) {
        GetOtherPlayer().IncreaseScore(EvaluatePiece(W_KING));
        UpdateScore(GetOtherPlayerConst());
        return EndGameText(n, CHECKMATE);
    }
    else if(all_game_moves.back().first != CASTLING) {
        if(all_game_moves.back().second[4] == W_PAWN - 7*whites_turn)
            moves_after_last_pawn_move_or_capture = 0;
        else if(all_game_moves.back().second[5] != EMPTY)
            moves_after_last_pawn_move_or_capture = 0;
        else if((++moves_after_last_pawn_move_or_capture) == 50)
            return EndGameText(n, FIFTY_MOVES);
    }
    else if((++moves_after_last_pawn_move_or_capture) == 50)
        return EndGameText(n, FIFTY_MOVES);
    if(ThreefoldRepetition())
        return EndGameText(n, THREEFOLD_REP);
    return false;
}

bool Chess::PlayersTurn() noexcept {
    auto all_moves = AllMoves();
    all_moves.sort();
    unsigned short i=0;
    for(const auto &move : all_moves) {
        if(!((i++)%MOVES_PER_LINE))    std::cout << std::endl;
        std::cout << TO_RIGHT << move.substr(0, 2) << " " << move.substr(2);
    }
    if(IsCheck(whites_turn)) {
        std::cout << std::endl << std::endl << TO_RIGHT << "Check!";
        i += 2*MOVES_PER_LINE;
    }
    MoveCursorToXY(RIGHT+41, DOWN + 3*BOARD_SIZE + 5);
    while(true) {
        std::string from, to;
        std::cin >> from;
        if(!ToLowerString(from).compare("quit"))
            return EndGameText(i/MOVES_PER_LINE + 1, QUIT);
        if(!ToLowerString(from).compare("exit"))
            return EndGameText(i/MOVES_PER_LINE + 1, QUIT);
        std::cin >> to;
        from.resize(2);
        to.resize(2);
        from.shrink_to_fit();
        to.shrink_to_fit();
        from[0] = tolower(from[0]), to[0] = tolower(to[0]);
        ChangeToRealCoordinates(from[0], from[1], to[0], to[1]);
        if((from[0]!=to[0] || from[1]!=to[1]) && WithinBounds(from[0]) && WithinBounds(from[1]) && WithinBounds(to[0]) && WithinBounds(to[1]))
            if(CanMovePiece(from[0], from[1], to[0], to[1], all_moves)) {
                MovePiece(from[0], from[1], to[0], to[1], true, true);
                if(CheckEndgame(i/MOVES_PER_LINE + 1))
                    return false;
                break;
            }
        MoveCursorToXY(RIGHT+41, DOWN + 3*BOARD_SIZE + 5);
        std::cout << CLEAR_LINE << std::endl << CLEAR_LINE;
        MoveCursorToXY(RIGHT+41, DOWN + 3*BOARD_SIZE + 5);
    }
    MoveCursorToXY(RIGHT, DOWN + 3*BOARD_SIZE + 4);
    std::cout << GetCurrentPlayerConst().GetName() << "'s turn..." << CLEAR_LINE;
    MoveCursorToXY(RIGHT+41, DOWN + 3*BOARD_SIZE + 5);
    std::cout << CLEAR_LINE << std::endl << CLEAR_LINE;
    ClearAllMoves(i/MOVES_PER_LINE + 1);
    MoveCursorToXY(0, DOWN + 3*BOARD_SIZE + 8);
    return true;
}

bool Chess::BotsTurn() noexcept {
    const auto &move = (whites_turn ? white_bot_random : black_bot_random) ? GetRandomMove() : GetCurrentPlayer().GetIdealMove(*this);
    std::cout << "Bot moves: " << move.substr(0,2) << " to " << move.substr(2,2) << std::endl;
    MovePiece(move[0], move[1], move[2], move[3], false, true);
    PrintBoard();
    if(CheckEndgame())
        return false;
    MoveCursorToXY(RIGHT, DOWN + 3*BOARD_SIZE + 4);
    std::cout << GetCurrentPlayerConst().GetName() << "'s turn..." << CLEAR_LINE;
    return true;
}

bool Chess::GameOver() noexcept {
    std::cout << std::endl << std::endl << std::endl << TO_RIGHT << "Press R to play again.";
    std::cout << std::endl << TO_RIGHT << "Press any other key to quit.";
    PrintAllMovesMadeInOrder();
    char key = getch();
    switch(key = tolower(key)) {
        case 'r':
            Reset();
            return true;
        default:
            return false;
    }
}

// --- main() ---
int main() {
    std::cout << "Welcome to ChessBot!" << std::endl;
    srand((unsigned int)time(NULL));

    int game_mode = 0;
    while (true) {
        std::cout << "\nChoose game mode:" << std::endl;
        std::cout << "1. Play against Bot" << std::endl;
        std::cout << "2. Play against another Person" << std::endl;
        std::cout << "3. Bot vs Bot" << std::endl;
        std::cout << "Enter 1, 2, or 3: ";
        std::cin >> game_mode;
        if (game_mode >= 1 && game_mode <= 3) break;
        std::cout << "Invalid input. Please try again." << std::endl;
    }

    bool against_bot = (game_mode == 1);
    bool two_bots = (game_mode == 3);
    bool bot_is_white = false;
    int bot_difficulty = 1;
    std::string player1 = "Player1", player2 = "Player2";
    bool white_bot_random = false, black_bot_random = false;
    int white_bot_difficulty = 1, black_bot_difficulty = 1;

    if (against_bot) {
        std::string color_choice;
        while (true) {
            std::cout << "\nDo you want to play as white or black? (w/b): ";
            std::cin >> color_choice;
            if (color_choice == "w" || color_choice == "W") {
                bot_is_white = false;
                player1 = "You";
                player2 = "Bot";
                break;
            } else if (color_choice == "b" || color_choice == "B") {
                bot_is_white = true;
                player1 = "Bot";
                player2 = "You";
                break;
            } else {
                std::cout << "Invalid input. Please enter 'w' or 'b'." << std::endl;
            }
        }
        while (true) {
            std::cout << "\nChoose bot difficulty:" << std::endl;
            std::cout << "1. Easy" << std::endl;
            std::cout << "2. Medium" << std::endl;
            std::cout << "3. Hard" << std::endl;
            std::cout << "Enter 1, 2, or 3: ";
            std::cin >> bot_difficulty;
            if (bot_difficulty >= 1 && bot_difficulty <= 3) break;
            std::cout << "Invalid input. Please try again." << std::endl;
        }
        if (bot_is_white) {
            white_bot_random = false;
            black_bot_random = false;
            white_bot_difficulty = bot_difficulty;
            black_bot_difficulty = 1;
        } else {
            white_bot_random = false;
            black_bot_random = false;
            white_bot_difficulty = 1;
            black_bot_difficulty = bot_difficulty;
        }
    } else if (two_bots) {
        player1 = "Bot1";
        player2 = "Bot2";
        white_bot_random = false;
        black_bot_random = false;
        while (true) {
            std::cout << "\nChoose white bot difficulty (1=Easy, 2=Medium, 3=Hard): ";
            std::cin >> white_bot_difficulty;
            if (white_bot_difficulty >= 1 && white_bot_difficulty <= 3) break;
            std::cout << "Invalid input. Please try again." << std::endl;
        }
        while (true) {
            std::cout << "\nChoose black bot difficulty (1=Easy, 2=Medium, 3=Hard): ";
            std::cin >> black_bot_difficulty;
            if (black_bot_difficulty >= 1 && black_bot_difficulty <= 3) break;
            std::cout << "Invalid input. Please try again." << std::endl;
        }
    } else {
        player1 = "Player1";
        player2 = "Player2";
    }

    Chess c(player1, white_bot_difficulty, player2, black_bot_difficulty, white_bot_random, black_bot_random);

    if (against_bot) {
        do {
            c.PrintBoard();
            if (bot_is_white) {
                while (true) {
                    MoveCursorToXY(RIGHT, DOWN + 3*BOARD_SIZE + 5);
                    std::cout << CLEAR_LINE << std::endl << std::endl << CLEAR_LINE;
                    if (!c.BotsTurn())
                        break;
                    std::cout << std::endl << TO_RIGHT << "Enter move coordinates (x1,y1)->(x2,y2):";
                    std::cout << std::endl << std::endl << TO_RIGHT << "All possible moves:" << std::endl;
                    if (!c.PlayersTurn())
                        break;
                }
            } else {
                while (true) {
                    if (!c.PlayersTurn())
                        break;
                    MoveCursorToXY(RIGHT, DOWN + 3*BOARD_SIZE + 5);
                    std::cout << CLEAR_LINE << std::endl << std::endl << CLEAR_LINE;
                    if (!c.BotsTurn())
                        break;
                    std::cout << std::endl << TO_RIGHT << "Enter move coordinates (x1,y1)->(x2,y2):";
                    std::cout << std::endl << std::endl << TO_RIGHT << "All possible moves:" << std::endl;
                }
            }
        } while (c.GameOver());
        exit(0);
    } else if (two_bots) {
        do {
            c.PrintBoard();
            MoveCursorToXY(RIGHT, DOWN + 3*BOARD_SIZE + 5);
            std::cout << CLEAR_LINE << std::endl << std::endl << CLEAR_LINE;
            while (c.BotsTurn());
        } while (c.GameOver());
        exit(0);
    } else {
        do {
            c.PrintBoard();
            while (c.PlayersTurn());
        } while (c.GameOver());
    }
}