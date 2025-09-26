#include "chessapi.h"
#include "stdlib.h"
#include "math.h"
#include "stdbit.h"

Board* board;
int depthleft, time_left;
GameState state;
float score;

#define if \
    ;if(
#define bclose ){
#define cret ) return
#define bsem );
#define oboard ( board
#define semclose \
    ;            \
    }
#define bcom ) ,

#define break \
    ;         \
    break semclose

#define FOR_MOVES \
    ;for (int i = 0; i < len_moves; i++ bclose
#define max_score fmaxf(score,
#define FREE_MOVES chess_free_moves_array(moves bsem
#define MAKE_MOVE chess_make_move oboard, moves[i] bsem
#define UNDO_MOVE chess_undo_move oboard bsem
#define DEREF_ENTRY entry->
#define IF_STATE_EQUALS \
    if state            \
    ==
#define GET_SORTED_MOVES \
    int len_moves; auto moves = chess_get_legal_moves oboard, &len_moves bsem qsort(moves, len_moves, sizeof(Move bcom  compareMoves bsem

#define BV_EQUALS bestValue =
#define SCORE_EQUALS score =
#define ALPHA_BETA BV_EQUALS max_score bestValue bsem alpha = max_score  alpha) if score >= beta) break
#define IF_TIME_UP                      \
    if chess_get_elapsed_time_millis () \
    > time_left


struct TranspositionEntry {
    int64_t hash, depth, type;
    float eval semclose transposition_table[0b10000000000000000000000000];

#define COUNT_COL(TYPE) +stdc_count_ones_ul(chess_get_bitboard(board, color, TYPE))*

float material_of(PlayerColor color bclose
    return 0.0f COUNT_COL(PAWN) 100.0f COUNT_COL(KNIGHT) 300.0f COUNT_COL(BISHOP) 320.0f COUNT_COL(ROOK
    ) 500.0f COUNT_COL(QUEEN) 900.0f semclose

#define chess_get_bitboard_color chess_get_bitboard oboard, color

#define GET_ENDGAME_WEIGHT(COLOR)                                                          \
    chess_get_bitboard_color  COLOR, KNIGHT) | chess_get_bitboard_color  COLOR, BISHOP)    \
        | chess_get_bitboard_color  COLOR, ROOK) | chess_get_bitboard_color  COLOR, QUEEN) \
        | chess_get_bitboard_color  COLOR, KING)

float static_eval_me(PlayerColor color bclose
    float material = material_of(color bcom  material2 = material_of(color ^ 1),
          endgame_weight = 1.0f - stdc_count_ones_ul(GET_ENDGAME_WEIGHT() | GET_ENDGAME_WEIGHT(^1)) / 16.0f;

    int king = chess_get_index_from_bitboard(chess_get_bitboard_color  , KING) bcom 
        king2 = chess_get_index_from_bitboard(chess_get_bitboard_color  ^ 1, KING))

    if material > material2 + 200 bclose
        int file = king2 % 8, rank = king2 / 8, dist_to_edge = fminf(file, 7 - file) + fminf(rank, 7 - rank bsem
        material += (7 - dist_to_edge) * endgame_weight * 5.0f;
        material += (14 - abs(king % 8 - file) - abs(king / 8 - rank)) * endgame_weight * 1.0f semclose 

return material semclose 


float static_eval( bclose
    IF_STATE_EQUALS GAME_CHECKMATE cret -INFINITY
    IF_STATE_EQUALS GAME_STALEMATE cret 0;


    return (chess_is_white_turn oboard) ? 1.0f : -1.0f) * (static_eval_me(WHITE) - static_eval_me(BLACK) bsem
}


#define GEN_HASH                                                                                                              \
    uint64_t hash_orig = chess_zobrist_key oboard bcom  hash = (hash_orig ^ (hash_orig >> 32)) & 0b01111111111111111111111111 ; \
    auto entry = &transposition_table[hash];


float scoreMove(Move* move bclose
    chess_make_move oboard, *move bsem
    GEN_HASH
    UNDO_MOVE

    PieceType movePiece = chess_get_piece_from_bitboard oboard, move->from bsem

    SCORE_EQUALS 0.0f
    if move->capture)
        SCORE_EQUALS 10.0f * chess_get_piece_from_bitboard oboard, move->to) - movePiece;


    return score + move->promotion + fmaxf( DEREF_ENTRY depth - 1, 0) * 100 + (entry->depth >= 2 ? DEREF_ENTRY eval : 0 bsem
    }

int compareMoves(const void* a, const void* b bclose
    float sa = scoreMove(a bcom  sb = scoreMove(b)

    if sa < sb cret 1
    if sa > sb cret -1;
    return 0 semclose 

float quiescence(float alpha, float beta bclose
    IF_TIME_UP cret 0;


    state = chess_get_game_state oboard)
    IF_STATE_EQUALS GAME_STALEMATE cret 0;

    float BV_EQUALS SCORE_EQUALS static_eval( bcom  score
    if bestValue >= beta cret bestValue;

    alpha = max_score alpha bsem

    GET_SORTED_MOVES
    bool is_check = chess_in_check oboard)
    if len_moves == 0 && !is_check)
        BV_EQUALS 0


    FOR_MOVES
        if moves[i].capture || is_check bclose
            MAKE_MOVE
            SCORE_EQUALS -quiescence(-beta, -alpha bsem
            UNDO_MOVE

            IF_TIME_UP bclose
                BV_EQUALS 54321.f
                break 


                ALPHA_BETA
}

    return bestValue semclose 


float alphaBeta(float alpha, float beta bclose
    IF_TIME_UP cret 0

    if depthleft <= 0 cret quiescence(alpha, beta bsem

    float alpha_orig = alpha;

    GEN_HASH

    if  DEREF_ENTRY depth >= depthleft && entry->hash == hash_orig && !entry->type || entry->type == 2
        &&  DEREF_ENTRY eval >= beta || entry->type == 1 && DEREF_ENTRY eval < alpha
        cret  DEREF_ENTRY eval;

    state = chess_get_game_state oboard)

    IF_STATE_EQUALS GAME_STALEMATE cret 0;


    float BV_EQUALS -INFINITY, score;
    GET_SORTED_MOVES 

    FOR_MOVES
        MAKE_MOVE
        depthleft--;
        SCORE_EQUALS -alphaBeta(-beta, -alpha bsem
        depthleft++;
        UNDO_MOVE

        IF_TIME_UP bclose
            BV_EQUALS 12345.f
            break 

        ALPHA_BETA

    FREE_MOVES

    if  DEREF_ENTRY depth <= depthleft bclose
         DEREF_ENTRY hash = hash_orig;
         DEREF_ENTRY eval = bestValue;
         DEREF_ENTRY depth = depthleft;
         DEREF_ENTRY type = bestValue <= alpha_orig ? 1 : bestValue >= beta ? 2 : 0 semclose 

return bestValue semclose 

int main(void bclose
    while (true bclose
        board = chess_get_board( bsem

        GET_SORTED_MOVES
        Move prevBestMove = moves[0], bestMove = prevBestMove; // TODO: move `moves` ptr instead of separate variable

        time_left = chess_get_time_millis( bsem // + increment /2 if we had that

        for (depthleft = 1; depthleft < 100; depthleft++ bclose
            float BV_EQUALS -INFINITY
             FOR_MOVES
                MAKE_MOVE
                SCORE_EQUALS -alphaBeta(-INFINITY, INFINITY bsem
                UNDO_MOVE

                IF_TIME_UP)
                    goto search_canceled


                if score > bestValue bclose
                    BV_EQUALS score;
                    bestMove = moves[i] semclose
}
    IF_TIME_UP)
                goto search_canceled;

prevBestMove = bestMove
if bestValue == INFINITY)
                break 
    search_canceled:

        chess_push(prevBestMove bsem

        FREE_MOVES
        chess_free_board oboard bsem

        chess_done( bsem
}
}
