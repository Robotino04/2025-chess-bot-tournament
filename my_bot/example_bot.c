#include "chessapi.h"
#include <stdint.h>
#include <stdlib.h>
#include <math.h>
#include <stdio.h>
#include <limits.h>
#include <assert.h>
#include <stdbit.h>

// remove static
static Board* board;
static uint64_t time_left;

// TODO
// - [ ] macro away the board parameter: #define chess_get_legal_moves(...) chess_get_legal_moves(board, __VA_ARGS__)
// - [ ] remove braces of single-line if-statements
// - [ ] make common parameters globals
// - [ ] typedef unsigned long
// - [ ] make `nodes` global
// - [ ] test without custom libchess build
// - [ ] remove all const

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

// clang-format off
const float distance[] = {
    3, 3, 3, 3, 3, 3, 3, 3,
    3, 2, 2, 2, 2, 2, 2, 3,
    3, 2, 1, 1, 1, 1, 2, 3,
    3, 2, 1, 0, 0, 1, 2, 3,
    3, 2, 1, 0, 0, 1, 2, 3,
    3, 2, 1, 1, 1, 1, 2, 3,
    3, 2, 2, 2, 2, 2, 2, 3,
    3, 3, 3, 3, 3, 3, 3, 3,
};
// clang-format on

float material_of(PlayerColor color) {
    return (float)stdc_count_ones_ul(chess_get_bitboard(board, color, PAWN)) * 100.0f
         + (float)stdc_count_ones_ul(chess_get_bitboard(board, color, KNIGHT)) * 300.0f
         + (float)stdc_count_ones_ul(chess_get_bitboard(board, color, BISHOP)) * 320.0f
         + (float)stdc_count_ones_ul(chess_get_bitboard(board, color, ROOK)) * 500.0f
         + (float)stdc_count_ones_ul(chess_get_bitboard(board, color, QUEEN)) * 900.0f;
}

float static_eval_me(PlayerColor color) {
    // TODO: remove before submission
    static_assert(WHITE == 0, "WHITE isn't 0");
    static_assert(BLACK == 1, "BLACK isn't 1");
    static_assert((WHITE ^ 1) == BLACK, "WHITE isn't inverse of BLACK");
    static_assert((BLACK ^ 1) == WHITE, "BLACK isn't inverse of WHITE");

    float material = material_of(color);
    const float material2 = material_of(color ^ 1);

    float endgame_weight =
        (1.0f
         - (((float)stdc_count_ones_ul(
                chess_get_bitboard(board, WHITE, PAWN) | chess_get_bitboard(board, WHITE, KNIGHT)
                | chess_get_bitboard(board, WHITE, BISHOP) | chess_get_bitboard(board, WHITE, ROOK)
                | chess_get_bitboard(board, WHITE, QUEEN) | chess_get_bitboard(board, WHITE, KING)
                | chess_get_bitboard(board, BLACK, PAWN) | chess_get_bitboard(board, BLACK, KNIGHT)
                | chess_get_bitboard(board, BLACK, BISHOP) | chess_get_bitboard(board, BLACK, ROOK)
                | chess_get_bitboard(board, BLACK, QUEEN) | chess_get_bitboard(board, BLACK, KING)
            ))
            / 32.0f));

    int king = chess_get_index_from_bitboard(chess_get_bitboard(board, color, KING));
    int king2 = chess_get_index_from_bitboard(chess_get_bitboard(board, color ^ 1, KING));

    if (material > material2 + 200) {
        material += distance[king2] * endgame_weight * 10.0f;
        material += (14 - (abs(king % 8 - king2 % 8) + abs(king / 8 - king2 / 8))) * endgame_weight * 0.5f;
    }

    return material;
}

float static_eval(void) {
    if (chess_in_checkmate(board)) {
        return -INFINITY;
    }

    if (chess_in_draw(board)) {
        return 0;
    }

    return (chess_is_white_turn(board) ? 1.0f : -1.0f) * (static_eval_me(WHITE) - static_eval_me(BLACK));
}

float scoreMove(Move* move) {
    PieceType movePiece = chess_get_piece_from_bitboard(board, move->from);

    float score = 0.0f;
    if (move->capture) {
        score = 10.0f * chess_get_piece_from_bitboard(board, move->to) - movePiece;
    }

    if (move->promotion) { // "if" can be omitted
        score += move->promotion;
    }


    /* maybe if we get the bitboards
    int movelen;
    chess_skip_turn(board);
    Move* moves = chess_get_legal_moves(board, &movelen);

    BitBoard all_opp_attacked = 0;
    for (int i = 0; i < movelen; i++) {
        all_opp_attacked |= moves[i].to;
    }

    chess_free_moves_array(moves);
    chess_undo_move(board);

    if (move->to & all_opp_attacked) {
        score -= movePiece;
    }
    */

    return score;
}

int compareMoves(const void* a, const void* b) {
    float sa = scoreMove((Move*)a); // TODO: remove cast
    float sb = scoreMove((Move*)b); // TODO: remove cast

    if (sa < sb)
        return 1;
    if (sa > sb)
        return -1;
    return 0;
}

void orderMoves(Move* moves, int len) {
    qsort(moves, len, sizeof(Move), compareMoves);
}


float quiescence(float alpha, float beta, long* nodes) {
    if (chess_get_elapsed_time_millis() > time_left) {
        return 54321.0f;
    }
    ++*nodes;

    if (chess_in_draw(board)) {
        return 0;
    }

    float bestValue = static_eval();
    if (bestValue >= beta) {
        return bestValue;
    }
    alpha = fmaxf(alpha, bestValue);

    int len_moves;
    Move* moves = chess_get_legal_moves(board, &len_moves);
    orderMoves(moves, len_moves);

    for (int i = 0; i < len_moves; i++) {
        // maybe stupid because chekmate/.. only happens after making the move
        if (!(moves[i].capture || chess_in_checkmate(board) || chess_in_draw(board) || chess_in_check(board))) {
            continue;
        }

        chess_make_move(board, moves[i]);
        float score = -quiescence(-beta, -alpha, nodes);
        chess_undo_move(board);

        if (chess_get_elapsed_time_millis() > time_left) {
            bestValue = 54321.f;
            break;
        }

        bestValue = fmaxf(score, bestValue);
        alpha = fmaxf(score, alpha);
        if (score >= beta) {
            break;
        }
    }
done:

    chess_free_moves_array(moves);
    return bestValue;
}

float alphaBeta(float alpha, float beta, int depthleft, long* nodes) {
    if (chess_get_elapsed_time_millis() > time_left) {
        return -12345.f;
    }

    if (chess_in_draw(board)) {
        return 0;
    }

    if (depthleft <= 0) {
        return quiescence(alpha, beta, nodes);
    }

    ++*nodes;

    float bestValue = -INFINITY;
    int len_moves;
    Move* moves = chess_get_legal_moves(board, &len_moves);
    orderMoves(moves, len_moves);

    for (int i = 0; i < len_moves; i++) {
        chess_make_move(board, moves[i]);
        float score = -alphaBeta(-beta, -alpha, depthleft - 1, nodes);
        chess_undo_move(board);

        if (chess_get_elapsed_time_millis() > time_left) {
            bestValue = 12345.f;
            break;
        }

        bestValue = fmaxf(score, bestValue);
        alpha = fmaxf(score, alpha);
        if (score >= beta) {
            break;
        }
    }
done:

    chess_free_moves_array(moves);
    return bestValue;
}


int main(int argc, char* argv[]) {
    while (true) {
        board = chess_get_board();

        int len_moves;
        Move* moves = chess_get_legal_moves(board, &len_moves);
        Move prevBestMove = moves[0]; // TODO: move `moves` ptr instead of separate variable
        Move bestMove = moves[0];

        // TODO: divide by 20 to allow full-game time management
        time_left = chess_get_time_millis(); // + increment /2 if we had that

        for (int depth = 1; depth < 10; depth++) {
            float bestValue = -INFINITY;
            long nodes = 0;
            for (int i = 0; i < len_moves; i++) {
                chess_make_move(board, moves[i]);
                float score = -alphaBeta(-INFINITY, INFINITY, depth, &nodes);
                chess_undo_move(board);

                if (chess_get_elapsed_time_millis() > time_left) {
                    goto search_canceled;
                }

                if (score > bestValue) {
                    bestValue = score;
                    bestMove = moves[i];
                }
            }
            if (chess_get_elapsed_time_millis() > time_left) {
                goto search_canceled;
            }
            printf(
                "info depth %d score cp %d nodes %lu nps %lu time %lu\n",
                depth,
                (int)bestValue,
                nodes,
                (nodes * 1000) / (chess_get_elapsed_time_millis() + 1),
                chess_get_elapsed_time_millis()
            );
            fflush(stdout);
            prevBestMove = bestMove;
            if (bestValue == INFINITY) {
                break;
            }
        }
    search_canceled:

        chess_push(prevBestMove);

        chess_free_moves_array(moves);
        chess_free_board(board);

        chess_done();
    }
}
