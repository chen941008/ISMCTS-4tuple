/**
 * @file gst.cpp
 * @brief Implementation of Game State (GST) logic and Main Entry Point.
 * * Contains board logic, move generation, N-Tuple heuristics, and the main simulation loop.
 * @author Original Project Team (Inherited Code)
 * @author Chen You-Kai (Optimization & Docs)
 */

#define _CRT_RAND_S

#include "bitboard_local.hpp"

#include "4T_DATA.hpp"
#include "ismcts.hpp"
#include "mcts.hpp"

// ==========================================
// Selection Strategy Configuration
// ==========================================
// SELECTION_MODE = 2 -> Softmax Sampling (Default)
// SELECTION_MODE = 1 -> Linear Weight Sampling (p_i = w_i / Σw)
// SELECTION_MODE = 0 -> Argmax (Greedy)
// Compatibility for old flags: -DUSE_SOFTMAX_SELECTION=1/0 maps to 2/1
#ifndef SELECTION_MODE
#ifdef USE_SOFTMAX_SELECTION
#if USE_SOFTMAX_SELECTION
#define SELECTION_MODE 2
#else
#define SELECTION_MODE 1
#endif
#else
#define SELECTION_MODE 2
#endif
#endif

#if SELECTION_MODE == 2
#pragma message("Compiling with softmax selection")
#elif SELECTION_MODE == 1
#pragma message("Compiling with linear selection")
#else
#pragma message("Compiling with argmax selection")
#endif

// ==========================================
// Random Number Generator
// ==========================================
// Thread-local PCG32 RNG: Seeded once, reused throughout the thread's life
static thread_local pcg32 rng(0);

// Helper lambda: Generates double u in [0, 1)
auto next_u01 = []() {
	return static_cast<double>(rng()) / (static_cast<double>(pcg32::max()) + 1.0);
};

// ==========================================
// Static Lookups & Constants
// ==========================================
// Character map for piece indexing
static std::map<char, int> piece_index = {
	{'A', 0}, {'B', 1}, {'C', 2},  {'D', 3},  {'E', 4},	 {'F', 5},	{'G', 6},  {'H', 7},
	{'a', 8}, {'b', 9}, {'c', 10}, {'d', 11}, {'e', 12}, {'f', 13}, {'g', 14}, {'h', 15}};

// Character map for directions
static std::map<char, int> dir_index = {{'N', 0}, {'W', 1}, {'E', 2}, {'S', 3}};

// Index map for printing characters
static std::map<int, char> print_piece = {
	{0, 'A'}, {1, 'B'}, {2, 'C'},  {3, 'D'},  {4, 'E'},	 {5, 'F'},	{6, 'G'},  {7, 'H'},
	{8, 'a'}, {9, 'b'}, {10, 'c'}, {11, 'd'}, {12, 'e'}, {13, 'f'}, {14, 'g'}, {15, 'h'}};

// Initial Board Positions [Player][PieceIndex]
static const int init_pos[2][PIECES] = {
	{25, 26, 27, 28, 31, 32, 33, 34},  // Player 0 (User)
	{10, 9, 8, 7, 4, 3, 2, 1}		   // Player 1 (Enemy)
};

// Direction Offsets: {N, W, E, S}
static const int dir_val[4] = {-COL, -1, 1, COL};

// N-Tuple Pattern Offsets
static const int offset_1x4[4] = {0, 1, 2, 3};	  // Horizontal 1x4
static const int offset_2x2[4] = {0, 1, 6, 7};	  // Square 2x2
static const int offset_4x1[4] = {0, 6, 12, 18};  // Vertical 4x1

const int MAP_36_TO_64[36] = {9,  10, 11, 12, 13, 14, 17, 18, 19, 20, 21, 22,
							  25, 26, 27, 28, 29, 30, 33, 34, 35, 36, 37, 38,
							  41, 42, 43, 44, 45, 46, 49, 50, 51, 52, 53, 54};

const int MAP_64_TO_36[64] = {
	-1, -1, -1, -1, -1, -1, -1, -1,	 // 0~7:   Top Moat (上護城河)
	-1, 0,	1,	2,	3,	4,	5,	-1,	 // 8~15:  Row 0 + Side Moats
	-1, 6,	7,	8,	9,	10, 11, -1,	 // 16~23: Row 1 + Side Moats
	-1, 12, 13, 14, 15, 16, 17, -1,	 // 24~31: Row 2 + Side Moats
	-1, 18, 19, 20, 21, 22, 23, -1,	 // 32~39: Row 3 + Side Moats
	-1, 24, 25, 26, 27, 28, 29, -1,	 // 40~47: Row 4 + Side Moats
	-1, 30, 31, 32, 33, 34, 35, -1,	 // 48~55: Row 5 + Side Moats
	-1, -1, -1, -1, -1, -1, -1, -1	 // 56~63: Bottom Moat (下護城河)
};

constexpr uint64_t VALID_BOARD_MASK = 0x007E7E7E7E7E7E00ULL;

namespace {
constexpr int EXIT_MAX_DIST = 7;

constexpr int bb_index_from_row_col(int row, int col) { return (row + 1) * 8 + (col + 1); }

constexpr uint64_t build_user_exit_dist_mask(int dist) {
	uint64_t mask = 0ULL;
	for (int r = 0; r < ROW; ++r) {
		for (int c = 0; c < COL; ++c) {
			const int d_left = r + c;
			const int d_right = r + (COL - 1 - c);
			const int d = (d_left < d_right) ? d_left : d_right;
			if (d == dist) mask |= (1ULL << bb_index_from_row_col(r, c));
		}
	}
	return mask;
}

constexpr uint64_t build_enemy_exit_dist_mask(int dist) {
	uint64_t mask = 0ULL;
	for (int r = 0; r < ROW; ++r) {
		for (int c = 0; c < COL; ++c) {
			const int d_left = (ROW - 1 - r) + c;
			const int d_right = (ROW - 1 - r) + (COL - 1 - c);
			const int d = (d_left < d_right) ? d_left : d_right;
			if (d == dist) mask |= (1ULL << bb_index_from_row_col(r, c));
		}
	}
	return mask;
}

constexpr uint64_t USER_BLUE_EXIT_DIST_MASKS[EXIT_MAX_DIST + 1] = {
	build_user_exit_dist_mask(0), build_user_exit_dist_mask(1), build_user_exit_dist_mask(2),
	build_user_exit_dist_mask(3), build_user_exit_dist_mask(4), build_user_exit_dist_mask(5),
	build_user_exit_dist_mask(6), build_user_exit_dist_mask(7)};

constexpr uint64_t ENEMY_BLUE_EXIT_DIST_MASKS[EXIT_MAX_DIST + 1] = {
	build_enemy_exit_dist_mask(0), build_enemy_exit_dist_mask(1), build_enemy_exit_dist_mask(2),
	build_enemy_exit_dist_mask(3), build_enemy_exit_dist_mask(4), build_enemy_exit_dist_mask(5),
	build_enemy_exit_dist_mask(6), build_enemy_exit_dist_mask(7)};

constexpr int EXIT_DIST_SCORE[EXIT_MAX_DIST + 1] = {8, 7, 6, 5, 4, 3, 2, 1};

inline void stamp_feature_cache(uint64_t mask, int feature, int* feature_cache) {
	while (mask) {
		const int bb = __builtin_ctzll(mask);
		const int p36 = MAP_64_TO_36[bb];
		if (p36 >= 0) feature_cache[p36] = feature;
		mask &= (mask - 1);
	}
}

inline bool has_unknown_in_side(const int* pos, const int* color, int begin, int end, int unknown) {
	for (int i = begin; i < end; ++i) {
		if (pos[i] != -1 && color[i] == unknown) return true;
	}
	return false;
}

inline int exit_proximity_score(uint64_t blue_bits, const uint64_t* dist_masks) {
	int score = 0;
	for (int d = 0; d <= EXIT_MAX_DIST; ++d) {
		score += __builtin_popcountll(blue_bits & dist_masks[d]) * EXIT_DIST_SCORE[d];
	}
	return score;
}
}  // namespace

// ==========================================
// GST Implementation
// ==========================================

/**
 * @brief Initializes the board and randomly assigns Red pieces.
 */
void GST::init_board() {
	/*
		Board Layout Reference:
		A  B  C  D  E  F  G  H  a  b  c  d  e  f  g  h
		0  1  2  3  4  5  6  7  8  9 10 11 12 13 14 15  <-piece index
	   25 26 27 28 31 32 33 34 10  9  8  7  4  3  2  1  <-position on board
	*/

	memset(board, 0, sizeof(board));
	memset(pos, 0, sizeof(pos));
	memset(revealed, false, sizeof(revealed));
	for (int i = 0; i < ROW * COL; i++) piece_board[i] = -1;
	for (int i = 0; i < 4; i++) piece_nums[i] = 4;

	userRed = 0ULL;
	userBlue = 0ULL;
	enemyRed = 0ULL;
	enemyBlue = 0ULL;
	enemyUnknown = 0ULL;

	// Default all pieces to BLUE first
	for (int i = 0; i < PIECES; i++) {
		color[i] = BLUE;
		color[i + 8] = -BLUE;
	}
	nowTurn = USER;
	winner = -1;
	n_plies = 0;
	is_escape = false;

	// Randomly assign RED pieces for Player 0 (User)
	int red_num = 0;
	bool red_or_not[8];
	std::fill(std::begin(red_or_not), std::end(red_or_not), false);
	char red[4];
	char red2[4];

	while (red_num != 4) {
		int x = rng(8);
		if (!red_or_not[x]) {
			red[red_num] = print_piece[x];
			red_or_not[x] = true;
			red_num += 1;
		}
	}

	// Randomly assign RED pieces for Player 1 (Enemy)
	red_num = 0;
	std::fill(std::begin(red_or_not), std::end(red_or_not), false);
	while (red_num != 4) {
		int x = rng(8);
		if (!red_or_not[x]) {
			red2[red_num] = print_piece[x + 8];
			red_or_not[x] = true;
			red_num += 1;
		}
	}

	// Apply colors and reveal flags
	for (int i = 0; i < 4; i++) {
		color[piece_index[red[i]]] = RED;
		color[piece_index[red2[i]]] = -RED;

		// Reveal logic for enemy red pieces (if applicable rules)
		// if (piece_index[red2[i]] >= PIECES) {
		//	revealed[piece_index[red2[i]]] = true;
		//}
	}
	// Reveal all enemy blue pieces by rule? (Legacy logic maintained)
	// for (int i = PIECES; i < PIECES * 2; i++) {
	//	if (color[i] == -BLUE) {
	//		revealed[i] = true;
	//	}
	//}

	// Set pieces on the board
	int offset = 0;
	for (int player = 0; player < 2; player++) {
		for (int i = 0; i < PIECES; i++) {
			int pos_val = init_pos[player][i];
			int bb_pos = MAP_36_TO_64[pos_val];
			board[pos_val] = color[i + offset];
			piece_board[pos_val] = i + offset;
			pos[i + offset] = pos_val;

			if (player == 0) {	// User
				if (color[i + offset] == RED) {
					userRed |= (1ULL << bb_pos);
				} else {
					userBlue |= (1ULL << bb_pos);
				}
			} else {  // Enemy
				if (color[i + offset] == -RED) {
					enemyRed |= (1ULL << bb_pos);
				} else if (color[i + offset] == -BLUE) {
					enemyBlue |= (1ULL << bb_pos);
				} else {
					enemyUnknown |= (1ULL << bb_pos);
				}
			}
		}
		offset += 8;
	}

	return;
}

/**
 * @brief Prints the board, remaining pieces, and captured pieces to console.
 */
void GST::print_board() {
	for (int i = 0; i < ROW; i++) {
		for (int j = 0; j < COL; j++) {
			if (piece_board[i * ROW + j] != -1)
				printf("%4c", print_piece[piece_board[i * ROW + j]]);
			else if (i == 0 && j == 0)
				printf("%4c", '<');	 // Entry point
			else if (i == 0 && j == COL - 1)
				printf("%4c", '>');	 // Exit point
			else
				printf("%4c", '-');
		}
		printf("\n");
	}
	printf("\n");
	printf("User remaining ghosts: ");
	for (int i = 0; i < PIECES; i++) {
		if (pos[i] != -1) printf("%c: %s ", print_piece[i], color[i] == RED ? "red" : "blue");
	}
	printf("\n");
	printf("Eaten enemy ghosts: ");
	for (int i = 8; i < PIECES * 2; i++) {
		if (pos[i] == -1) printf("%c: %s ", print_piece[i], color[i] == -RED ? "red" : "blue");
	}
	printf("\n");
}

/**
 * @brief Generates valid moves for a single piece.
 */
int GST::gen_move(int* move_arr, int piece, int location, int& count) {
	int row = location / ROW;
	int col = location % COL;

	if (nowTurn == USER) {
		// Normal moves: Up, Down, Left, Right
		if (row != 0 && board[location - 6] <= 0) move_arr[count++] = piece << 4;			 // Up
		if (row != ROW - 1 && board[location + 6] <= 0) move_arr[count++] = piece << 4 | 3;	 // Down
		if (col != 0 && board[location - 1] <= 0) move_arr[count++] = piece << 4 | 1;		 // Left
		if (col != COL - 1 && board[location + 1] <= 0)
			move_arr[count++] = piece << 4 | 2;	 // Right

		// Escape moves (Blue pieces only)
		if (color[piece] == BLUE) {
			if (location == 0) move_arr[count++] = piece << 4 | 1;	// Exit at Top-Left
			if (location == 5) move_arr[count++] = piece << 4 | 2;	// Exit at Top-Right
		}
	} else {  // ENEMY Turn
		if (row != 0 && board[location - 6] >= 0) move_arr[count++] = piece << 4;
		if (row != ROW - 1 && board[location + 6] >= 0) move_arr[count++] = piece << 4 | 3;
		if (col != 0 && board[location - 1] >= 0) move_arr[count++] = piece << 4 | 1;
		if (col != COL - 1 && board[location + 1] >= 0) move_arr[count++] = piece << 4 | 2;

		// Escape moves (Blue pieces only)
		if (color[piece] == -BLUE) {
			if (location == 30) move_arr[count++] = piece << 4 | 1;	 // Exit at Bottom-Left
			if (location == 35) move_arr[count++] = piece << 4 | 2;	 // Exit at Bottom-Right
		}
	}
	return count;
}
// ==========================================
// gen_all_move — Bitboard Shift Version (COMMENTED OUT)
// ==========================================
/*
int GST::gen_all_move(int* move_arr) {
	int count = 0;
	uint64_t m;

	if (nowTurn == USER) {
		const uint64_t my_pieces = userRed | userBlue;
		const uint64_t valid_targets = VALID_BOARD_MASK & ~my_pieces;

		// North
		m = (my_pieces >> 8) & valid_targets;
		while (m) {
			const int bb_dst = __builtin_ctzll(m);
			move_arr[count++] = (piece_board[MAP_64_TO_36[bb_dst + 8]] << 4) | 0;
			m &= m - 1;
		}
		// South
		m = (my_pieces << 8) & valid_targets;
		while (m) {
			const int bb_dst = __builtin_ctzll(m);
			move_arr[count++] = (piece_board[MAP_64_TO_36[bb_dst - 8]] << 4) | 3;
			m &= m - 1;
		}
		// West
		m = (my_pieces >> 1) & valid_targets;
		while (m) {
			const int bb_dst = __builtin_ctzll(m);
			move_arr[count++] = (piece_board[MAP_64_TO_36[bb_dst + 1]] << 4) | 1;
			m &= m - 1;
		}
		// East
		m = (my_pieces << 1) & valid_targets;
		while (m) {
			const int bb_dst = __builtin_ctzll(m);
			move_arr[count++] = (piece_board[MAP_64_TO_36[bb_dst - 1]] << 4) | 2;
			m &= m - 1;
		}
		// Escape
		if (userBlue & (1ULL << 9)) move_arr[count++] = (piece_board[0] << 4) | 1;
		if (userBlue & (1ULL << 14)) move_arr[count++] = (piece_board[5] << 4) | 2;

	} else {
		const uint64_t my_pieces = enemyRed | enemyBlue | enemyUnknown;
		const uint64_t valid_targets = VALID_BOARD_MASK & ~my_pieces;

		m = (my_pieces >> 8) & valid_targets;
		while (m) {
			const int bb_dst = __builtin_ctzll(m);
			move_arr[count++] = (piece_board[MAP_64_TO_36[bb_dst + 8]] << 4) | 0;
			m &= m - 1;
		}
		m = (my_pieces << 8) & valid_targets;
		while (m) {
			const int bb_dst = __builtin_ctzll(m);
			move_arr[count++] = (piece_board[MAP_64_TO_36[bb_dst - 8]] << 4) | 3;
			m &= m - 1;
		}
		m = (my_pieces >> 1) & valid_targets;
		while (m) {
			const int bb_dst = __builtin_ctzll(m);
			move_arr[count++] = (piece_board[MAP_64_TO_36[bb_dst + 1]] << 4) | 1;
			m &= m - 1;
		}
		m = (my_pieces << 1) & valid_targets;
		while (m) {
			const int bb_dst = __builtin_ctzll(m);
			move_arr[count++] = (piece_board[MAP_64_TO_36[bb_dst - 1]] << 4) | 2;
			m &= m - 1;
		}

		const uint64_t enemy_all = enemyRed | enemyBlue | enemyUnknown;
		if (enemy_all & (1ULL << 49)) {
			const int p = piece_board[30];
			if (color[p] == -BLUE) move_arr[count++] = (p << 4) | 1;
		}
		if (enemy_all & (1ULL << 54)) {
			const int p = piece_board[35];
			if (color[p] == -BLUE) move_arr[count++] = (p << 4) | 2;
		}
	}

	return count;
}
*/

// ==========================================
// gen_all_move — Array Scan Version (ACTIVE)
// ==========================================
/**
 * @brief Generates all possible legal moves for the current player.
 * * Piece-major iteration matching gst.cpp move ordering.
 */
int GST::gen_all_move(int* move_arr) {
	int count = 0;
	int offset = nowTurn == ENEMY ? PIECES : 0;
	int* nowTurn_pos = pos + offset;

	for (int i = 0; i < PIECES; i++) {
		if (pos[i + offset] != -1) {
			gen_move(move_arr, i + offset, nowTurn_pos[i], count);
		}
	}
	return count;
}

/**
 * @brief Helper: Checks if a move results in an immediate win (Escape).
 */
bool check_win_move(int location, int dir) {
	if (location == 0 || location == 30)
		return dir == 1 ? true : false;
	else if (location == 5 || location == 35)
		return dir == 2 ? true : false;
	return false;
}

/**
 * @brief Executes a move, updates board, handles captures, and checks state.
 */
void GST::do_move(int move) {
	int piece = move >> 4;
	int direction = move & 0xf;

	// Check for Escape Victory
	if (abs(color[piece]) == BLUE) {
		if (check_win_move(pos[piece], direction)) {
			winner = nowTurn;
			n_plies++;
			nowTurn ^= 1;
			is_escape = true;
			return;
		}
	}

	// Safety break for infinite loops
	if (n_plies == MAX_PLIES) {
		fprintf(stderr, "cannot do anymore moves\n");
		exit(1);
	}

	const int src_36 = pos[piece];
	const int dst_36 = src_36 + dir_val[direction];
	const int bb_src = MAP_36_TO_64[src_36];
	const int bb_dst = MAP_36_TO_64[dst_36];
	const uint64_t dst_bit = (1ULL << bb_dst);
	// Pre-compute combined bitboard flip mask (clear src, set dst in one XOR)
	const uint64_t move_bits = (1ULL << bb_src) | (1ULL << bb_dst);

	// Capture class encoding (bits 13~14): 0 EnemyRed, 1 EnemyBlue, 2 EnemyUnknown, 3 User
	int eaten = -1;
	int captured_class = 0;
	const uint64_t user_occ = userRed | userBlue;
	const uint64_t enemy_occ = enemyRed | enemyBlue | enemyUnknown;

	if (nowTurn == USER) {
		if (__builtin_expect(enemy_occ & dst_bit, 0)) {
			eaten = piece_board[dst_36];
			if (enemyRed & dst_bit)
				captured_class = 0;
			else if (enemyBlue & dst_bit)
				captured_class = 1;
			else
				captured_class = 2;
		}
	} else {
		if (__builtin_expect(user_occ & dst_bit, 0)) {
			eaten = piece_board[dst_36];
			captured_class = 3;	 // General User class
		}
	}

	if (__builtin_expect(eaten != -1, 0)) {
		pos[eaten] = -1;
		move |= eaten << 8;
		move |= (captured_class << 13);
		revealed[eaten] = true;

		if (nowTurn == USER) {
			if (captured_class == 0)
				enemyRed ^= dst_bit;
			else if (captured_class == 1)
				enemyBlue ^= dst_bit;
			else
				enemyUnknown ^= dst_bit;
		} else {
			if (userRed & dst_bit) {
				userRed ^= dst_bit;
				move |= 1 << 15;  // Extra bit for UserRed vs UserBlue
			} else {
				userBlue ^= dst_bit;
			}
		}

		const int ec = color[eaten];
		if (ec == -RED)
			piece_nums[2]--;
		else if (ec == -BLUE)
			piece_nums[3]--;
		else if (ec == RED)
			piece_nums[0]--;
		else if (ec == BLUE)
			piece_nums[1]--;
	} else {
		move |= 0x1000;
	}

	// Move the piece — single XOR clears src and sets dst
	if (nowTurn == USER) {
		if (color[piece] == RED)
			userRed ^= move_bits;
		else
			userBlue ^= move_bits;
	} else {
		const int cp = color[piece];
		if (cp == -RED)
			enemyRed ^= move_bits;
		else if (cp == -BLUE)
			enemyBlue ^= move_bits;
		else
			enemyUnknown ^= move_bits;
	}

	board[src_36] = 0;
	piece_board[src_36] = -1;
	board[dst_36] = color[piece];
	piece_board[dst_36] = piece;
	pos[piece] = dst_36;
	history[n_plies++] = move;
	nowTurn ^= 1;
}

/**
 * @brief Undoes the last move (restores board state).
 */
void GST::undo() {
	if (winner != -1) winner = -1;

	if (n_plies == 0) {
		fprintf(stderr, "no history\n");
		exit(1);
	}
	nowTurn ^= 1;  // Switch back to previous player

	int move = history[--n_plies];
	int check_eaten = move >> 12;
	int eaten_piece = (move & 0xfff) >> 8;
	int captured_class = (move >> 13) & 0x3;
	int piece = (move & 0xff) >> 4;
	int direction = move & 0xf;

	int dst_36 = pos[piece];
	int src_36 = pos[piece] - dir_val[direction];

	if (is_escape) {
		is_escape = false;
		return;
	}

	int bb_src = MAP_36_TO_64[src_36];
	int bb_dst = MAP_36_TO_64[dst_36];
	const uint64_t src_bit = (1ULL << bb_src);
	const uint64_t dst_bit = (1ULL << bb_dst);
	const uint64_t move_bits = src_bit | dst_bit;

	if (nowTurn == USER) {
		if (color[piece] == RED)
			userRed ^= move_bits;
		else
			userBlue ^= move_bits;
	} else {
		const int cp = color[piece];
		if (cp == -RED)
			enemyRed ^= move_bits;
		else if (cp == -BLUE)
			enemyBlue ^= move_bits;
		else
			enemyUnknown ^= move_bits;
	}

	// Restore captured piece if any
	if (check_eaten != 0x1) {
		board[dst_36] = color[eaten_piece];
		piece_board[dst_36] = eaten_piece;
		pos[eaten_piece] = dst_36;
		revealed[eaten_piece] = false;

		if (nowTurn == USER) {
			if (captured_class == 0)
				enemyRed |= dst_bit;
			else if (captured_class == 1)
				enemyBlue |= dst_bit;
			else
				enemyUnknown |= dst_bit;
		} else {
			if (move & (1 << 15))
				userRed |= dst_bit;
			else
				userBlue |= dst_bit;
		}

		// Restore piece counts
		if (nowTurn == USER) {
			if (color[eaten_piece] == -RED)
				piece_nums[2]++;
			else if (color[eaten_piece] == -BLUE)
				piece_nums[3]++;
		} else {
			if (color[eaten_piece] == RED)
				piece_nums[0]++;
			else if (color[eaten_piece] == BLUE)
				piece_nums[1]++;
		}
	} else {
		// Just clear current pos
		board[dst_36] = 0;
		piece_board[dst_36] = -1;
	}

	// Move piece back to src
	board[src_36] = color[piece];
	piece_board[src_36] = piece;
	pos[piece] = src_36;
}

/**
 * @brief Checks if the game is over.
 */
bool GST::is_over() {
	if (n_plies >= 200) {
		winner = -2;  // Draw (Rule: 200 plies limit)
		return true;
	}
	if (winner != -1)
		return true;
	else {
		// Keep original precedence but use bitboards for fast local color exhaustion checks.
		if (userRed == 0ULL || piece_nums[3] == 0) {
			winner = USER;
			return true;
		} else if (userBlue == 0ULL || piece_nums[2] == 0) {
			winner = ENEMY;
			return true;
		}
	}
	return false;
}

// ==========================================
// Statistics & Utilities
// ==========================================

struct GameStats {
	int total_games;
	// ISMCTS (Player 1) Stats
	int ismcts_wins;
	int ismcts_escape;
	int ismcts_enemy_red;
	int ismcts_enemy_blue;
	int ismcts_total_steps;
	double ismcts_total_times;
	// MCTS (Player 2) Stats
	int mcts_wins;
	int mcts_escape;
	int mcts_enemy_red;
	int mcts_enemy_blue;
	// Draws
	int draws;

	GameStats()
		: total_games(0),
		  mcts_wins(0),
		  mcts_escape(0),
		  mcts_enemy_red(0),
		  mcts_enemy_blue(0),
		  ismcts_total_steps(0),
		  ismcts_total_times(0.0),
		  ismcts_wins(0),
		  ismcts_escape(0),
		  ismcts_enemy_red(0),
		  ismcts_enemy_blue(0),
		  draws(0) {}
};

void print_game_stats(const GameStats& stats) {
	std::cout << "\n===== 統計結果 =====\n";
	std::cout << "總場次: " << stats.total_games << "\n\n";

	std::cout << "ISMCTS 獲勝: " << stats.ismcts_wins << " 場\n";
	std::cout << "  - 藍子逃脫: " << stats.ismcts_escape << " 場\n";
	std::cout << "  - 紅子被吃光: " << stats.ismcts_enemy_red << " 場\n";
	std::cout << "  - 吃光對手藍子: " << stats.ismcts_enemy_blue << " 場\n\n";
	std::cout << "ISMCTS 總思考步數: " << stats.ismcts_total_steps << "\n";
	std::cout << "ISMCTS 總思考時間: " << stats.ismcts_total_times << " ms\n";
	std::cout << "ISMCTS 平均思考時間: " << (stats.ismcts_total_times / stats.ismcts_total_steps)
			  << " ms\n";

	std::cout << "MCTS 獲勝: " << stats.mcts_wins << " 場\n";
	std::cout << "  - 藍子逃脫: " << stats.mcts_escape << " 場\n";
	std::cout << "  - 紅子被吃光: " << stats.mcts_enemy_red << " 場\n";
	std::cout << "  - 吃光對手藍子: " << stats.mcts_enemy_blue << " 場\n\n";

	std::cout << "平局: " << stats.draws << " 場\n";
}

void print_progress_bar(int current, int total) {
	const int bar_width = 50;
	float progress = (float)current / total;
	int pos = bar_width * progress;

	std::cout << "\r[";
	for (int i = 0; i < bar_width; ++i) {
		if (i < pos)
			std::cout << "=";
		else if (i == pos)
			std::cout << ">";
		else
			std::cout << " ";
	}
	std::cout << "] " << int(progress * 100.0) << "% (" << current << "/" << total << ")"
			  << std::flush;
}

// ==========================================
// N-Tuple Heuristic Implementation
// ==========================================

/**
 * @brief Checks if a pattern is valid within board boundaries.
 */
bool GST::is_valid_pattern(int base_pos, const int* offset) {
	int base_row = base_pos / COL;
	int base_col = base_pos % COL;

	if (offset == offset_1x4) {
		if (base_col > 2) return false;
	} else if (offset == offset_2x2) {
		if (base_col > 4 || base_row > 4) return false;
	} else if (offset == offset_4x1) {
		if (base_row > 2) return false;
	}

	return true;
}

/**
 * @brief Encodes the location of a pattern.
 */
int GST::get_loc(int base_pos, const int* offset) {
	int position[4];
	for (int i = 0; i < 4; i++) {
		position[i] = base_pos + offset[i];
	}
	return (position[0] * 36 * 36 * 36 + position[1] * 36 * 36 + position[2] * 36 + position[3]);
}

/**
 * @brief Extracts feature encoding from the board.
 * * Uses feature_cache for optimization.
 */
int GST::get_feature_unknown(int base_pos, const int* offset, const int* feature_cache) {
	int features[4];
	for (int i = 0; i < 4; i++) {
		int pos = base_pos + offset[i];

		features[i] = feature_cache[pos];
	}
	return (features[0] * 64 + features[1] * 16 + features[2] * 4 + features[3]);
}

/**
 * @brief Retrieves heuristic weight for a specific pattern.
 */
float GST::get_weight(int base_pos, const int* offset, DATA& d, const int* feature_cache) {
	// Pass feature_cache down to feature extraction
	int feature = get_feature_unknown(base_pos, offset, feature_cache);

	int LUTidx = d.LUT_idx(d.trans[get_loc(base_pos, offset)], feature);
	float weight = 0;

	// LUT selection based on remaining pieces
	if (nowTurn == USER) {
		if (piece_nums[2] == 1) {  // Enemy Red = 1
			weight = d.LUTwr_U_R1[LUTidx];
		} else if (piece_nums[1] == 1) {  // User Blue = 1
			weight = d.LUTwr_U_B1[LUTidx];
		} else {
			weight = d.LUTwr_U[LUTidx];
		}
	} else {
		if (piece_nums[0] == 1) {  // User Red = 1
			weight = d.LUTwr_E_R1[LUTidx];
		} else if (piece_nums[3] == 1) {  // Enemy Blue = 1
			weight = d.LUTwr_E_B1[LUTidx];
		} else {
			weight = d.LUTwr_E[LUTidx];
		}
	}

	return weight;
}

/**
 * @brief Computes the aggregated weight of the entire board.
 * * Matches gst.cpp semantics: board[] iteration, piece_nums[] for LUT, no proximity bonus.
 * * Optimizations: LUT hoisted once (was re-checked 61× per call), inlined feature extraction.
 * * CRITICAL: Single pos loop (0→35) preserved for FP accumulation order parity.
 */
float GST::compute_board_weight(DATA& d) {
	float total_weight = 0;

	// 1. Create a fast L1 cache on stack
	int feature_cache[ROW * COL];

	// 2. Iterate board once to fill cache (identical to gst.cpp)
	if (nowTurn == USER) {
		for (int pos = 0; pos < ROW * COL; pos++) {
			feature_cache[pos] = (board[pos] < 0) ? 3 : board[pos];
		}
	} else {
		for (int pos = 0; pos < ROW * COL; pos++) {
			feature_cache[pos] = (board[pos] > 0) ? 3 : -board[pos];
		}
	}

	// 3. Hoist LUT selection — same logic as get_weight but done ONCE
	const float* lut;
	if (nowTurn == USER) {
		if (piece_nums[2] == 1)
			lut = d.LUTwr_U_R1;
		else if (piece_nums[1] == 1)
			lut = d.LUTwr_U_B1;
		else
			lut = d.LUTwr_U;
	} else {
		if (piece_nums[0] == 1)
			lut = d.LUTwr_E_R1;
		else if (piece_nums[3] == 1)
			lut = d.LUTwr_E_B1;
		else
			lut = d.LUTwr_E;
	}

	// 4. Single pos loop — SAME accumulation order as gst.cpp (1x4 → 4x1 → 2x2 per pos)
	for (int pos = 0; pos < ROW * COL; pos++) {
		const int row = pos / COL;
		const int col = pos % COL;

		// offset_1x4: {0,1,2,3} — valid when col <= 2
		if (col <= 2) {
			const int f = feature_cache[pos] * 64 + feature_cache[pos + 1] * 16 +
						  feature_cache[pos + 2] * 4 + feature_cache[pos + 3];
			total_weight += lut[d.LUT_idx(d.trans[get_loc(pos, offset_1x4)], f)];
		}

		// offset_4x1: {0,6,12,18} — valid when row <= 2
		if (row <= 2) {
			const int f = feature_cache[pos] * 64 + feature_cache[pos + 6] * 16 +
						  feature_cache[pos + 12] * 4 + feature_cache[pos + 18];
			total_weight += lut[d.LUT_idx(d.trans[get_loc(pos, offset_4x1)], f)];
		}

		// offset_2x2: {0,1,6,7} — valid when col <= 4 AND row <= 4
		if (col <= 4 && row <= 4) {
			const int f = feature_cache[pos] * 64 + feature_cache[pos + 1] * 16 +
						  feature_cache[pos + 6] * 4 + feature_cache[pos + 7];
			total_weight += lut[d.LUT_idx(d.trans[get_loc(pos, offset_2x2)], f)];
		}
	}

	return total_weight / (float)TUPLE_NUM;
}

/**
 * @brief Selects the highest weighted move (Greedy Policy).
 * * Includes optimizations for corner bonuses and pre-computation.
 */
int GST::highest_weight(DATA& d) {
	float WEIGHT[MAX_MOVES] = {0};
	int root_nmove;
	int root_moves[MAX_MOVES];
	root_nmove = gen_all_move(root_moves);

	// Store distances from pieces to corners
	std::vector<std::tuple<int, int, int>> pieces_distances;  // (piece_idx, corner_id, distance)

	if (nowTurn == USER) {
		// Calculate for all User pieces
		for (int i = 0; i < PIECES; i++) {
			if (pos[i] != -1) {
				int p_row = pos[i] / 6;
				int p_col = pos[i] % 6;

				// Manhattan distance to 4 corners
				int dist_to_0 = p_row + p_col;
				int dist_to_5 = p_row + (5 - p_col);
				int dist_to_30 = (5 - p_row) + p_col;
				int dist_to_35 = (5 - p_row) + (5 - p_col);

				// Add to list
				pieces_distances.push_back(std::make_tuple(i, 0, dist_to_0));
				pieces_distances.push_back(std::make_tuple(i, 1, dist_to_5));
				pieces_distances.push_back(std::make_tuple(i, 2, dist_to_30));
				pieces_distances.push_back(std::make_tuple(i, 3, dist_to_35));
			}
		}
	} else if (nowTurn == ENEMY) {
		// Calculate for all Enemy pieces
		for (int i = PIECES; i < PIECES * 2; i++) {
			if (pos[i] != -1) {
				int p_row = pos[i] / 6;
				int p_col = pos[i] % 6;

				int dist_to_0 = p_row + p_col;
				int dist_to_5 = p_row + (5 - p_col);
				int dist_to_30 = (5 - p_row) + p_col;
				int dist_to_35 = (5 - p_row) + (5 - p_col);

				pieces_distances.push_back(std::make_tuple(i, 0, dist_to_0));
				pieces_distances.push_back(std::make_tuple(i, 1, dist_to_5));
				pieces_distances.push_back(std::make_tuple(i, 2, dist_to_30));
				pieces_distances.push_back(std::make_tuple(i, 3, dist_to_35));
			}
		}
	}

	// Sort all piece-corner tuples by distance
	std::sort(pieces_distances.begin(), pieces_distances.end(),
			  [](const std::tuple<int, int, int>& a, const std::tuple<int, int, int>& b) {
				  return std::get<2>(a) < std::get<2>(b);
			  });

	// Tracking assigned pieces and corners
	bool piece_assigned[PIECES * 2];
	bool corner_assigned[4];
	int assigned_corner_for_piece[PIECES * 2];	// Map: [PieceID] -> AssignedCornerID

	memset(piece_assigned, false, sizeof(piece_assigned));
	memset(corner_assigned, false, sizeof(corner_assigned));
	memset(assigned_corner_for_piece, -1, sizeof(assigned_corner_for_piece));

	// Assign closest pieces to corners
	for (const auto& tuple : pieces_distances) {
		int p_idx = std::get<0>(tuple);
		int corner = std::get<1>(tuple);

		if (!piece_assigned[p_idx] && !corner_assigned[corner]) {
			piece_assigned[p_idx] = true;
			corner_assigned[corner] = true;
			assigned_corner_for_piece[p_idx] = corner;	// Memorize task
		}

		if (corner_assigned[0] && corner_assigned[1] && corner_assigned[2] && corner_assigned[3]) {
			break;
		}
	}

	for (int m = 0; m < root_nmove; m++) {
		int move_index = m;
		int piece = root_moves[m] >> 4;
		int direction = root_moves[m] & 0xf;
		int src = pos[piece];
		int dst = src + dir_val[direction];	 // the position after move

		// Immediate win checks or special heuristics
		if (pos[piece] == 0 && direction == 1 && nowTurn == USER && board[0] == BLUE) {
			WEIGHT[move_index] = 1;
		} else if (pos[piece] == 5 && direction == 2 && nowTurn == USER && board[5] == BLUE) {
			WEIGHT[move_index] = 1;
		} else if (pos[piece] == 30 && direction == 1 && nowTurn == ENEMY && board[30] == -BLUE) {
			WEIGHT[move_index] = 1;
		} else if (pos[piece] == 35 && direction == 2 && nowTurn == ENEMY && board[35] == -BLUE) {
			WEIGHT[move_index] = 1;
		} else if (pos[piece] == 4 && direction == 2 && nowTurn == USER && color[piece] == BLUE) {
			if (board[5] == 0 && board[11] >= 0) {
				WEIGHT[move_index] = 1;
			}
		} else if (pos[piece] == 1 && direction == 1 && nowTurn == USER && color[piece] == BLUE) {
			if (board[0] == 0 && board[6] >= 0) {
				WEIGHT[move_index] = 1;
			}
		} else if (pos[piece] == 34 && direction == 2 && nowTurn == ENEMY &&
				   color[piece] == -BLUE) {
			if (board[35] == 0 && board[29] <= 0) {
				WEIGHT[move_index] = 1;
			}
		} else if (pos[piece] == 31 && direction == 1 && nowTurn == ENEMY &&
				   color[piece] == -BLUE) {
			if (board[30] == 0 && board[24] <= 0) {
				WEIGHT[move_index] = 1;
			}
		} else {
			// General case: Simulate move and evaluate board
			int tmp_color[PIECES * 2];	// Backup colors (Remove God View)
			for (int i = 0; i < PIECES * 2; i++) tmp_color[i] = color[i];

			// Mask hidden info
			if (nowTurn == USER)
				for (int i = PIECES; i < PIECES * 2; i++) color[i] = -UNKNOWN;
			else
				for (int i = 0; i < PIECES; i++) color[i] = UNKNOWN;

			do_move(root_moves[m]);
			nowTurn ^= 1;

			WEIGHT[move_index] = compute_board_weight(d);

			nowTurn ^= 1;
			undo();

			// Restore colors
			for (int i = 0; i < PIECES * 2; i++) color[i] = tmp_color[i];
		}

		// Apply Corner Heuristics
		int row = dst / 6;
		int col = dst % 6;

		int d0 = row + col;
		int d5 = row + (5 - col);
		int d30 = (5 - row) + col;
		int d35 = (5 - row) + (5 - col);

		float corner_bonus = 1.0;

		// Check if current piece has an assigned corner task
		// Uses pre-calculated 'assigned_corner_for_piece'
		if (assigned_corner_for_piece[piece] != -1) {
			int assigned_corner = assigned_corner_for_piece[piece];
			int current_dist;

			int p_row = src / 6;
			int p_col = src % 6;

			if (assigned_corner == 0) {
				current_dist = p_row + p_col;
				if (d0 < current_dist) {  // Moving closer to corner 0
					corner_bonus = 1.01;
				}
			} else if (assigned_corner == 1) {
				current_dist = p_row + (5 - p_col);
				if (d5 < current_dist) {  // Moving closer to corner 5
					corner_bonus = 1.01;
				}
			} else if (assigned_corner == 2) {
				current_dist = (5 - p_row) + p_col;
				if (d30 < current_dist) {  // Moving closer to corner 30
					corner_bonus = 1.01;
				}
			} else if (assigned_corner == 3) {
				current_dist = (5 - p_row) + (5 - p_col);
				if (d35 < current_dist) {  // Moving closer to corner 35
					corner_bonus = 1.01;
				}
			}
		}

		WEIGHT[move_index] *= corner_bonus;

		if (piece_nums[2] <= 1 && board[dst] == 0) {
			WEIGHT[move_index] *= 1.01;
		}
	}

	// Final Selection Logic (Softmax / Linear / Argmax)
	float max_weight = -std::numeric_limits<float>::infinity();
	float min_weight = std::numeric_limits<float>::infinity();
	std::vector<int> best_candidates;
	best_candidates.reserve(root_nmove);

	for (int i = 0; i < root_nmove; ++i) {
		const float wi = WEIGHT[i];
		if (!(wi == wi)) continue;	// Skip NaN
		if (wi > max_weight) {
			max_weight = wi;
			best_candidates.clear();
			best_candidates.push_back(i);
		} else if (wi == max_weight) {
			best_candidates.push_back(i);
		}
		if (wi < min_weight) {
			min_weight = wi;
		}
	}

	int best_idx = -1;
	if (!best_candidates.empty()) {
		best_idx = best_candidates[rng(best_candidates.size())];
	}
	if (best_idx < 0) {
		best_idx = 0;  // Fallback
		max_weight = 0.0f;
		min_weight = 0.0f;
	}

	int chosen_idx = best_idx;	// Default to argmax

#if SELECTION_MODE == 2
	// Softmax Probability Sampling
	const double temperature = 1.0;
	const double T = std::max(1e-9, temperature);
	std::vector<double> probs(root_nmove, 0.0);
	double sumProb = 0.0;
	for (int i = 0; i < root_nmove; i++) {
		double wi = static_cast<double>(WEIGHT[i]);
		if (!(wi == wi)) {	// NaN -> 0
			probs[i] = 0.0;
			continue;
		}
		double v = std::exp((wi - static_cast<double>(max_weight)) / T);
		if (!std::isfinite(v)) v = 0.0;
		probs[i] = v;
		sumProb += v;
	}
	if (sumProb > 0.0 && std::isfinite(sumProb)) {
		double u = next_u01();
		double target = u * sumProb;
		double acc = 0.0;
		for (int i = 0; i < root_nmove; i++) {
			acc += probs[i];
			if (target < acc) {
				chosen_idx = i;
				break;
			}
		}
		if (chosen_idx < 0) chosen_idx = best_idx;
	}
#elif SELECTION_MODE == 1
	// Linear Weight Sampling (Shift negative values)
	const double shift = (min_weight < 0.0f) ? -static_cast<double>(min_weight) : 0.0;
	std::vector<double> w(root_nmove, 0.0);
	double sumW = 0.0;
	for (int i = 0; i < root_nmove; i++) {
		double wi = static_cast<double>(WEIGHT[i]);
		if (!(wi == wi)) {	// NaN -> 0
			w[i] = 0.0;
			continue;
		}
		double vi = wi + shift;
		if (vi < 0.0) vi = 0.0;
		w[i] = vi;
		sumW += vi;
	}
	if (sumW > 0.0 && std::isfinite(sumW)) {
		double u = next_u01();
		double target = u * sumW;
		double acc = 0.0;
		for (int i = 0; i < root_nmove; i++) {
			acc += w[i];
			if (target < acc) {
				chosen_idx = i;
				break;
			}
		}
		if (chosen_idx < 0) chosen_idx = best_idx;
	}
#else
	// Argmax (Already calculated in best_idx)
#endif

	// Final safety check
	if (chosen_idx < 0 || chosen_idx >= root_nmove) chosen_idx = best_idx;
	return root_moves[chosen_idx];
}

// ==========================================
// Main Application Entry
// ==========================================

DATA data;

int main() {
	std::random_device rd;
	std::uniform_int_distribution<> dist(0, 7);

	int num_games;
	std::cout << "請輸入要進行的遊戲場數: ";
	std::cin >> num_games;

	data.init_data();
	data.read_data_file(500000);
	GameStats stats;
	stats.total_games = num_games;

	if (num_games > 1) {
		std::cout << "\n開始進行多場遊戲模擬...\n";
	}

	for (int game_num = 1; game_num <= num_games; game_num++) {
		GST game;
		MCTS mcts(5000);
		ISMCTS ismcts(5000);
		bool my_turn = true;

		// Reset all states
		game.init_board();
		mcts.reset();
		ismcts.reset();

		if (num_games == 1) {
			std::cout << "\n===== 遊戲開始 =====\n\n";
			game.print_board();
		} else {
			print_progress_bar(game_num - 1, num_games);
		}

		// Main Game Loop
		while (!game.is_over()) {
			if (my_turn) {
				if (num_games == 1) std::cout << "Player 1 (ISMCTS) 思考中...\n";
				stats.ismcts_total_steps++;
				auto start = std::chrono::steady_clock::now();

				int move = ismcts.findBestMove(game, data);

				auto end = std::chrono::steady_clock::now();
				stats.ismcts_total_times +=
					std::chrono::duration<double, std::milli>(end - start).count();
				if (move == -1) break;
				game.do_move(move);
			} else {
				if (num_games == 1) std::cout << "Player 2 (MCTS) 思考中...\n";
				int move = mcts.findBestMove(game);
				if (move == -1) break;
				game.do_move(move);
			}

			if (num_games == 1) {
				game.print_board();
				std::cout << "當前回合數: " << game.n_plies << std::endl;
			}

			my_turn = !my_turn;
		}

		// Update statistics based on result
		int winner = game.get_winner();
		if (winner == -2) {
			stats.draws++;
		} else if (winner == USER) {
			stats.ismcts_wins++;
			if (game.is_escape) {
				stats.ismcts_escape++;
			} else if (game.piece_nums[0] == 0) {
				stats.ismcts_enemy_red++;
			} else if (game.piece_nums[3] == 0) {
				stats.ismcts_enemy_blue++;
			}
		} else if (winner == ENEMY) {
			stats.mcts_wins++;
			if (game.is_escape) {
				stats.mcts_escape++;
			} else if (game.piece_nums[2] == 0) {
				stats.mcts_enemy_red++;
			} else if (game.piece_nums[1] == 0) {
				stats.mcts_enemy_blue++;
			}
		}

		// Only show detailed output for single game
		if (num_games == 1) {
			if (winner == -2) {
				printf("遊戲結束！達到200回合，判定為平局！\n");
			} else {
				printf("遊戲結束！%s 獲勝！\n", winner ? "Player 2 (MCTS)" : "Player 1 (ISMCTS)");
				if (game.is_escape) {
					printf("勝利方式：藍色棋子成功逃脫！\n");
				} else if (game.piece_nums[0] == 0) {
					printf("勝利方式：Player 1 的紅色棋子全部被吃光！\n");
				} else if (game.piece_nums[2] == 0) {
					printf("勝利方式：Player 2 的紅色棋子全部被吃光！\n");
				} else if (game.piece_nums[1] == 0) {
					printf("勝利方式：Player 1 的藍色棋子全部被吃光！\n");
				} else if (game.piece_nums[3] == 0) {
					printf("勝利方式：Player 2 的藍色棋子全部被吃光！\n");
				}
			}
		}
	}

	// Complete the progress bar and print final stats
	if (num_games > 1) {
		print_progress_bar(num_games, num_games);
		std::cout << "\n\n";
		print_game_stats(stats);
	}

	return 0;
}