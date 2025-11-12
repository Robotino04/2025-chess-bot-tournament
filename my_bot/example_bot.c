#include "chessapi.h"
#include "stdlib.h"

#ifndef MINIMIZE
    #define STATS
    #include "stdio.h"

    #define STATIC_ASSERTS
#endif


// TODO: maybe use TT size as infinity
#undef INFINITY
#define INFINITY 10000000
#define NEGATIVE_INFINITY 0b11111111011001110110100110000000

#ifdef STATIC_ASSERTS
static_assert(-INFINITY == NEGATIVE_INFINITY, "-INFINITY != NEGATIVE_INFINITY");
static_assert(((long long)(int)INFINITY) == (long)INFINITY, "INFINITY is too large");
#endif

#define NEGATIVE_ONE 0b11111111111111111111111111111111

#ifdef STATIC_ASSERTS
static_assert(-1 == NEGATIVE_ONE, "-1 != NEGATIVE_ONE");
#endif

#define MIN(A, B) __builtin_fminf(A, B)
#define MAX(A, B) __builtin_fmaxf(A, B)


#define TRANSPOSITION_SIZE 0b100000000000000000000000000ul

#ifdef STATIC_ASSERTS
static_assert(TRANSPOSITION_SIZE % 2 == 0, "TRANSPOSITION_SIZE isn't a power of two");
static_assert(TRANSPOSITION_SIZE == (1ul << 26));
#endif

// define it here so minimize.py removes it before applying macros
#undef stdc_count_ones_ul
#define stdc_count_ones_ul(x) __builtin_popcountl(x)


#define TYPE_UNUSED 0
#define TYPE_EXACT 1
#define TYPE_UPPER_BOUND 2
#define TYPE_LOWER_BOUND 3


Board* board;

struct {
#ifdef STATS
    struct {
        uint64_t num_nodes;
    } stats;
#endif
    uint64_t hash;
    int eval;
    uint8_t type, depth, bestMove_from, bestMove_to;
} transposition_table[TRANSPOSITION_SIZE];

#ifdef STATIC_ASSERTS
constexpr size_t tt_size = sizeof transposition_table;
constexpr size_t stat_size = sizeof transposition_table[0].stats * TRANSPOSITION_SIZE;
constexpr size_t optimized_tt_size = tt_size - stat_size;
static_assert(optimized_tt_size <= 1024 * 1024 * 1024, "Transposition table is too big");
#endif


int history_table[8192], // [2][64][64]
    timeout_jmp[20];     // reserve more be safe: https://gcc.gnu.org/onlinedocs/gcc/Nonlocal-Gotos.html

#define INDEX_HISTORY_TABLE(FROM, TO) \
    history_table[chess_is_white_turn(board) * 4096 + chess_get_index_from_bitboard(FROM) * 64 + chess_get_index_from_bitboard(TO)]


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
uint64_t lmr_hits;
uint64_t lmr_misses;
#endif

#define MAX_MOVES 256
#define FETCH_MOVES        \
    Move moves[MAX_MOVES]; \
    int len_moves = chess_get_legal_moves_inplace(board, moves, MAX_MOVES);

#define SORT_MOVES qsort(moves, len_moves, sizeof *moves, compareMoves);

#define ITERATE_MOVES for (int i = NEGATIVE_ONE; ++i < len_moves;)

// TODO
// - [ ] test without custom libchess build
// - [ ] expand and remove unneeded parens


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
// don't give away queen: rn2k1nr/ppp2ppp/4p3/2bp1b2/3q4/P7/RPP1PPPP/1NBQKBNR w Kkq - 0 8
//
// midgame fail: r5k1/p6p/6p1/2Qb1r2/P6K/8/RP5P/6R1 w - - 0 33
// prevent promotion: 8/3K4/4P3/8/8/8/6k1/7q w - - 0 1

#define MATERIAL_OF(COLOR)                                                   \
    +stdc_count_ones_ul(chess_get_bitboard(board, COLOR, PAWN)) * 100        \
        + stdc_count_ones_ul(chess_get_bitboard(board, COLOR, KNIGHT)) * 300 \
        + stdc_count_ones_ul(chess_get_bitboard(board, COLOR, BISHOP)) * 320 \
        + stdc_count_ones_ul(chess_get_bitboard(board, COLOR, ROOK)) * 500   \
        + stdc_count_ones_ul(chess_get_bitboard(board, COLOR, QUEEN)) * 900

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

    int material = MATERIAL_OF(color);

    float endgame_weight = 16.0f;
    int king = chess_get_index_from_bitboard(chess_get_bitboard(board, color, KING));
    endgame_weight -= stdc_count_ones_ul(GET_ENDGAME_WEIGHT(color));

    color ^= 1;

    int king2 = chess_get_index_from_bitboard(chess_get_bitboard(board, color, KING));
    endgame_weight -= stdc_count_ones_ul(GET_ENDGAME_WEIGHT(color));


    // color is inverted already
    if (material > 200 /* there's a plus in the macro */ MATERIAL_OF(color)) {
#define king2_file king2 % 8
#define king2_rank king2 / 8
#define king1_file king % 8
#define king1_rank king / 8

        /*
        ORIGINAL:
        material += ((7 - MIN(king2_file, 7 - king2_file) - MIN(king2_rank, 7 - king2_rank)) * 5.0f
                     + (14 - abs(king % 8 - king2_file) - abs(king / 8 - king2_rank)))
                  * endgame_weight;
        ABS-IFIED:
        material += ((fabsf(king2_file - 3.5f) + fabsf(king2_rank - 3.5f)) * 5.0f
                     + (14 - abs(king % 8 - king2_file) - abs(king / 8 - king2_rank)))
                  * endgame_weight;

        ^^^^^ These are invalid now because endgame_weight has changed value
        */


        material += (14 + (__builtin_fabsf(king2_file - 3.5f) + __builtin_fabsf(king2_rank - 3.5f)) * 5.0f
                     - __builtin_abs(king1_file - king2_file) - __builtin_abs(king1_rank - king2_rank))
                  * endgame_weight / 16.0f;
    }

    return material;
}

/*
int static_eval() {
    if (state == GAME_CHECKMATE) {
        return NEGATIVE_INFINITY;
    }

    if (state == GAME_STALEMATE) {
        return 0;
    }

    return (chess_is_white_turn(board) ? 1.0f : -1.0f) * (static_eval_me(WHITE) - static_eval_me(BLACK));
}
*/


#define HASH chess_zobrist_key(board)
#define ENTRY transposition_table[HASH % TRANSPOSITION_SIZE]


int scoreMove(Move* move) {

    // clang-format off
#define SCORE_TIER_PV          10000000
#define SCORE_TIER_CAPTURE      1000000
#define SCORE_TIER_PROMOTION      50000
#define SCORE_TIER_KILLER         25000
#define MAX_HISTORY               10000
    // clang-format on

    return move->from == 1UL << ENTRY.bestMove_from && move->to == 1UL << ENTRY.bestMove_to ? SCORE_TIER_PV
         : move->capture ? SCORE_TIER_CAPTURE + 10 * chess_get_piece_from_bitboard(board, move->to)
                               - chess_get_piece_from_bitboard(board, move->from)
         : move->promotion ? SCORE_TIER_PROMOTION + move->promotion
                           : INDEX_HISTORY_TABLE(move->from, move->to);
}

int compareMoves(const void* a, const void* b) {
#ifdef STATIC_ASSERTS
    return scoreMove((Move*)b) - scoreMove((Move*)a);
#else
    return scoreMove(b) - scoreMove(a);
#endif
}

#define max_best_value_and(X) MAX(bestValue, X)


int alphaBeta(int depthleft, int alpha, int beta) {
    if ((int64_t)chess_get_elapsed_time_millis() >= MAX((int64_t)chess_get_time_millis() / 40, 5)) {
        __builtin_longjmp(timeout_jmp, 1);
    }

#ifdef STATS
    ++searched_nodes;
    uint64_t old_searched_nodes = searched_nodes;
    uint64_t old_cached_nodes = cached_nodes;
#endif


#define is_not_quiescence depthleft > 0


#define INFINITY_OVER_TWO 5000000

    // #define STATE_RETVALUE(X) (X == GAME_CHECKMATE) * NEGATIVE_INFINITY
#define STATE_RETVALUE(X) X* INFINITY_OVER_TWO - INFINITY_OVER_TWO

#ifdef STATIC_ASSERTS
    static_assert(INFINITY / 2 == INFINITY_OVER_TWO);

    static_assert(GAME_STALEMATE != 0);
    static_assert(GAME_CHECKMATE != 0);

    static_assert(GAME_STALEMATE == 1);
    static_assert(GAME_CHECKMATE == -1);
    static_assert(GAME_NORMAL == 0);

    static_assert((STATE_RETVALUE(GAME_STALEMATE)) == 0);
    static_assert((STATE_RETVALUE(GAME_CHECKMATE)) == -INFINITY);
#endif

// maybe slightly more expensive than storing it in a variable
#define GAME_STATE chess_get_game_state(board)

    if (GAME_STATE) {
        return STATE_RETVALUE(GAME_STATE);
    }

    int alpha_orig = alpha, bestValue = NEGATIVE_INFINITY, bestMoveIndex = 0;

    if (is_not_quiescence) {
        if (ENTRY.depth >= depthleft && ENTRY.hash == HASH
            && (ENTRY.type == TYPE_EXACT || (ENTRY.type == TYPE_LOWER_BOUND && ENTRY.eval >= beta)
                || (ENTRY.type == TYPE_UPPER_BOUND && ENTRY.eval < alpha))) {
#ifdef STATS
            transposition_hits++;
            cached_nodes += ENTRY.stats.num_nodes;
#endif
            return ENTRY.eval;
        }
    }
    else {
        bestValue = static_eval_me(WHITE) - static_eval_me(BLACK),      //
            bestValue *= chess_is_white_turn(board) ? 1 : NEGATIVE_ONE, //
            alpha = max_best_value_and(alpha);                          //
    }
    if (alpha >= beta) {
        return alpha;
    }

#define NULL_WINDOW -alpha - 1, -alpha
#define NORMAL_WINDOW -beta, -alpha

    bool is_check = chess_in_check(board);

    FETCH_MOVES
    SORT_MOVES

    ITERATE_MOVES {
        if (is_not_quiescence || moves[i].capture || is_check) {
            chess_make_move(board, moves[i]);

            int score;
            if (depthleft <= 2 || i == 0) {
                score = -alphaBeta(depthleft - 1, NORMAL_WINDOW);
            }
            else {
#define do_reduce !(moves[i].capture || is_check || i < 3)

                score = -alphaBeta(depthleft - 1 - do_reduce, NULL_WINDOW);

                if (score > alpha) {
                    // low-depth search looks promising so retry with full depth.
                    // This will always research even if we didn't even reduce depth.
                    // The TT should catch that in most cases so it's cheap.
                    //
                    // if (!dont_reduce)
                    score = -alphaBeta(depthleft - 1, NULL_WINDOW);

#ifdef STATS
                    lmr_misses++;
                }
                else {
                    lmr_hits++;
#endif
                }

                if (score > alpha && score <= beta) {
                    // full-depth search isn't conclusive, so try a full-window one
                    score = -alphaBeta(depthleft - 1, NORMAL_WINDOW);
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
                bestMoveIndex = i,     //
                    bestValue = score; //
            }

            alpha = max_best_value_and(alpha);
            if (alpha >= beta) {
#ifdef STATS
                if (i == 0) {
                    first_move_cuts++;
                }
                else {
                    first_move_non_cuts++;
                }
#endif

#define HISTORY_UPDATE_INDEX INDEX_HISTORY_TABLE(moves[i].from, moves[i].to)

                // this version is slightly better for some reason
#define UPDATE_HISTORY(BONUS) HISTORY_UPDATE_INDEX -= HISTORY_UPDATE_INDEX * BONUS / MAX_HISTORY - BONUS
                // #define UPDATE_HISTORY(BONUS) HISTORY_UPDATE_INDEX += BONUS - HISTORY_UPDATE_INDEX * BONUS / MAX_HISTORY

                // TODO: consider moving bonus to before "if" so it matches the condition in "while"; test if
                // that saves tokens.
                // only saves tokens if the loop "if" doesn't get the braces removed
                if (!moves[i].capture) {
                    int bonus = 300 * depthleft - 250;
                    UPDATE_HISTORY(bonus);

                    bonus /= -8;

                    while (--i >= 0) {
                        if (!moves[i].capture) {
                            UPDATE_HISTORY(bonus);
                        }
                    }
                }
                break;
            }
        }
    }


    // cannot be simplified because even though the depth is good, the score might not cause a cutoff
    if (is_not_quiescence && (ENTRY.depth < depthleft || ENTRY.hash != HASH)) {
#ifdef STATS
        ENTRY.stats.num_nodes = (searched_nodes + cached_nodes) - (old_searched_nodes + old_cached_nodes);
        if (ENTRY.type == TYPE_UNUSED) {
            hashes_used++;
            new_hashes++;
        }
        else {
            transposition_overwrites++;
        }
#endif

        ENTRY.hash = HASH,                                                                  //
            ENTRY.eval = bestValue,                                                         //
            ENTRY.depth = depthleft,                                                        //
            ENTRY.type = bestValue <= alpha_orig ? TYPE_UPPER_BOUND                         //
                       : bestValue >= beta       ? TYPE_LOWER_BOUND                         //
                                                 : TYPE_EXACT,                                    //
            ENTRY.bestMove_from = chess_get_index_from_bitboard(moves[bestMoveIndex].from), //
            ENTRY.bestMove_to = chess_get_index_from_bitboard(moves[bestMoveIndex].to);     //
    }

    return bestValue;
}

#ifdef STATS
void print_tt_stats(uint64_t prev_searched_nodes) {
    printf(
        "info string %ldms left"
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
        "info string    lmr hits: %lu\n"
        "info string        misses: %lu\n"
        "info string        rate: %f%%\n"
        "info string Root Search\n"
        "info string    branching factor: %f\n"
        "info string    aspiration researches: %lu\n",
        (int64_t)chess_get_time_millis(),
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
        lmr_hits,
        lmr_misses,
        (float)lmr_misses / (float)(lmr_hits + lmr_misses) * 100.0f,
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

int main() {
    // gcc doesn't like recursive main for some reason.
    // I guess we won't save that token
main_top:

    board = chess_get_board();

    // including the sort here saved one token at some point
    // TODO: recheck
    FETCH_MOVES
    SORT_MOVES

#ifdef STATS
    uint64_t prev_searched_nodes = 0;
    searched_nodes = 1;
#endif

    __builtin_memset(history_table, 0, sizeof history_table);

    // static to prevent longjmp clobbering
    static Move prevBestMove, bestMove;
    prevBestMove = bestMove = *moves;

    int prevBestValue = 0, depthleft = 1;

    while (depthleft++) {

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

        lmr_hits = 0;
        lmr_misses = 0;
#endif
        if (__builtin_setjmp(timeout_jmp)) {
            goto search_canceled;
        }

        int bestValue = NEGATIVE_INFINITY;

        SORT_MOVES

        ITERATE_MOVES {
            chess_make_move(board, moves[i]);
            int alphaOffset = 25, betaOffset = 25;
#ifdef STATS
            researches--; // remove the initial overcount
#endif
        aspiration_fail:
#ifdef STATS
            researches++;
#endif
            // invert prevBestValue back, because we also invert the search results
            int score = -alphaBeta(depthleft - 1, -prevBestValue - alphaOffset, -prevBestValue + betaOffset);
            // don't invert because both are inverted once
            if (score <= prevBestValue - alphaOffset && score > bestValue) {
                // fail-low: the real score is lower than alpha (aka. prevBestValue - alphaOffset).
                // still worth searching though because it is still higher than bestValue

                alphaOffset *= 2;
                goto aspiration_fail;
            }
            if (score >= prevBestValue + betaOffset) {
                // fail-high: the real score is higher than beta (aka. prevBestValue + betaOffset).
                // so we keep searching with a bigger window
                //
                betaOffset *= 2;
                goto aspiration_fail;
            }

            chess_undo_move(board);

            if (score > bestValue) {
                bestValue = prevBestValue = score, //
                    bestMove = moves[i];           //
            }
        }

#ifdef STATS
        print_stats(depthleft - 1, bestValue, prev_searched_nodes);
#endif


        prevBestMove = bestMove;
        if (bestValue >= INFINITY) {
            // stop searching if we found guaranteed mate
            break;
        }
    }


search_canceled:

    // TODO: use partial search results
    chess_push(prevBestMove);

    chess_free_board(board);

    chess_done();

    goto main_top;
}
