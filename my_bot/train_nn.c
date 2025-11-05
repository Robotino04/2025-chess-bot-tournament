#define _POSIX_C_SOURCE 1

#include "stddef.h"
#include "stdio.h"
#include "stdlib.h"
#include "unistd.h"
#include "math.h"
#include "string.h"
#include "chessapi.h"
#include <sys/mman.h>
#include <stdint.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>


#define MAX_MOVES 256
#define FETCH_MOVES(BOARD) \
    Move moves[MAX_MOVES]; \
    int num_moves = chess_get_legal_moves_inplace(BOARD, moves, MAX_MOVES)

int board_to_fen(Board* board, char fen[], size_t max_fen_length) {
    int fen_index = 0;

    int empty_accumulator = 0;
    for (int y = 7; y >= 0 && fen_index < max_fen_length; y--) {
        for (int x = 0; x < 8 && fen_index < max_fen_length; x++) {
            int i = x + y * 8;

            if (chess_get_piece_from_index(board, i)) {
                if (empty_accumulator > 0 && fen_index + 1 < max_fen_length) {
                    fen[fen_index++] = empty_accumulator + '0';
                    empty_accumulator = 0;
                }
            }

            switch (chess_get_piece_from_index(board, i)) {
                case PAWN:
                    fen[fen_index++] = chess_get_color_from_index(board, i) == WHITE ? 'P' : 'p';
                    break;
                case BISHOP:
                    fen[fen_index++] = chess_get_color_from_index(board, i) == WHITE ? 'B' : 'b';
                    break;
                case KNIGHT:
                    fen[fen_index++] = chess_get_color_from_index(board, i) == WHITE ? 'N' : 'n';
                    break;
                case ROOK:
                    fen[fen_index++] = chess_get_color_from_index(board, i) == WHITE ? 'R' : 'r';
                    break;
                case QUEEN:
                    fen[fen_index++] = chess_get_color_from_index(board, i) == WHITE ? 'Q' : 'q';
                    break;
                case KING:
                    fen[fen_index++] = chess_get_color_from_index(board, i) == WHITE ? 'K' : 'k';
                    break;
                default: empty_accumulator++; break;
            }

            if (x == 7 && y != 0 && fen_index < max_fen_length) {
                if (empty_accumulator > 0 && fen_index < max_fen_length) {
                    fen[fen_index++] = empty_accumulator + '0';
                    empty_accumulator = 0;
                }
                fen[fen_index++] = '/';
            }
        }
    }

    if (empty_accumulator > 0 && fen_index < max_fen_length) {
        fen[fen_index++] = empty_accumulator + '0';
        empty_accumulator = 0;
    }

    if (fen_index < max_fen_length) {
        fen[fen_index++] = ' ';
    }

    // white/black turn
    if (fen_index < max_fen_length) {
        fen[fen_index++] = chess_is_white_turn(board) ? 'w' : 'b';
    }

    if (fen_index < max_fen_length) {
        fen[fen_index++] = ' ';
    }

    // castling
    if (fen_index < max_fen_length && chess_can_kingside_castle(board, WHITE)) {
        fen[fen_index++] = 'K';
    }
    if (fen_index < max_fen_length && chess_can_queenside_castle(board, WHITE)) {
        fen[fen_index++] = 'Q';
    }
    if (fen_index < max_fen_length && chess_can_kingside_castle(board, BLACK)) {
        fen[fen_index++] = 'k';
    }
    if (fen_index < max_fen_length && chess_can_queenside_castle(board, BLACK)) {
        fen[fen_index++] = 'q';
    }
    if (fen_index < max_fen_length && fen[fen_index - 1] == ' ') {
        fen[fen_index++] = '-';
    }

    if (fen_index < max_fen_length) {
        fen[fen_index++] = ' ';
    }

    // en-passant
    if (fen_index < max_fen_length) {
        fen[fen_index++] = '-';
    }

    if (fen_index < max_fen_length) {
        fen[fen_index++] = ' ';
    }

    // turn counters
    if (fen_index < max_fen_length) {
        fen[fen_index++] = '0';
    }
    if (fen_index < max_fen_length) {
        fen[fen_index++] = ' ';
    }
    if (fen_index < max_fen_length) {
        fen[fen_index++] = '1';
    }


    return fen_index - 1;
}

static struct {
    bool is_started;

    FILE* child_stdin;
    FILE* child_stdout;
} stockfish_state = {
    .is_started = false,
};

void start_stockfish(void) {
    if (stockfish_state.is_started) {
        return;
    }

    int child_stdin_fds[2];
    pipe(child_stdin_fds);

    int child_stdout_fds[2];
    pipe(child_stdout_fds);

    printf("Starting stockfish\n");

    if (fork()) {
        stockfish_state.is_started = true;

        // parent
        close(child_stdin_fds[0]);
        close(child_stdout_fds[1]);

        stockfish_state.child_stdin = fdopen(child_stdin_fds[1], "w");
        stockfish_state.child_stdout = fdopen(child_stdout_fds[0], "r");


        fprintf(stockfish_state.child_stdin, "uci\n");
        fflush(stockfish_state.child_stdin);

        char buffer[250] = {0};

        while (strcmp(buffer, "uciok\n") != 0) {
            fgets(buffer, sizeof(buffer), stockfish_state.child_stdout);
            printf("%s", buffer);
        }

        printf("Stockfish ready\n");
    }
    else {
        // child
        dup2(child_stdin_fds[0], STDIN_FILENO);
        close(child_stdin_fds[1]);

        dup2(child_stdout_fds[1], STDOUT_FILENO);
        close(child_stdout_fds[0]);

        execlp("stockfish", "stockfish", NULL);
        exit(0);
    }
}

void stop_stockfish(void) {
    if (!stockfish_state.is_started) {
        return;
    }

    fprintf(stockfish_state.child_stdin, "quit\n");
    fflush(stockfish_state.child_stdin);

    fclose(stockfish_state.child_stdin);
    fclose(stockfish_state.child_stdout);

    stockfish_state.child_stdin = 0;
    stockfish_state.child_stdout = 0;

    stockfish_state.is_started = false;
}

int ask_stockfish(Board* board) {
    start_stockfish();

    char fen[250] = {0};
    board_to_fen(board, fen, sizeof(fen));

    char buffer[250] = {0};
    fprintf(stockfish_state.child_stdin, "ucinewgame\n");
    fprintf(stockfish_state.child_stdin, "isready\n");
    fflush(stockfish_state.child_stdin);

    while (strcmp(buffer, "readyok\n") != 0) {
        fgets(buffer, sizeof(buffer), stockfish_state.child_stdout);
        printf("%s", buffer);
    }

    fprintf(stockfish_state.child_stdin, "position fen %s\n", fen);
    fprintf(stockfish_state.child_stdin, "isready\n");
    fflush(stockfish_state.child_stdin);

    while (strcmp(buffer, "readyok\n") != 0) {
        fgets(buffer, sizeof(buffer), stockfish_state.child_stdout);
        printf("%s", buffer);
    }
    printf("ready to go\n");

    int expected_depth = 10;

    fprintf(stockfish_state.child_stdin, "go depth %d\n", expected_depth);
    fflush(stockfish_state.child_stdin);

    // right after "bestmove"
    bool has_eval = false;
    int eval = 0;
    while (!has_eval) {
        fgets(buffer, sizeof(buffer), stockfish_state.child_stdout);


        char* token = strtok(buffer, " \n");
        bool is_depth = false;
        bool has_depth = false;
        bool is_eval = false;
        while (token) {
            if (has_depth && is_eval) {
                eval = atoi(token);
                has_eval = true;
                break;
            }

            if (is_depth && expected_depth == atoi(token)) {
                has_depth = true;
            }

            if (strcmp(token, "depth") == 0) {
                is_depth = true;
            }
            else {
                is_depth = false;
            }

            if (strcmp(token, "cp") == 0) {
                is_eval = true;
            }
            else {
                is_eval = false;
            }

            token = strtok(NULL, " \n");
        }
    }

    return eval;
}

// TODO: to pointer
//  TODO: 64 bits per token
// log256(3^(64*6*2)) ≈ 152.1564001
char compressed_weights[152] = {
    1,
    41,
    88,
    124,
};

#define LAYER_1_SIZE (64 * 6 * 2)
int weights[LAYER_1_SIZE] = {0};

void decompress_weights(char* compressed_weights) {
    char c;
    uint32_t buffer = 0;
    int weight_index = 0;
    while ((c = *compressed_weights++)) {
        buffer = buffer << 8 | c;

        while (buffer >= 3) {
            weights[weight_index++] = buffer % 3 - 1;
            buffer /= 3;
        }
    }
    if (buffer) {
        weights[weight_index++] = buffer;
    }
}

int ask_nn(Board* board) {
    int out = 0;
    for (int i = 0; i < LAYER_1_SIZE; i++) {
        out += weights[i] * ((chess_get_bitboard(board, i % 2, i / 2 % 6) >> i / (6 * 2)) & 0b1);
    }

    return out;
}

int main(void) {
    char* memblock;
    int fd;
    struct stat sb;

    fd = open("lichess_db_eval_processed.raw", O_RDONLY);
    fstat(fd, &sb);
    printf("Size: %lu\n", (uint64_t)sb.st_size);

    memblock = mmap(NULL, sb.st_size, PROT_WRITE, MAP_PRIVATE, fd, 0);
    if (memblock == MAP_FAILED) {
        printf("mmap failed\n");
        exit(1);
    }

    memblock[sb.st_size] = '\0';

    decompress_weights(compressed_weights);

    uint64_t loss = 0;
    uint64_t prev_loss = 0;

    int weight_backup[LAYER_1_SIZE];


    while (true) {
        char* mod_memblock = strdup(memblock);

        char* buffer = strtok(mod_memblock, "\n");
        buffer = strtok(NULL, "\n");

        memcpy(weight_backup, weights, sizeof(weights));

        do {
            weights[rand() % LAYER_1_SIZE] = rand() % 3 - 1;
        } while (rand() % 10 == 0);

        loss = 0;


        for (uint64_t count = 0; count < 10'000; count++) {
            while (*buffer)
            Board* board = chess_board_from_fen(buffer);

            buffer = strtok(NULL, "\n");
            int stockfish_eval = atoi(buffer);

            // printf("Stockfish eval: %d\n", stockfish_eval);
            int nn_eval = ask_nn(board);
            // printf("NN eval: %d\n", nn_eval);

            loss += abs(stockfish_eval - nn_eval);

            chess_free_board(board);
        }
        printf("Loss: %lu\n", loss);

        if (loss < prev_loss) {
            prev_loss = loss;
        }
        else {
            memcpy(weights, weight_backup, sizeof(weights));
        }

        if (loss == 0) {
            break;
        }

        free(mod_memblock);
    }

    printf("Loss: %lu\n", loss);

    stop_stockfish();
    return 0;
}
