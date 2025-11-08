#include <cstdlib>
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
    size_t fen_index = 0;

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
    .child_stdin = nullptr,
    .child_stdout = nullptr,
};

void start_stockfish(void) {
    if (stockfish_state.is_started) {
        return;
    }

    int child_stdin_fds[2];
    if (pipe(child_stdin_fds) < 0) {
        perror("pipe stdin");
    }

    int child_stdout_fds[2];
    if (pipe(child_stdout_fds)) {
        perror("pipe stdout");
    }

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
            if (fgets(buffer, sizeof(buffer), stockfish_state.child_stdout) == nullptr) {
                perror("fgets");
            }
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
        if (fgets(buffer, sizeof(buffer), stockfish_state.child_stdout) == nullptr) {
            perror("fgets");
        };
        printf("%s", buffer);
    }

    fprintf(stockfish_state.child_stdin, "position fen %s\n", fen);
    fprintf(stockfish_state.child_stdin, "isready\n");
    fflush(stockfish_state.child_stdin);

    while (strcmp(buffer, "readyok\n") != 0) {
        if (fgets(buffer, sizeof(buffer), stockfish_state.child_stdout) == nullptr) {
            perror("fgets");
        }
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
        if (fgets(buffer, sizeof(buffer), stockfish_state.child_stdout) == nullptr) {
            perror("fgets");
        }


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
#define LAYER_2_PARAMS (256)
#define LAYER_3_PARAMS (256)
#define LAYER_4_PARAMS (1)

template <int N, int M>
struct Matrix {
    float data[N * M];

    inline constexpr float at(int n, int m) const {
        assert(n >= 0 && n < N);
        assert(m >= 0 && m < M);
        return data[n + m * N];
    }
    inline constexpr float& at(int n, int m) {
        assert(n >= 0 && n < N);
        assert(m >= 0 && m < M);
        return data[n + m * N];
    }
    inline constexpr int getN() const {
        return N;
    }
    inline constexpr int getM() const {
        return M;
    }
};

Matrix<LAYER_1_PARAMS, LAYER_2_PARAMS> weights1;
Matrix<LAYER_2_PARAMS, LAYER_3_PARAMS> weights2;
Matrix<LAYER_3_PARAMS, LAYER_4_PARAMS> weights3;
Matrix<1, LAYER_2_PARAMS> biases1;
Matrix<1, LAYER_3_PARAMS> biases2;
Matrix<1, LAYER_4_PARAMS> biases3;


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
void matrix_multiply(const Matrix<N, M>& __restrict__ a, const Matrix<M, O>& __restrict__ b, Matrix<N, O>& __restrict__ output) {
#pragma omp parallel for collapse(2)
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
void matrix_flatten(const Matrix<N, M>& __restrict__ a, Matrix<1, M>& __restrict__ b) {
#pragma omp parallel for
    for (int y = 0; y < M; y++) {
        b.at(0, y) = 0;
        for (int x = 0; x < N; x++) {
            b.at(0, y) += a.at(x, y);
        }
    }
}
// a *= b (elementwise)
template <int N, int M>
void matrix_fold_el(Matrix<N, M>& __restrict__ a, const Matrix<N, M>& __restrict__ b) {
#pragma omp parallel for collapse(2)
    for (int y = 0; y < M; y++) {
        for (int x = 0; x < N; x++) {
            a.at(x, y) *= b.at(x, y);
        }
    }
}
template <int N, int M>
void matrix_multiply_el(const Matrix<N, M>& __restrict__ a, const Matrix<N, M>& __restrict__ b, Matrix<N, M>& __restrict__ c) {
#pragma omp parallel for collapse(2)
    for (int y = 0; y < M; y++) {
        for (int x = 0; x < N; x++) {
            c.at(x, y) = a.at(x, y) * b.at(x, y);
        }
    }
}
template <int N, int M>
void matrix_multiply_scalar_inplace(Matrix<N, M>& __restrict__ a, const float b) {
#pragma omp parallel for collapse(2)
    for (int y = 0; y < M; y++) {
        for (int x = 0; x < N; x++) {
            a.at(x, y) *= b;
        }
    }
}
template <int N, int M>
void matrix_sign_inplace(Matrix<N, M>& __restrict__ a) {
#pragma omp parallel for collapse(2)
    for (int y = 0; y < M; y++) {
        for (int x = 0; x < N; x++) {
            a.at(x, y) = a.at(x, y) > 0 ? 1.0f : -1.0f;
        }
    }
}


template <int N, int M>
void matrix_transpose(const Matrix<N, M>& __restrict__ a, Matrix<M, N>& __restrict__ b) {
#pragma omp parallel for collapse(2)
    for (int y = 0; y < M; y++) {
        for (int x = 0; x < N; x++) {
            b.at(y, x) = a.at(x, y);
        }
    }
}

template <int N, int M>
void matrix_accumulate_thin(Matrix<N, M>& __restrict__ a, const Matrix<1, M>& __restrict__ b) {
#pragma omp parallel for collapse(2)
    for (int y = 0; y < M; y++) {
        for (int x = 0; x < N; x++) {
            a.at(x, y) += b.at(0, y);
        }
    }
}

template <int N, int M>
void matrix_accumulate(Matrix<N, M>& __restrict__ a, const Matrix<N, M>& __restrict__ b) {
#pragma omp parallel for collapse(2)
    for (int y = 0; y < M; y++) {
        for (int x = 0; x < N; x++) {
            a.at(x, y) += b.at(x, y);
        }
    }
}
template <int N, int M>
void matrix_reduce(Matrix<N, M>& __restrict__ a, const Matrix<N, M>& __restrict__ b) {
#pragma omp parallel for collapse(2)
    for (int y = 0; y < M; y++) {
        for (int x = 0; x < N; x++) {
            a.at(x, y) -= b.at(x, y);
        }
    }
}

template <int N, int M>
void matrix_activate_tanh(Matrix<N, M>& __restrict__ a) {
#pragma omp parallel for collapse(2)
    for (int y = 0; y < M; y++) {
        for (int x = 0; x < N; x++) {
            a.at(x, y) = tanhf(a.at(x, y));
        }
    }
}

template <int N, int M>
void matrix_deactivate_tanh(Matrix<N, M>& __restrict__ a) {
#pragma omp parallel for collapse(2)
    for (int y = 0; y < M; y++) {
        for (int x = 0; x < N; x++) {
            float tanh = tanhf(a.at(x, y));
            a.at(x, y) = 1.0f - tanh * tanh;
        }
    }
}
template <int N, int M>
void matrix_deactivate_identity(Matrix<N, M>& __restrict__ a) {
#pragma omp parallel for collapse(2)
    for (int y = 0; y < M; y++) {
        for (int x = 0; x < N; x++) {
            a.at(x, y) = 1;
        }
    }
}

template <int N, int M>
float matrix_l2_loss(const Matrix<N, M>& __restrict__ prediction, const Matrix<N, M>& __restrict__ target) {
    float loss = 0;
#pragma omp parallel for collapse(2) reduction(+ : loss)
    for (int y = 0; y < M; y++) {
        for (int x = 0; x < N; x++) {
            float this_error = prediction.at(x, y) - target.at(x, y);
            loss += this_error * this_error;
        }
    }

    return loss / (float)(N * M);
}
template <int N, int M>
float matrix_l1_loss(const Matrix<N, M>& __restrict__ prediction, const Matrix<N, M>& __restrict__ target) {
    float loss = 0;
#pragma omp parallel for collapse(2) reduction(+ : loss)
    for (int y = 0; y < M; y++) {
        for (int x = 0; x < N; x++) {
            float this_error = prediction.at(x, y) - target.at(x, y);
            loss += fabsf(this_error);
        }
    }

    return loss / (float)(N * M);
}

template <int batch_size, int l1_params>
void input_as_matrix(const PreprocessedBoard* __restrict__ boards, Matrix<batch_size, l1_params>& __restrict__ mat) {
#pragma omp parallel for collapse(2)
    for (int b = 0; b < batch_size; b++) {
        for (int i = 0; i < l1_params; i++) {
            mat.at(b, i) = (boards[b].bitboards[i % 2][i / 2 % 6] >> (i / (6 * 2)) & 0b1);
            mat.at(b, i) *= 2.0f;
            mat.at(b, i) -= 1.0f;
        }
    }
}

template <int batch_size, int l1_params, int l2_params, int l3_params, int l4_params>
void pass_forwards(
    const Matrix<batch_size, l1_params>& inputs,
    Matrix<batch_size, l4_params>& predictions,
    Matrix<batch_size, l2_params>& unactive_out1,
    Matrix<batch_size, l3_params>& unactive_out2,
    Matrix<batch_size, l4_params>& unactive_out3,
    Matrix<batch_size, l2_params>& active_out1,
    Matrix<batch_size, l3_params>& active_out2,
    Matrix<batch_size, l4_params>& active_out3
) {
    static Matrix<batch_size, l2_params> output1;
    matrix_multiply(inputs, weights1, output1);
    matrix_accumulate_thin(output1, biases1);
    unactive_out1 = output1;
    matrix_activate_tanh(output1);
    active_out1 = output1;

    static Matrix<batch_size, l3_params> output2;
    matrix_multiply(output1, weights2, output2);
    matrix_accumulate_thin(output2, biases2);
    unactive_out2 = output2;
    matrix_activate_tanh(output2);
    active_out2 = output2;

    static Matrix<batch_size, l4_params> output3;
    matrix_multiply(output2, weights3, output3);
    matrix_accumulate_thin(output3, biases3);
    unactive_out3 = output3;
    matrix_activate_tanh(output3);
    active_out3 = output3;

    predictions = output3;
}

template <int batch_size, int input_params, int output_params>
void pass_backwards_once(
    Matrix<input_params, output_params> const& __restrict__ weights,
    Matrix<batch_size, output_params> const& __restrict__ error,
    Matrix<batch_size, input_params> const& __restrict__ activated_input,
    Matrix<batch_size, output_params> const& __restrict__ output_grad,
    Matrix<1, output_params>& __restrict__ bias_grad,
    Matrix<input_params, output_params>& __restrict__ weight_grad,
    Matrix<batch_size, input_params>& __restrict__ input_grad
) {
    static Matrix<input_params, batch_size> activated_input_trans;
    static Matrix<batch_size, output_params> bias_grad_wide;

    matrix_multiply_el(error, output_grad, bias_grad_wide);

    matrix_transpose(activated_input, activated_input_trans);
    matrix_multiply(activated_input_trans, bias_grad_wide, weight_grad);
    matrix_flatten(bias_grad_wide, bias_grad);

    static Matrix<output_params, input_params> weights_trans;

    matrix_transpose(weights, weights_trans);
    matrix_multiply(bias_grad_wide, weights_trans, input_grad);
}

template <int batch_size, int l1_params, int l2_params, int l3_params, int l4_params>
void pass_backwards(
    const float lr,
    const Matrix<batch_size, l4_params>& __restrict__ predictions,
    const Matrix<batch_size, l4_params>& __restrict__ targets,
    const Matrix<batch_size, l1_params>& __restrict__ inputs,
    Matrix<batch_size, l2_params>& __restrict__ unactive_out1,
    Matrix<batch_size, l3_params>& __restrict__ unactive_out2,
    Matrix<batch_size, l4_params>& __restrict__ unactive_out3,
    const Matrix<batch_size, l2_params>& __restrict__ active_out1,
    const Matrix<batch_size, l3_params>& __restrict__ active_out2
) {
    static Matrix<batch_size, l4_params> y_hat_minus_y;
    y_hat_minus_y = predictions;

    // a -= b
    matrix_reduce(y_hat_minus_y, targets);
    // matrix_sign_inplace(y_hat_minus_y);
    matrix_multiply_scalar_inplace(y_hat_minus_y, 1.0f / (float)batch_size);

    static Matrix<1, l4_params> bias_grad3;
    static Matrix<l3_params, l4_params> weight_grad3;
    static Matrix<batch_size, l3_params> delta2_wide;

    matrix_deactivate_tanh(unactive_out3);
    pass_backwards_once(weights3, y_hat_minus_y, active_out2, unactive_out3, bias_grad3, weight_grad3, delta2_wide);

    static Matrix<1, l3_params> bias_grad2;
    static Matrix<l2_params, l3_params> weight_grad2;
    static Matrix<batch_size, l2_params> delta1_wide;

    matrix_deactivate_tanh(unactive_out2);
    pass_backwards_once(weights2, delta2_wide, active_out1, unactive_out2, bias_grad2, weight_grad2, delta1_wide);

    static Matrix<1, l2_params> bias_grad1;
    static Matrix<l1_params, l2_params> weight_grad1;
    static Matrix<batch_size, l1_params> delta0_wide;

    matrix_deactivate_tanh(unactive_out1);
    pass_backwards_once(weights1, delta1_wide, inputs, unactive_out1, bias_grad1, weight_grad1, delta0_wide);

    // update
    const float step_size = lr;

    matrix_multiply_scalar_inplace(weight_grad1, -step_size);
    matrix_accumulate(weights1, weight_grad1);
    matrix_multiply_scalar_inplace(weight_grad2, -step_size);
    matrix_accumulate(weights2, weight_grad2);
    matrix_multiply_scalar_inplace(weight_grad3, -step_size);
    matrix_accumulate(weights3, weight_grad3);

    matrix_multiply_scalar_inplace(bias_grad1, -step_size);
    matrix_accumulate(biases1, bias_grad1);
    matrix_multiply_scalar_inplace(bias_grad2, -step_size);
    matrix_accumulate(biases2, bias_grad2);
    matrix_multiply_scalar_inplace(bias_grad3, -step_size);
    matrix_accumulate(biases3, bias_grad3);
}


PreprocessedBoard preprocess_fen(char* fen) {
    Board* board = chess_board_from_fen(fen);

    PreprocessedBoard pp_board = {};


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
        int depth = atoi(buffer);

        buffer = strtok(NULL, "\n");
        board.stockfish_eval = atoi(buffer);

        if (num_boards % 1'000'000 == 0) {
            printf("Processed first %lu boards\n", num_boards);
        }

        if (depth >= 15) {
            boards[num_boards++] = board;
        }

    } while ((buffer = strtok(NULL, "\n")) && num_boards < board_size);

    return num_boards;
}

extern "C" {

typedef struct {
    size_t num_boards;
    PreprocessedBoard boards[];
} FileFormat;
}

int compareBoard(void const* b1, void const* b2) {
    return ((PreprocessedBoard*)b1)->stockfish_eval - ((PreprocessedBoard*)b2)->stockfish_eval;
}

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
        size_t max_boards = 71'000'000;

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
        for (int i = 0; i < weights3.getN(); i++) {
            for (int j = 0; j < weights3.getM(); j++) {
                weights3.at(i, j) = ((float)rand() / (float)RAND_MAX) * 2 - 1;
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
        for (int i = 0; i < biases3.getN(); i++) {
            for (int j = 0; j < biases3.getM(); j++) {
                biases3.at(i, j) = ((float)rand() / (float)RAND_MAX) * 2 - 1;
            }
        }

        constexpr int num_epochs = 1000;
        constexpr int batch_size = 256;
        constexpr float lr_decay = 0.99f;

        constexpr int log_steps = 32;

        FILE* gnuplot = popen(
            "feedgnuplot"
            " --stream"
            " --lines"
            " --title 'Training Progress'"
            " --xlabel 'Epoch'"
            " --ylabel 'Loss'"
            " --y2label 'Absolute Error'"
            " --y2 1"
            " --y2 2"
            " --legend 0 'Loss'"
            " --legend 1 'Difference to Stockfish'"
            " --legend 2 'Improvement from static eval'",
            "w"
        );


        float lr = 0.001;

        for (int epoch = 0; epoch < num_epochs; epoch++) {
            float epoch_loss = 0.0f;
            float epoch_diff = 0.0f;
            float epoch_static_diff = 0.0f;

            if (epoch == 0 && false) {
                printf("Sorting\n");
                qsort(all_boards->boards, all_boards->num_boards, sizeof(PreprocessedBoard), compareBoard);
            }
            else {

                printf("Shuffling\n");
                for (size_t i = all_boards->num_boards - 1; i > 0; i--) {
                    size_t j = (size_t)rand() % (i + 1);

                    PreprocessedBoard tmp = all_boards->boards[i];
                    all_boards->boards[i] = all_boards->boards[j];
                    all_boards->boards[j] = tmp;
                }
            }


            printf("Training\n");
            for (size_t i = 0; i + batch_size - 1 < all_boards->num_boards; i += batch_size) {
                static Matrix<batch_size, LAYER_4_PARAMS> stockfish_eval;
                for (int j = 0; j < batch_size; j++) {
                    stockfish_eval.at(j, 0) = all_boards->boards[i + j].stockfish_eval
                                            * (all_boards->boards[i + j].is_white ? 1.0f : -1.0f);
                }
                matrix_multiply_scalar_inplace(stockfish_eval, 1.0f / 2000.0f);

                static Matrix<batch_size, LAYER_1_PARAMS> inputs;
                input_as_matrix(&all_boards->boards[i], inputs);

                static Matrix<batch_size, LAYER_4_PARAMS> outputs;

                static Matrix<batch_size, LAYER_2_PARAMS> unactive_out1;
                static Matrix<batch_size, LAYER_3_PARAMS> unactive_out2;
                static Matrix<batch_size, LAYER_4_PARAMS> unactive_out3;
                static Matrix<batch_size, LAYER_2_PARAMS> active_out1;
                static Matrix<batch_size, LAYER_3_PARAMS> active_out2;
                static Matrix<batch_size, LAYER_4_PARAMS> active_out3;

                pass_forwards(inputs, outputs, unactive_out1, unactive_out2, unactive_out3, active_out1, active_out2, active_out3);
                epoch_loss += matrix_l2_loss(outputs, stockfish_eval);

                pass_backwards(lr, outputs, stockfish_eval, inputs, unactive_out1, unactive_out2, unactive_out3, active_out1, active_out2);


                for (int b = 0; b < batch_size; b++) {
                    float static_eval = ask_static_eval(all_boards->boards[i + b]);
                    float unscaled_stockfish_eval = all_boards->boards[i + b].stockfish_eval
                                                  * (all_boards->boards[i + b].is_white ? 1.0f : -1.0f);

                    epoch_diff += fabsf(unscaled_stockfish_eval - outputs.at(b, 0) * 2000.0f);
                    epoch_static_diff += fabsf(unscaled_stockfish_eval - static_eval);
                }

                if (epoch == 0 && i > all_boards->num_boards / 2) {
                    break;
                }

                if (epoch == 0 && i < batch_size * log_steps * 10) {
                    epoch_loss = 0;
                    epoch_diff = 0;
                    epoch_static_diff = 0;
                }

                if ((i + batch_size) % (log_steps * batch_size) == 0 && (epoch != 0 || i > batch_size * log_steps * 10)) {
                    printf(
                        "[Epoch %d] %zu/%lu (%.02f%%) | Loss: %.4f | Delta: ±%.4f | Static: ±%.4f| "
                        "improvement: %.4f\n",
                        epoch + 1,
                        i + batch_size,
                        all_boards->num_boards,
                        (float)(i + batch_size) / (float)all_boards->num_boards * 100.0f,
                        epoch_loss / (float)(log_steps),
                        epoch_diff / (float)(log_steps * batch_size),
                        epoch_static_diff / (float)(log_steps * batch_size),
                        (epoch_static_diff - epoch_diff) / (float)(log_steps * batch_size)
                    );

                    if ((i + batch_size) / batch_size > 5) {
                        fprintf(
                            gnuplot,
                            "%f %f %f\n",
                            epoch_loss / (float)(log_steps),
                            epoch_diff / (float)(log_steps * batch_size),
                            (epoch_static_diff - epoch_diff) / (float)(log_steps * batch_size)
                        );
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

            lr *= lr_decay;
        }

        printf("Done training\n");

        getchar();
        pclose(gnuplot);
    }

    exit(0);
    return 0;
}
