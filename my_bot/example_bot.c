#include "chessapi.h"
#include <stdlib.h>

int main(int argc, char* argv[]) {
    for (int i = 0; i < 500; i++) {
        Board* board = chess_get_board();

        int len_moves;
        Move* moves = chess_get_legal_moves(board, &len_moves);

        chess_push(moves[rand() % len_moves]);

        chess_free_moves_array(moves);
        chess_free_board(board);

        chess_done();
    }
}
