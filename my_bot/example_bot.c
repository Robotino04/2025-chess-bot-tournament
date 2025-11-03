#include "chessapi.h"
#include "stdlib.h"
#include "math.h"
#include "setjmp.h"

#undef INFINITY
#define INFINITY 9999999

#ifndef MINIMIZE
    #define STATS
    #include "stdio.h"

    #define STATIC_ASSERTS
#endif


// TODO: write as single number
#define TRANSPOSITION_SIZE (1ul << 25)

// define it here so minimize.py removes it before applying macros
#undef stdc_count_ones_ul
#define stdc_count_ones_ul(x) __builtin_popcountl(x)

#ifdef STATIC_ASSERTS
static_assert((TRANSPOSITION_SIZE & (TRANSPOSITION_SIZE - 1)) == 0, "TRANSPOSITION_SIZE isn't a power of two");
#endif

#define TYPE_UNUSED 0
#define TYPE_EXACT 1
#define TYPE_UPPER_BOUND 2
#define TYPE_LOWER_BOUND 3


Board* board;
uint64_t time_left;
jmp_buf timeout_jmp;
GameState state;

struct {
#ifdef STATS
    uint64_t num_nodes;
#endif
    uint64_t hash;
    int eval, type, depth;
    Move bestMove;
} transposition_table[TRANSPOSITION_SIZE];

#ifdef STATS
uint64_t hashes_used;

uint64_t searched_nodes;
uint64_t transposition_hits;
uint64_t cached_nodes;
uint64_t transposition_overwrites;
uint64_t new_hashes;
uint64_t researches;
uint64_t first_move_cuts;
uint64_t first_move_non_cuts;
uint64_t negascout_hits;
uint64_t negascout_misses;
#endif

#define MAX_MOVES 256
#define FETCH_MOVES        \
    Move moves[MAX_MOVES]; \
    int len_moves = chess_get_legal_moves_inplace(board, moves, MAX_MOVES);


// TODO
// - [ ] macro away the board parameter: #define chess_get_legal_moves(...) chess_get_legal_moves(board, __VA_ARGS__)
// - [ ] remove braces of single-line if-statements
// - [ ] make common parameters globals
// - [ ] test without custom libchess build

/*
int countBit1Fast(unsigned long n) {
    int c = 0;
    for (; n; ++c)
        n &= n - 1;
    return c;
}
*/


// notshit fen: r3k2r/p1p2ppp/2pp4/4p3/P7/2P1PbP1/RP5P/2Q2K1R w kq - 0 20
// r1bqk1nr/pppp2pp/2n2p2/6B1/1b5Q/P7/1PP1PPPP/RN2KBNR w KQkq - 3 6
// rn2k1nr/ppp2ppp/4p3/2bp1b2/7q/P7/RPPPPPPP/1NBQKBNR w Kkq - 6 7
//
// endgame: 8/8/4k3/8/8/4K3/4R3/4R3 w - - 0 1
// endgame nodraw: k7/8/1R6/1K6/8/8/8/8 w - - 18 10
// 2k5/8/2K5/8/8/8/4R3/1R6 w - - 16 9
// 3k4/8/8/8/8/8/8/3R3K b - - 0 1
// 8/4k3/8/4K3/8/8/8/2R5 w - - 41 22
//
// midgame fail: r5k1/p6p/6p1/2Qb1r2/P6K/8/RP5P/6R1 w - - 0 33
// prevent promotion: 8/3K4/4P3/8/8/8/6k1/7q w - - 0 1

int material_of(PlayerColor color) {
    return +stdc_count_ones_ul(chess_get_bitboard(board, color, PAWN)) * 100
         + stdc_count_ones_ul(chess_get_bitboard(board, color, KNIGHT)) * 300
         + stdc_count_ones_ul(chess_get_bitboard(board, color, BISHOP)) * 320
         + stdc_count_ones_ul(chess_get_bitboard(board, color, ROOK)) * 500
         + stdc_count_ones_ul(chess_get_bitboard(board, color, QUEEN)) * 900;
}
#define GET_ENDGAME_WEIGHT(COLOR)                                                          \
    chess_get_bitboard(board, COLOR, KNIGHT) | chess_get_bitboard(board, COLOR, BISHOP)    \
        | chess_get_bitboard(board, COLOR, ROOK) | chess_get_bitboard(board, COLOR, QUEEN) \
        | chess_get_bitboard(board, COLOR, KING)

int static_eval_me(PlayerColor color) {
#ifdef STATIC_ASSERTS
    static_assert(WHITE == 0, "WHITE isn't 0");
    static_assert(BLACK == 1, "BLACK isn't 1");
    static_assert((WHITE ^ 1) == BLACK, "WHITE isn't inverse of BLACK");
    static_assert((BLACK ^ 1) == WHITE, "BLACK isn't inverse of WHITE");
#endif

    int material = material_of(color);

    float endgame_weight = 0;
    int king = chess_get_index_from_bitboard(chess_get_bitboard(board, color, KING));
    endgame_weight += stdc_count_ones_ul(GET_ENDGAME_WEIGHT(color));

    color ^= 1;

    int king2 = chess_get_index_from_bitboard(chess_get_bitboard(board, color, KING));
    endgame_weight += stdc_count_ones_ul(GET_ENDGAME_WEIGHT(color));

    endgame_weight = 1.0f - endgame_weight / 16.0f;


    // color is inverted already
    if (material > material_of(color) + 200) {
#define king2_file king2 % 8
#define king2_rank king2 / 8

        material += ((7 - fminf(king2_file, 7 - king2_file) - fminf(king2_rank, 7 - king2_rank)) * 5.0f
                     + (14 - abs(king % 8 - king2_file) - abs(king / 8 - king2_rank)))
                  * endgame_weight;
    }

    return material;
}

int static_eval() {
    if (state == GAME_CHECKMATE) {
        return -INFINITY;
    }

    if (state == GAME_STALEMATE) {
        return 0;
    }

    return (chess_is_white_turn(board) ? 1.0f : -1.0f) * (static_eval_me(WHITE) - static_eval_me(BLACK));
}


#define GEN_HASH /* parse fix */              \
    uint64_t hash = chess_zobrist_key(board); \
    auto entry = &transposition_table[hash % TRANSPOSITION_SIZE];


int scoreMove(Move* move) {
    GEN_HASH

    if (move->from == entry->bestMove.from && move->to == entry->bestMove.to) {
        return INFINITY;
    }


    PieceType movePiece = chess_get_piece_from_bitboard(board, move->from);

    int score = 0;

    if (move->capture) {
        score += 10.0f * chess_get_piece_from_bitboard(board, move->to) - movePiece;
    }

    // add piece if it is a promotion
    score += move->promotion;


    /* maybe if we get the bitboards
    int movelen;
    chess_skip_turn(board);
    Move* moves = chess_get_legal_moves(board, &movelen);

    BitBoard all_opp_attacked = 0;
    for (int i = 0; i < movelen; i++) {
        all_opp_attacked |= moves[i].to;
    }

    chess_undo_move(board);

    if (move->to & all_opp_attacked) {
        score -= movePiece;
    }

    */

    return score;
}

int compareMoves(const void* a, const void* b) {
#ifdef STATIC_ASSERTS
    int sa = scoreMove((Move*)a);
    int sb = scoreMove((Move*)b);
#else
    int sa = scoreMove(a);
    int sb = scoreMove(b);
#endif

    if (sa < sb) {
        return 1;
    }
    if (sa > sb) {
        return -1;
    }
    return 0;
}

#define max_best_value_and(X) fmaxf(bestValue, X)

// TODO: move depthleft to first parameter
int alphaBeta(int alpha, int beta, int depthleft) {
#ifdef STATS
    ++searched_nodes;
    uint64_t old_searched_nodes = searched_nodes;
    uint64_t old_cached_nodes = cached_nodes;
#endif

    if (chess_get_elapsed_time_millis() > time_left) {
        longjmp(timeout_jmp, 1234);
    }


#define is_not_quiescence depthleft > 0

    state = chess_get_game_state(board);
    if (state == GAME_STALEMATE) {
        return 0;
    }

    int alpha_orig = alpha;

    int bestValue = -INFINITY;
    int bestMoveIndex = 0;

    GEN_HASH
    if (is_not_quiescence) {
        if (entry->depth >= depthleft && entry->hash == hash
            && (entry->type == TYPE_EXACT || (entry->type == TYPE_LOWER_BOUND && entry->eval >= beta)
                || (entry->type == TYPE_UPPER_BOUND && entry->eval < alpha))) {
#ifdef STATS
            transposition_hits++;
            cached_nodes += entry->num_nodes;
#endif
            return entry->eval;
        }
    }
    else {
        bestValue = static_eval();
        if (bestValue >= beta) {
            return bestValue;
        }
        alpha = max_best_value_and(alpha);
    }

    bool is_check = chess_in_check(board);

    FETCH_MOVES
    qsort(moves, len_moves, sizeof(Move), compareMoves);

    for (int i = 0; i < len_moves; i++) {
        if (is_not_quiescence || moves[i].capture || is_check) {
            chess_make_move(board, moves[i]);
            int score;
            if (depthleft <= 2 || i == 0) {
                score = -alphaBeta(-beta, -alpha, depthleft - 1);
            }
            else {
                score = -alphaBeta(-alpha - 1, -alpha, depthleft - 1);
                if (score > alpha && score <= beta) {
                    score = -alphaBeta(-beta, -alpha, depthleft - 1);
#ifdef STATS
                    negascout_misses++;
                }
                else {
                    negascout_hits++;
#endif
                }
            }
            chess_undo_move(board);


            if (score > bestValue) {
                bestMoveIndex = i;
            }

            bestValue = max_best_value_and(score);
            alpha = max_best_value_and(alpha);
            if (score >= beta) {
#ifdef STATS
                if (i == 0) {
                    first_move_cuts++;
                }
                else {
                    first_move_non_cuts++;
                }
#endif

                break;
            }
        }
    }


    // TODO: maybe redundant, but lets leave it here for now
    if (is_not_quiescence && (entry->depth < depthleft || entry->hash != hash)) {
#ifdef STATS
        entry->num_nodes = (searched_nodes + cached_nodes) - (old_searched_nodes + old_cached_nodes);
        if (entry->type == TYPE_UNUSED) {
            hashes_used++;
            new_hashes++;
        }
        else {
            transposition_overwrites++;
        }
#endif

        entry->hash = hash;
        entry->eval = bestValue;
        entry->depth = depthleft;
        entry->type = bestValue <= alpha_orig ? TYPE_UPPER_BOUND
                    : bestValue >= beta       ? TYPE_LOWER_BOUND
                                              : TYPE_EXACT;
        entry->bestMove = moves[bestMoveIndex];
    }

    return bestValue;
}

#ifdef STATS
void print_tt_stats(uint64_t prev_searched_nodes) {
    printf(
        "info string Transposition Table\n"
        "info string    hits: %lu\n"
        "info string        hits/search: %f%%\n"
        "info string        hits/total: %f%%\n"
        "info string    overwrites: %lu\n"
        "info string        rate: %f%%\n"
        "info string    new_hashes: %lu\n"
        "info string Alpha-Beta Search\n"
        "info string    first move cuts: %f%%\n"
        "info string    negascout hits: %lu\n"
        "info string        misses: %lu\n"
        "info string        rate: %f%%\n"
        "info string Root Search\n"
        "info string    branching factor: %f\n"
        "info string    aspiration researches: %lu\n",
        transposition_hits,
        (float)transposition_hits / (float)(searched_nodes) * 100.0f,
        (float)cached_nodes / (float)(searched_nodes + cached_nodes) * 100.0f,
        transposition_overwrites,
        (float)transposition_overwrites / (float)(transposition_overwrites + new_hashes) * 100.0f,
        new_hashes,
        (float)first_move_cuts / (float)(first_move_non_cuts + first_move_cuts) * 100.0f,
        negascout_hits,
        negascout_misses,
        (float)negascout_hits / (float)(negascout_hits + negascout_misses) * 100.0f,
        (float)searched_nodes / (float)prev_searched_nodes,
        researches
    );
    fflush(stdout);
}
void print_stats(int depth, int bestValue, uint64_t prev_searched_nodes) {
    printf(
        "info depth %d score cp %d nodes %lu nps %lu hashfull %lu time %lu\n",
        depth,
        bestValue,
        searched_nodes,
        (searched_nodes * 1000) / (chess_get_elapsed_time_millis() + 1),
        hashes_used * 1000 / TRANSPOSITION_SIZE,
        chess_get_elapsed_time_millis()
    );
    print_tt_stats(prev_searched_nodes);
    fflush(stdout);
}
#endif

// TODO: maybe remove void
int main(void) {
    // TODO: make recursive
#ifdef STATS
    searched_nodes = 1;
#endif

    while (true) {
        board = chess_get_board();

        // TODO: divide by 20 to allow full-game time management
        time_left = chess_get_time_millis() / 30; // + increment /2 if we had that

        FETCH_MOVES
        Move prevBestMove = *moves, bestMove = prevBestMove;
        int prevBestValue = 0;

        uint64_t prev_searched_nodes = 0;

        for (int depth = 1; depth < 100; depth++) {
#ifdef STATS
            prev_searched_nodes = searched_nodes;

            searched_nodes = 0;
            transposition_hits = 0;
            cached_nodes = 0;
            transposition_overwrites = 0;
            new_hashes = 0;
            researches = 0;

            first_move_cuts = 0;
            first_move_non_cuts = 0;

            negascout_hits = 0;
            negascout_misses = 0;
#endif
            // TODO: don't compare to 0
            if (setjmp(timeout_jmp) != 0)
                goto search_canceled;

            int bestValue = -INFINITY;
            for (int i = 0; i < len_moves; i++) {
                chess_make_move(board, moves[i]);
                int alphaOffset = 25;
                int betaOffset = 25;
                int score;
                while (true) {
                    // invert prevBestValue back, because we also invert the search results
                    score = -alphaBeta(-prevBestValue - alphaOffset, -prevBestValue + betaOffset, depth);
                    // don't invert because both are inverted once
                    if (score <= prevBestValue - alphaOffset) {
                        alphaOffset *= 2;

                        // fail-low: the real score is lower than alpha (aka. prevBestValue - alphaOffset).
                        // so no need to search the exact value if this is already bad enough
                        if (score <= bestValue) {
                            break;
                        }
                    }
                    else if (score >= prevBestValue + betaOffset) {
                        betaOffset *= 2;
                        // fail-high: the real score is higher than beta (aka. prevBestValue + betaOffset).
                        // so we keep searching with a bigger window
                    }
                    else {
                        break;
                    }
#ifdef STATS
                    researches++;
#endif
                }
                prevBestValue = score;
                chess_undo_move(board);

                if (score > bestValue) {
                    bestValue = score;
                    bestMove = moves[i];
                }
            }

#ifdef STATS
            print_stats(depth, bestValue, prev_searched_nodes);
#endif


            prevBestMove = bestMove;
            if (bestValue >= INFINITY) {
                break;
            }
        }
    search_canceled:

        chess_push(prevBestMove);

        chess_free_board(board);

        chess_done();
    }
}
