#define _POSIX_C_SOURCE 200809L

#include "stddef.h"
#include "assert.h"
#include "stdio.h"
#include "stdlib.h"
#include "unistd.h"
#include "string.h"
#include "math.h"
#include "chessapi.h"
#include <sys/mman.h>
#include <stdint.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>


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
    (void)pipe(child_stdin_fds);

    int child_stdout_fds[2];
    (void)pipe(child_stdout_fds);

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
            (void)fgets(buffer, sizeof(buffer), stockfish_state.child_stdout);
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
        (void)fgets(buffer, sizeof(buffer), stockfish_state.child_stdout);
        printf("%s", buffer);
    }

    fprintf(stockfish_state.child_stdin, "position fen %s\n", fen);
    fprintf(stockfish_state.child_stdin, "isready\n");
    fflush(stockfish_state.child_stdin);

    while (strcmp(buffer, "readyok\n") != 0) {
        (void)fgets(buffer, sizeof(buffer), stockfish_state.child_stdout);
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
        (void)fgets(buffer, sizeof(buffer), stockfish_state.child_stdout);


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
// log256(3^(7690)) ≈ 1523.545204

char compressed_weights[1523] = {
    1,
    41,
    88,
    124,
};

#define LAYER_1_PARAMS (64 * 6 * 2)
#define LAYER_2_PARAMS (10)
#define LAYER_3_PARAMS (1)

constexpr const int layer_sizes[] = {LAYER_1_PARAMS, LAYER_2_PARAMS, LAYER_3_PARAMS};

template <int N, int M>
struct Matrix {
    float data[N][M];

    constexpr float at(int n, int m) const {
        assert(n >= 0 && n < N);
        assert(m >= 0 && m < M);
        return data[n][m];
    }
    constexpr float& at(int n, int m) {
        assert(n >= 0 && n < N);
        assert(m >= 0 && m < M);
        return data[n][m];
    }
    constexpr int getN() const {
        return N;
    }
    constexpr int getM() const {
        return M;
    }
};

Matrix<LAYER_1_PARAMS, LAYER_2_PARAMS> weights1;
Matrix<LAYER_2_PARAMS, LAYER_3_PARAMS> weights2;
Matrix<1, LAYER_2_PARAMS> biases1;
Matrix<1, LAYER_3_PARAMS> biases2;


/*
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
*/

typedef struct {
    uint64_t bitboards[2][6];
    int stockfish_eval;
    bool is_white;
} PreprocessedBoard;

float ask_static_eval(PreprocessedBoard board) {
    return (+(__builtin_popcountl(board.bitboards[WHITE][PAWN]) - __builtin_popcountl(board.bitboards[BLACK][PAWN])) * 100
            + (__builtin_popcountl(board.bitboards[WHITE][KNIGHT]) - __builtin_popcountl(board.bitboards[BLACK][KNIGHT])) * 300
            + (__builtin_popcountl(board.bitboards[WHITE][BISHOP]) - __builtin_popcountl(board.bitboards[BLACK][BISHOP])) * 320
            + (__builtin_popcountl(board.bitboards[WHITE][ROOK]) - __builtin_popcountl(board.bitboards[BLACK][ROOK])) * 500
            + (__builtin_popcountl(board.bitboards[WHITE][QUEEN]) - __builtin_popcountl(board.bitboards[BLACK][QUEEN])) * 900)
         * (board.is_white ? 1 : -1);
}

template <int N, int M, int O>
void matrix_multiply(const Matrix<N, M>& a, const Matrix<M, O>& b, Matrix<N, O>& output) {
    for (int y = 0; y < O; y++) {
        for (int x = 0; x < N; x++) {
            output.at(x, y) = 0;
            for (int i = 0; i < M; i++) {
                output.at(x, y) += a.at(x, i) * b.at(i, y);
            }
        }
    }
}
template <int N, int M>
void matrix_flatten(const Matrix<N, M>& a, Matrix<1, M>& b) {
    for (int y = 0; y < M; y++) {
        b.at(0, y) = 0;
        for (int x = 0; x < N; x++) {
            b.at(0, y) += a.at(x, y);
        }
    }
}
// a *= b (elementwise)
template <int N, int M>
void matrix_fold_el(Matrix<N, M>& a, const Matrix<N, M>& b) {
    for (int y = 0; y < M; y++) {
        for (int x = 0; x < N; x++) {
            a.at(x, y) *= b.at(x, y);
        }
    }
}
template <int N, int M>
void matrix_multiply_el(const Matrix<N, M>& a, const Matrix<N, M>& b, Matrix<N, M>& c) {
    for (int y = 0; y < M; y++) {
        for (int x = 0; x < N; x++) {
            c.at(x, y) = a.at(x, y) * b.at(x, y);
        }
    }
}

template <int N, int M>
void matrix_transpose(const Matrix<N, M>& a, Matrix<M, N>& b) {
    for (int y = 0; y < M; y++) {
        for (int x = 0; x < N; x++) {
            b.at(y, x) = a.at(x, y);
        }
    }
}

template <int N, int M>
void matrix_accumulate_thin(Matrix<N, M>& a, const Matrix<1, M>& b) {
    for (int y = 0; y < M; y++) {
        for (int x = 0; x < N; x++) {
            a.at(x, y) += b.at(0, y);
        }
    }
}

template <int N, int M>
void matrix_accumulate(Matrix<N, M>& a, const Matrix<N, M>& b) {
    for (int y = 0; y < M; y++) {
        for (int x = 0; x < N; x++) {
            a.at(x, y) += b.at(x, y);
        }
    }
}
template <int N, int M>
void matrix_reduce(Matrix<N, M>& a, const Matrix<N, M>& b) {
    for (int y = 0; y < M; y++) {
        for (int x = 0; x < N; x++) {
            a.at(x, y) -= b.at(x, y);
        }
    }
}

template <int N, int M>
void matrix_activate_tanh(Matrix<N, M>& a) {
    for (int y = 0; y < M; y++) {
        for (int x = 0; x < N; x++) {
            a.at(x, y) = tanhf(a.at(x, y));
        }
    }
}

template <int N, int M>
void matrix_deactivate_tanh(Matrix<N, M>& a) {
    for (int y = 0; y < M; y++) {
        for (int x = 0; x < N; x++) {
            float tanh = tanhf(a.at(x, y));
            a.at(x, y) = 1.0f - tanh * tanh;
        }
    }
}

template <int N, int M>
float matrix_loss(const Matrix<N, M>& prediction, const Matrix<N, M>& target) {
    float loss = 0;
    for (int y = 0; y < M; y++) {
        for (int x = 0; x < N; x++) {
            float this_error = prediction.at(x, y) - target.at(x, y);
            loss += this_error * this_error;
        }
    }

    return loss / (float)(N * M);
}

template <int batch_size, int l1_params>
void input_as_matrix(const PreprocessedBoard* boards, Matrix<batch_size, l1_params>& mat) {
    for (int b = 0; b < batch_size; b++) {
        for (int i = 0; i < l1_params; i++) {
            mat.at(b, i) = (boards[b].bitboards[i % 2][i / 2 % 6] >> (i / (6 * 2)) & 0b1);
            mat.at(b, i) *= 2.0f;
            mat.at(b, i) -= 1.0f;
        }
    }
}

template <int batch_size, int l1_params, int l2_params, int l3_params>
void pass_forwards(
    const Matrix<batch_size, l1_params>& inputs,
    Matrix<batch_size, l3_params>& predictions,
    Matrix<batch_size, l2_params>& unactive_out1,
    Matrix<batch_size, l3_params>& unactive_out2,
    Matrix<batch_size, l2_params>& active_out1,
    Matrix<batch_size, l3_params>& active_out2
) {
    Matrix<batch_size, l2_params> output1;
    matrix_multiply(inputs, weights1, output1);
    matrix_accumulate_thin(output1, biases1);
    unactive_out1 = output1;
    matrix_activate_tanh(output1);
    active_out1 = output1;

    Matrix<batch_size, l3_params> output2;
    matrix_multiply(output1, weights2, output2);
    matrix_accumulate_thin(output2, biases2);
    unactive_out2 = output2;
    matrix_activate_tanh(output2);
    active_out2 = output2;

    predictions = output2;
}

template <int batch_size, int l1_params, int l2_params, int l3_params>
void pass_backwards(
    float loss,
    const Matrix<batch_size, l3_params>& predictions,
    const Matrix<batch_size, l3_params>& targets,
    Matrix<batch_size, l2_params>& unactive_out1,
    Matrix<batch_size, l3_params>& unactive_out2,
    const Matrix<batch_size, l2_params>& active_out1,
    const Matrix<batch_size, l3_params>& active_out2
) {
    Matrix<batch_size, l3_params> y_hat_minus_y;
    y_hat_minus_y = predictions;

    // a -= b
    matrix_reduce(y_hat_minus_y, targets);

    matrix_deactivate_tanh(unactive_out2);

    Matrix<l2_params, batch_size> active_out_trans1;
    Matrix<batch_size, l3_params> bias_grad_wide2;

    Matrix<1, l3_params> bias_grad2;
    Matrix<l2_params, l3_params> weight_grad2;

    matrix_multiply_el(y_hat_minus_y, unactive_out2, bias_grad_wide2);
    matrix_transpose(active_out1, active_out_trans1);
    matrix_multiply(active_out_trans1, bias_grad_wide2, weight_grad2);
    matrix_flatten(bias_grad_wide2, bias_grad2);

    matrix_deactivate_tanh(unactive_out1);

    Matrix<batch_size, l2_params> delta1_wide;
    Matrix<l3_params, l2_params> weights2_trans;

    matrix_transpose(weights2, weights2_trans);
    matrix_multiply(bias_grad_wide2, weights2_trans, delta1_wide);


    Matrix<l2_params, batch_size> active_out_trans0;
    Matrix<batch_size, l2_params> bias_grad_wide1;

    Matrix<1, l2_params> bias_grad1;
    Matrix<l2_params, l2_params> weight_grad1;

    matrix_multiply_el(delta1_wide, unactive_out1, bias_grad_wide1);
    matrix_transpose(active_out1, active_out_trans0);
    matrix_multiply(active_out_trans0, bias_grad_wide1, weight_grad1);
    matrix_flatten(bias_grad_wide1, bias_grad1);
}


PreprocessedBoard preprocess_fen(char* fen) {
    Board* board = chess_board_from_fen(fen);

    PreprocessedBoard pp_board = {0};


    for (int color = 0; color < 2; color++) {
        for (int piece = 0; piece < 6; piece++) {
            for (int i = 0; i < 64; i++) {
                pp_board.bitboards[color][piece] = chess_get_bitboard(board, (PlayerColor)color, (PieceType)(piece + 1));
            }
        }
    }

    pp_board.is_white = chess_is_white_turn(board);

    chess_free_board(board);

    return pp_board;
}

size_t process_all_boards(char* string, PreprocessedBoard* boards, size_t board_size) {
    char* buffer = strtok(string, "\n");

    size_t num_boards = 0;

    do {
        PreprocessedBoard board = preprocess_fen(buffer);


        buffer = strtok(NULL, "\n");
        board.stockfish_eval = atoi(buffer);

        if (num_boards % 1'000'000 == 0) {
            printf("Processed first %lu boards\n", num_boards);
        }

        boards[num_boards] = board;

    } while ((buffer = strtok(NULL, "\n")) && ++num_boards < board_size);

    return num_boards;
}

typedef struct {
    size_t num_boards;
    PreprocessedBoard boards[];
} FileFormat;

int main(int argc, const char** argv) {
    if (argc >= 2 && !strcmp(argv[1], "preprocess")) {
        int fd = open("lichess_db_eval_processed.raw", O_RDONLY);
        struct stat sb;
        fstat(fd, &sb);
        printf("Size: %lu\n", (uint64_t)sb.st_size);

        const size_t fen_buffer_space = 5;

        char* memblock = (char*)mmap(NULL, sb.st_size + fen_buffer_space, PROT_READ | PROT_WRITE, MAP_PRIVATE, fd, 0);
        if (memblock == MAP_FAILED) {
            fprintf(stderr, "mmap failed %s", strerror(errno));
            exit(EXIT_FAILURE);
        }

        memset(memblock + sb.st_size, ' ', fen_buffer_space);

        memblock[sb.st_size + fen_buffer_space - 1] = '\0';

        // segfaults if we process all of them
        size_t max_boards = 72'000'000;

        FileFormat* file = (FileFormat*)malloc(sizeof(FileFormat) + max_boards * sizeof(PreprocessedBoard));

        file->num_boards = process_all_boards(memblock, file->boards, max_boards);

        printf("Processing done\n");

        int outfile = open("lichess_db_eval_processed.bin", O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (outfile < 0) {
            fprintf(stderr, "open failed %s", strerror(errno));
            exit(EXIT_FAILURE);
        }

        size_t bytes_to_write = sizeof(FileFormat) + file->num_boards * sizeof(PreprocessedBoard);

        printf("Writing %lu bytes\n", bytes_to_write);
        char* ptr = (char*)file;
        while (bytes_to_write > 0) {
            ssize_t n = write(outfile, ptr, bytes_to_write);
            if (n < 0) {
                perror("write failed");
                exit(EXIT_FAILURE);
            }
            ptr += n;
            bytes_to_write -= n;
        }
        printf("Done\n");

        close(outfile);

        free(file);
    }
    else {
        int fd = open("lichess_db_eval_processed.bin", O_RDONLY);
        if (fd < 0) {
            fprintf(stderr, "open failed %s", strerror(errno));
            exit(EXIT_FAILURE);
        }
        struct stat sb;
        fstat(fd, &sb);
        printf("Size: %lu\n", (uint64_t)sb.st_size);

        FileFormat* all_boards = (FileFormat*)mmap(NULL, sb.st_size, PROT_READ | PROT_WRITE, MAP_PRIVATE, fd, 0);
        if (all_boards == MAP_FAILED) {
            fprintf(stderr, "mmap failed %s", strerror(errno));
            exit(EXIT_FAILURE);
        }

        // decompress_weights(compressed_weights);


        for (int i = 0; i < weights1.getN(); i++) {
            for (int j = 0; j < weights1.getM(); j++) {
                weights1.at(i, j) = ((float)rand() / (float)RAND_MAX) * 2 - 1;
            }
        }
        for (int i = 0; i < weights2.getN(); i++) {
            for (int j = 0; j < weights2.getM(); j++) {
                weights2.at(i, j) = ((float)rand() / (float)RAND_MAX) * 2 - 1;
            }
        }
        for (int i = 0; i < biases1.getN(); i++) {
            for (int j = 0; j < biases1.getM(); j++) {
                biases1.at(i, j) = ((float)rand() / (float)RAND_MAX) * 2 - 1;
            }
        }
        for (int i = 0; i < biases2.getN(); i++) {
            for (int j = 0; j < biases2.getM(); j++) {
                biases2.at(i, j) = ((float)rand() / (float)RAND_MAX) * 2 - 1;
            }
        }

        int num_iterations = 1'000'000;

        const int num_epochs = 1000;
        const int batch_size = 1024 * 4;
        const float lr_decay = 0.98f;

        FILE* gnuplot = popen(
            "feedgnuplot"
            " --stream"
            " --lines"
            " --title 'Training Progress'"
            " --xlabel 'Epoch'"
            " --ylabel 'Loss'"
            //" --y2 1"
            " --ymin -100",
            "w"
        );


        float lr = 0.05;
        float prev_loss = INFINITY;

        for (int epoch = 0; epoch < num_epochs; epoch++) {
            float epoch_loss = 0.0f;
            float epoch_diff = 0.0f;
            float epoch_static_diff = 0.0f;

            if (epoch == 0) {
                printf("Shuffling\n");
                for (size_t i = all_boards->num_boards - 1; i > 0; i--) {
                    size_t j = (size_t)rand() % (i + 1);

                    PreprocessedBoard tmp = all_boards->boards[i];
                    all_boards->boards[i] = all_boards->boards[j];
                    all_boards->boards[j] = tmp;
                }
            }


            for (int i = 0; i < 1'000'000; i += batch_size) {
                Matrix<batch_size, 1> stockfish_eval;
                for (int j = 0; j < batch_size; j++) {
                    stockfish_eval.at(j, 0) = all_boards->boards[i + j].stockfish_eval;
                }

                Matrix<batch_size, LAYER_1_PARAMS> inputs;
                input_as_matrix(all_boards->boards, inputs);

                Matrix<batch_size, LAYER_3_PARAMS> outputs;

                Matrix<batch_size, LAYER_2_PARAMS> unactive_out1;
                Matrix<batch_size, LAYER_3_PARAMS> unactive_out2;
                Matrix<batch_size, LAYER_2_PARAMS> active_out1;
                Matrix<batch_size, LAYER_3_PARAMS> active_out2;

                pass_forwards(inputs, outputs, unactive_out1, unactive_out2, active_out1, active_out2);
                float sample_loss = matrix_loss(outputs, stockfish_eval);

                pass_backwards<batch_size, LAYER_1_PARAMS>(sample_loss, outputs, stockfish_eval, unactive_out1, unactive_out2, active_out1, active_out2);

                float static_eval = ask_static_eval(all_boards->boards[i]);

                epoch_loss += sample_loss;
                float sample_diff = fabsf(stockfish_eval.at(0, 0) - outputs.at(0, 0));
                epoch_diff += sample_diff;
                float static_diff = fabsf(stockfish_eval.at(0, 0) - static_diff);
                epoch_static_diff += static_diff;

                // Optional: print progress every batch
                if ((i + 1) % batch_size == 0) {
                    printf(
                        "[Epoch %d] %d/%lu (%.02f%%) | Loss: %.4f | Delta: ±%.4f | Static: ±%.4f\n",
                        epoch + 1,
                        i + 1,
                        all_boards->num_boards,
                        (float)(i + 1) / (float)all_boards->num_boards * 100.0f,
                        epoch_loss / (float)batch_size,
                        epoch_diff / (float)batch_size,
                        epoch_static_diff / (float)batch_size
                    );

                    if ((i + 1) / batch_size > 5) {
                        fprintf(gnuplot, "%f %f\n", epoch_loss / (float)batch_size, epoch_diff / (float)batch_size);
                        fflush(gnuplot);
                    }

                    epoch_loss = 0;
                    epoch_diff = 0;
                    epoch_static_diff = 0;
                }
                /*
                if ((i + 1) % batch_size == 0) {
                    for (int i = 0; i < LAYER_1_SIZE; i++) {
                        l1weights[i] = fmaxf(fminf(l1weights[i], 1), -1);
                    }
                    for (int i = 0; i < LAYER_2_SIZE; i++) {
                        l2weights[i] = fmaxf(fminf(l2weights[i], 1), -1);
                    }
                    printf("Requantized\n");
                }
                */
            }

            float avg_loss = epoch_loss / (float)all_boards->num_boards;
            printf("Epoch %d complete. Average loss: %.4f\n", epoch + 1, avg_loss);

            // Simple improvement tracking
            if (avg_loss < prev_loss) {
                printf("Loss improved! (%.4f → %.4f)\n", prev_loss, avg_loss);
                prev_loss = avg_loss;
            }
            else {
                printf("Loss did not improve this epoch.\n");
            }

            // Optionally decay learning rate inside ask_nn() or via global var
            lr *= lr_decay; // if you expose lr as global or static
        }

        printf("Training finished. Final average loss: %.4f\n", prev_loss);

        getchar();
        pclose(gnuplot);
    }

    exit(0);
    return 0;
}
