/**
 * @file 4T_GST_impl.cpp
 * @brief Implementation of Game State (GST) logic.
 * * Contains board logic, move generation, and N-Tuple heuristics.
 * @author Original Project Team (Inherited Code)
 * @author Chen You-Kai (Optimization & Docs)
 */

#include "4T_DATA.hpp"
#include "4T_GST.hpp"
#include "4T_header.h"

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
static thread_local pcg32 rng(std::random_device{}());

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

// ==========================================
// Terminal Utilities
// ==========================================

/**
 * @brief Sets terminal text color (Windows/ANSI).
 */
void SetColor(int color = 7) {
#ifdef _WIN32
	HANDLE hConsole;
	hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
	SetConsoleTextAttribute(hConsole, color);
#else
	// For non-Windows platforms, use ANSI escape codes
	switch (color) {
		case 4:
			printf("\033[31m");
			break;	// RED
		case 9:
			printf("\033[34m");
			break;	// BLUE
		default:
			printf("\033[0m");
			break;	// Reset to default
	}
#endif
}

// ==========================================
// GST Implementation
// ==========================================

/**
 * @brief Server Utility: Sets the board state from a string.
 * @param position String representation of the board state.
 */
void GST::set_board(char* position) {
	memset(board, 0, sizeof(board));
	memset(pos, 0, sizeof(pos));
	memset(revealed, false, sizeof(revealed));
	for (int i = 0; i < ROW * COL; i++) piece_board[i] = -1;
	for (int i = 0; i < 4; i++) piece_nums[i] = 4;

	nowTurn = USER;
	winner = -1;

	// Server message format: MOV?10B24B34B99b15R25R35R99r45u31u21u99r40u30u20u99b
	for (int i = 0; i < PIECES * 2; i++) {
		int index = i * 3;
		char x = position[index];
		char y = position[index + 1];
		char c = position[index + 2];

		if (x == '9' && y == '9') {
			pos[i] = -1;  // Eaten
			revealed[i] = true;
			if (i < PIECES) {
				if (c == 'r') {
					color[i] = RED;
					piece_nums[0]--;
				} else if (c == 'b') {
					color[i] = BLUE;
					piece_nums[1]--;
				}
			} else {
				if (c == 'r') {
					color[i] = -RED;
					piece_nums[2]--;
				} else if (c == 'b') {
					color[i] = -BLUE;
					piece_nums[3]--;
				}
			}
		} else {
			int pos_val = (x - '0') + (y - '0') * COL;
			pos[i] = pos_val;

			if (i < PIECES) {  // User
				if (c == 'R')
					color[i] = RED;
				else if (c == 'B')
					color[i] = BLUE;
				revealed[i] = true;
			} else {								// Enemy
				if (c == 'u') color[i] = -UNKNOWN;	// Unknown piece
				revealed[i] = false;
			}

			// Update board
			board[pos_val] = color[i];
			piece_board[pos_val] = i;
		}
	}

	print_board();

	return;
}

/**
 * @brief Initializes the board and randomly assigns Red pieces.
 */
void GST::init_board() {
	auto now = std::chrono::system_clock::now();
	auto now_as_duration = now.time_since_epoch();
	auto now_as_microseconds =
		std::chrono::duration_cast<std::chrono::microseconds>(now_as_duration).count();
	pcg32 rng(now_as_microseconds);

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
	}

	// Set pieces on the board
	int offset = 0;
	for (int player = 0; player < 2; player++) {
		for (int i = 0; i < PIECES; i++) {
			// board: records color type (1, 2, -1, -2)
			board[init_pos[player][i]] = color[i + offset];
			// piece_board: records piece ID (0~15)
			piece_board[init_pos[player][i]] = i + offset;
			// pos: records location index (0~35)
			pos[i + offset] = init_pos[player][i];
		}
		offset += 8;
	}

	// for (int i = 0; i < 201; i++) {
	//     for (int j = 0; j < ROW * COL + 1; j++) {
	//         color_his_U[i][j] = 0;
	//         color_his_E[i][j] = 0;
	//     }
	// }
	step = 0;

	return;
}

/**
 * @brief Prints the board, remaining pieces, and captured pieces to console.
 */
void GST::print_board() {
	printf("step = %d\n", step - 1);
	for (int i = 0; i < ROW * COL; i++) {
		if (piece_board[i] != -1) {
			if (abs(color[piece_board[i]]) == RED)
				SetColor(4);
			else if (abs(color[piece_board[i]]) == BLUE)
				SetColor(9);
			printf("%4c", print_piece[piece_board[i]]);
			SetColor();
		} else if (i == 0 || i == 30)
			printf("%4c", '<');
		else if (i == 5 || i == 35)
			printf("%4c", '>');
		else
			printf("%4c", '-');
		if (i % 6 == 5) printf("\n");
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

	std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
}

/**
 * @brief Generates moves for a specific piece.
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

/**
 * @brief Generates all possible legal moves for the current player.
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

	// dst: the chess's location after move / pos: the location of chess / dir_val: up down left
	// right
	int dst = pos[piece] + dir_val[direction];

	// Handle Captures
	if (board[dst] < 0) {				// Occupied by Enemy
		pos[piece_board[dst]] = -1;		// Piece eaten
		move |= piece_board[dst] << 8;	// Record eaten piece in move (for undo)
		revealed[piece_board[dst]] = true;

		// Update piece counts
		if (color[piece_board[dst]] == -RED)
			piece_nums[2] -= 1;
		else if (color[piece_board[dst]] == -BLUE)
			piece_nums[3] -= 1;
		else if (color[piece_board[dst]] == -UNKNOWN) {
			// Do nothing for unknown
		} else {
			fprintf(stderr, "piece: %d, direction: %d\n", piece, direction);
			fprintf(stderr, "pos[piece]: %d, dir_val[direction]: %d\n", pos[piece],
					dir_val[direction]);
			fprintf(stderr, "color[piece_board[dst]]: %d, piece_board[dst]: %d\n",
					color[piece_board[dst]], piece_board[dst]);
			fprintf(stderr, "do_move error, eaten color wrong!\n");
			exit(1);
		}
	} else if (board[dst] > 0) {  // User's color
		pos[piece_board[dst]] = -1;
		move |= piece_board[dst] << 8;
		revealed[piece_board[dst]] = true;
		if (color[piece_board[dst]] == RED)
			piece_nums[0] -= 1;
		else if (color[piece_board[dst]] == BLUE)
			piece_nums[1] -= 1;
		else if (color[piece_board[dst]] == UNKNOWN) {
			// Do nothing
		} else {
			fprintf(stderr, "piece: %d, direction: %d\n", piece, direction);
			fprintf(stderr, "pos[piece]: %d, dir_val[direction]: %d\n", pos[piece],
					dir_val[direction]);
			fprintf(stderr, "color[piece_board[dst]]: %d, piece_board[dst]: %d\n",
					color[piece_board[dst]], piece_board[dst]);
			fprintf(stderr, "do_move error, eaten color wrong!\n");
			exit(1);
		}
	} else {
		// No capture, mark as empty move
		move |= 0x1000;
	}

	// Update Board State
	board[pos[piece]] = 0;		   // set 0 at the location which stay before => space: color = 0
	piece_board[pos[piece]] = -1;  // set 0 at the location which stay before => space: no chess
	board[dst] = color[piece];	   // color the chess color at the location after move
	piece_board[dst] = piece;	   // set chess number at the location after move
	pos[piece] = dst;			   // the location of chess now
	history[n_plies++] = move;
	nowTurn ^= 1;  // change player
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
	int piece = (move & 0xff) >> 4;
	int direction = move & 0xf;
	int src = pos[piece] - dir_val[direction];	// Original location

	if (is_escape) {
		is_escape = false;
		return;
	}

	// Restore captured piece if any
	if (check_eaten != 0x1) {
		board[pos[piece]] = color[eaten_piece];
		piece_board[pos[piece]] = eaten_piece;
		pos[eaten_piece] = pos[piece];

		// Restore piece counts
		if (nowTurn == USER) {
			if (color[eaten_piece] == -RED)
				piece_nums[2] += 1;
			else if (color[eaten_piece] == -BLUE)
				piece_nums[3] += 1;
			else if (color[eaten_piece] == -UNKNOWN) {
			}  // Do nothing
			else {
				fprintf(stderr, "undo error, eaten color wrong!");
				exit(1);
			}
		} else {
			if (color[eaten_piece] == RED)
				piece_nums[0] += 1;
			else if (color[eaten_piece] == BLUE)
				piece_nums[1] += 1;
			else if (color[eaten_piece] == UNKNOWN) {
			}  // Do nothing
			else {
				fprintf(stderr, "undo error, eaten color wrong!");
				exit(1);
			}
		}
	} else {
		// Just clear current pos
		board[pos[piece]] = 0;
		piece_board[pos[piece]] = -1;
	}

	// Move piece back to src
	board[src] = color[piece];
	piece_board[src] = piece;
	pos[piece] = src;
}

/**
 * @brief Checks if the game has ended.
 */
bool GST::is_over() {
	if (n_plies >= 200) {
		winner = -2;  // Draw (Rule: 200 plies limit)
		return true;
	}
	if (winner != -1)
		return true;
	else {
		// Victory Condition: Eliminate all opponent's pieces of a specific color
		if (piece_nums[0] == 0 || piece_nums[3] == 0) {
			winner = USER;
			return true;
		} else if (piece_nums[1] == 0 || piece_nums[2] == 0) {
			winner = ENEMY;
			return true;
		}
	}
	return false;
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
 */
float GST::compute_board_weight(DATA& d) {
	float total_weight = 0;

	// 1. Create a fast L1 cache on stack
	int feature_cache[ROW * COL];

	// 2. Iterate board once to fill cache
	if (nowTurn == USER) {
		for (int pos = 0; pos < ROW * COL; pos++) {
			feature_cache[pos] = (board[pos] < 0) ? 3 : board[pos];
		}
	} else {  // nowTurn == ENEMY
		for (int pos = 0; pos < ROW * COL; pos++) {
			feature_cache[pos] = (board[pos] > 0) ? 3 : -board[pos];
		}
	}

	for (int pos = 0; pos < ROW * COL; pos++) {
		if (is_valid_pattern(pos, offset_1x4)) {
			total_weight += get_weight(pos, offset_1x4, d, feature_cache);
		}
		if (is_valid_pattern(pos, offset_4x1)) {
			total_weight += get_weight(pos, offset_4x1, d, feature_cache);
		}
		if (is_valid_pattern(pos, offset_2x2)) {
			total_weight += get_weight(pos, offset_2x2, d, feature_cache);
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