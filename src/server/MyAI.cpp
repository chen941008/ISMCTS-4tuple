/**
 * @file MyAI.cpp
 * @brief Implementation of the AI Server Interface.
 * * Handles protocol parsing, board initialization, random red piece selection,
 * * and move generation using the MCTS/ISMCTS engine.
 * @author Original Project Team (Inherited Code)
 * @author Chen You-Kai (Optimization & Docs)
 */

#include "MyAI.h"

#include "../4T_header.h"
#include "../ismcts.hpp"

// Global instances for AI logic
DATA data;
GST game;
ISMCTS ismcts(10000);  // Initialize ISMCTS with 10,000 simulations

// =============================
// Constructor & Destructor
// =============================

/**
 * @brief Construct a new MyAI object.
 * * Initializes N-Tuple data and loads trained weights.
 */
MyAI::MyAI(void) {
	data.init_data();			  // Initialize N-Tuple structures
	data.read_data_file(500000);  // Load pre-trained weights
}

MyAI::~MyAI(void) {}

// =============================
// Protocol Command: INI
// =============================

/**
 * @brief Handles the 'ini' command: Sets up player ID and initial board.
 * @param data Command arguments from server.
 * @param response Buffer to store the initial board configuration for P2.
 */
void MyAI::Ini(const char* data[], char* response) {
	// Parse Player ID (1 = User/First, 2 = Enemy/Second)
	if (!strcmp(data[2], "1")) {
		player = USER;
	} else if (!strcmp(data[2], "2")) {
		player = ENEMY;
	}

	char position[16];
	Init_board_state(position);	 // Generate initial piece layout

	// Format response: P2's piece positions (Protocol requirement)
	snprintf(response, 50, "%c%c %c%c %c%c %c%c %c%c %c%c %c%c %c%c", position[0], position[1],
			 position[2], position[3], position[4], position[5], position[6], position[7],
			 position[8], position[9], position[10], position[11], position[12], position[13],
			 position[14], position[15]);
}

// =============================
// Protocol Command: SET
// =============================

// High-resolution clock for random seeding
auto now = std::chrono::high_resolution_clock::now();
auto nanos = std::chrono::duration_cast<std::chrono::nanoseconds>(now.time_since_epoch()).count();

/**
 * @brief Handles the 'set' command: Randomly selects 4 Red pieces.
 * @param response Buffer to return the selected Red pieces.
 */
void MyAI::Set(char* response) {
	std::mt19937 generator(nanos);
	std::string pieces = "ABCDEFGH";

	// Shuffle pieces to pick 4 random ones as RED
	std::shuffle(pieces.begin(), pieces.end(), generator);

	std::string redString = pieces.substr(0, 4);

	snprintf(response, 50, "SET:%s\r\n", redString.c_str());

	// Debug fixed set (Optional)
	// snprintf(response, 50, "SET:ABDH");
}

// =============================
// Protocol Command: GET
// =============================

/**
 * @brief Handles the 'get' command: Updates board and requests a move.
 * @param data Command arguments (Current board string).
 * @param response Buffer to return the calculated move.
 */
void MyAI::Get(const char* data[], char* response) {
	// Parse board string from server message
	char position[49];	// 3 chars per piece * 16 pieces + 1 null terminator
	position[0] = '\0';
	snprintf(position, sizeof(position), "%s", data[0] + 4);  // Skip "MOV?" prefix

	Set_board(position);  // Sync internal board state

	// Generate best move using AI
	char move[50];
	Generate_move(move);

	// Format response
	snprintf(response, 50, "MOV:%s", move);
}

// =============================
// Protocol Command: EXIT
// =============================

/**
 * @brief Handles the 'exit' command: Cleanup and log.
 */
void MyAI::Exit(const char* data[], char* response) { fprintf(stderr, "Bye~\n"); }

// *********************** AI Internal Logic *********************** //

// =============================
// Board Initialization Logic
// =============================

/**
 * @brief Generates the initial layout of pieces for both players.
 * * Uses standard 4T dark chess starting positions.
 */
void MyAI::Init_board_state(char* position) {
	int order[PIECES] = {0, 1, 2, 3, 4, 5, 6, 7};  // Indices A~H

	// Hardcoded initial positions (Coordinate format)
	std::string p2_init_position = "4131211140302010";
	std::string p1_init_position = "1424344415253545";

	for (int i = 0; i < PIECES; i++) {
		if (player == USER) {
			position[order[i] * 2] = p1_init_position[i * 2];
			position[order[i] * 2 + 1] = p1_init_position[i * 2 + 1];
		} else if (player == ENEMY) {
			position[order[i] * 2] = p2_init_position[i * 2];
			position[order[i] * 2 + 1] = p2_init_position[i * 2 + 1];
		}
	}
}

// =============================
// Board Synchronization
// =============================

/**
 * @brief Parses the server's board string and updates local state.
 * * Also updates the GST (Game State) object for the AI engine.
 * @param position String format: [X][Y][Color]...
 */
void MyAI::Set_board(char* position) {
	memset(board, -1, sizeof(board));
	memset(piece_colors, '0', sizeof(piece_colors));
	memset(p2_exist, 1, sizeof(p2_exist));
	memset(p1_exist, 1, sizeof(p1_exist));
	p2_piece_num = PIECES;
	p1_piece_num = PIECES;

	for (int i = 0; i < PIECES * 2; i++) {
		int index = i * 3;	// Each piece info takes 3 chars

		// Check if piece is dead (99 coordinates)
		if (position[index] == '9' && position[index + 1] == '9') {
			// Player 1 (User) pieces
			if (i < PIECES) {
				p1_piece_num--;
				p1_exist[i] = 0;
			}
			// Player 2 (Enemy) pieces
			else {
				p2_piece_num--;
				p2_exist[i - PIECES] = 0;
			}
			piece_pos[i] = -1;
		}
		// Alive pieces: 0~7 (P1), 8~15 (P2)
		else {
			board[position[index + 1] - '0'][position[index] - '0'] = i;
			piece_pos[i] = (position[index + 1] - '0') * BOARD_SIZE + (position[index] - '0');
		}
		piece_colors[i] = position[index + 2];	// Update color ('R', 'B', 'u', etc.)
	}

	// Sync with GST engine
	game.set_board(position);
	// game.record_board(); // Optional history tracking

	Print_chessboard();	 // Debug output
}

// =============================
// Debug Visualization
// =============================

/**
 * @brief Prints the current board state to stderr for debugging.
 */
void MyAI::Print_chessboard() {
	fprintf(stderr, "\n");
	// 0~7 represent P1 pieces; 8~15 represent P2 pieces
	for (int i = 0; i < BOARD_SIZE; i++) {
		fprintf(stderr, "<%d>", i);
		for (int j = 0; j < BOARD_SIZE; j++) {
			if (board[i][j] == -1)
				fprintf(stderr, "   -");
			else if (board[i][j] < PIECES) {
				fprintf(stderr, "%4c", board[i][j] + 'A');	// P1 pieces (Uppercase)
			} else
				fprintf(stderr, "%4c", board[i][j] - PIECES + 'a');	 // P2 pieces (Lowercase)
		}
		fprintf(stderr, "\n");
	}
	fprintf(stderr, "\n     ");
	for (int i = 0; i < BOARD_SIZE; i++) {
		fprintf(stderr, "<%d> ", i);
	}
	fprintf(stderr, "\n\n");
	fprintf(stderr, "The number of P1 pieces: %d\nThe number of P2 pieces: %d\n", p1_piece_num,
			p2_piece_num);
}

// =============================
// Move Generation
// =============================

/**
 * @brief Calculates the best move using ISMCTS and formats it string.
 * @param move Buffer to store the result string (e.g., "A,NORTH").
 */
void MyAI::Generate_move(char* move) {
	// Strategy: Use ISMCTS with N-Tuple heuristic guidance
	// int best_move = game.highest_weight(data); // Legacy: Pure N-Tuple Greedy
	int best_move = ismcts.findBestMove(game, data);

	int piece = best_move >> 4;
	int direction = best_move & 0xf;

	char piece_char = piece + 'A';

	const char* dir_str;
	switch (direction) {
		case 0:
			dir_str = "NORTH";
			break;
		case 1:
			dir_str = "WEST";
			break;
		case 2:
			dir_str = "EAST";
			break;
		case 3:
			dir_str = "SOUTH";
			break;
		default:
			dir_str = "UNKNOWN";
			break;
	}

	snprintf(move, 50, "%c,%s", piece_char, dir_str);

	// Apply move locally to keep state consistent
	game.do_move(best_move);
}