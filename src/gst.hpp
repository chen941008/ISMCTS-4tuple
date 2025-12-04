/**
 * @file GST.hpp
 * @brief Definition of the Game State (GST) class.
 * * This class encapsulates the board representation, game rules, move generation,
 * and state transitions. It also supports N-Tuple heuristic evaluation.
 * @author Original Project Team (Inherited Code)
 * @author Chen You-Kai (Optimization & Docs)
 */

#ifndef GST_HPP
#define GST_HPP

#include "4T_header.h"

class DATA;

/// @brief Global constant for UCB exploration (Standard value: sqrt(2) approx 1.414)
constexpr double EXPLORATION_PARAM = 1.414;

// Forward declarations
class ISMCTS;
class MCTS;

/**
 * @class GST
 * @brief Represents a snapshot of the game board and status.
 * * Manages piece positions, colors, visibility (fog of war), history, and
 * * interface for feature extraction (4-Tuple Network).
 */
class GST {
	// Grant direct access to AI solvers and main loop for performance optimization
	friend class ISMCTS;
	friend class MCTS;
	friend int main();

   private:
	/// @name Board Representation
	/// @{
	int board[ROW * COL];  ///< Grid status (0:Empty, 1:Red, 2:Blue, -1:Enemy Red, -2:Enemy Blue)
	int piece_board[ROW * COL];	 ///< Piece ID map (-1:None, 0~15:Piece ID)
	int pos[PIECES * 2];		 ///< Position lookup (0~7:User, 8~15:Enemy)
	int color[PIECES * 2];		 ///< Color lookup (1:Red, 2:Blue, -1:Enemy Red, -2:Enemy Blue)
	int piece_nums[4];	///< Piece counts [0]:MyRed, [1]:MyBlue, [2]:EnemyRed, [3]:EnemyBlue
	bool revealed[PIECES * 2] = {false};  ///< Fog of War: True if the piece is revealed
	/// @}

	/// @name Game Status
	/// @{
	int nowTurn;			 ///< Current turn (USER / ENEMY)
	int winner;				 ///< Game Result (USER / ENEMY / -1:None)
	bool is_escape = false;	 ///< Flag for "Escape" victory condition
	/// @}

	/// @name History & Tracking
	/// @{
	int history[1000];	///< Move history stack for Undo
	int n_plies;		///< Total plies (half-moves) played
	/// @}

   public:
	/// @name Core Game Logic
	/// @{
	/**
	 * @brief Initializes the board to the starting state.
	 */
	void init_board();

	/**
	 * @brief Renders the current board state to the console.
	 */
	void print_board();

	/**
	 * @brief Generates all legal moves for the current player.
	 * @param move_arr Output array to store moves.
	 * @return int Number of moves generated.
	 */
	int gen_all_move(int* move_arr);

	/**
	 * @brief Helper: Generates moves for a specific piece.
	 */
	int gen_move(int* move_arr, int piece, int location, int& count);

	/**
	 * @brief Executes a move and updates the state.
	 * @param move The encoded move integer.
	 */
	void do_move(int move);

	/**
	 * @brief Reverts the last move (Restores state from history).
	 */
	void undo();

	/**
	 * @brief Checks if the game has ended.
	 * @return true if there is a winner or draw condition met.
	 */
	bool is_over();

	/**
	 * @brief Check if a specific piece is revealed.
	 */
	bool is_revealed(int piece) const { return revealed[piece]; }
	/// @}

	/// @name 4-Tuple Heuristics (Feature Engineering)
	/// @{
	bool is_valid_pattern(int base_pos, const int* offset);	 ///< Checks if pattern is within bounds
	int get_loc(int base_pos, const int* offset);			 ///< Gets encoded location of a pattern
	int get_feature_unknown(int base_pos, const int* offset,
							const int* feature_cache);	///< Extracts feature index

	/**
	 * @brief Retrieves the trained weight for a specific pattern.
	 */
	float get_weight(int base_pos, const int* offset, DATA& d, const int* feature_cache);

	/**
	 * @brief Computes the heuristic score for the entire board.
	 * @return float Aggregated score from N-Tuple network.
	 */
	float compute_board_weight(DATA&);

	/**
	 * @brief Greedy Policy: Selects the move with the highest heuristic weight.
	 */
	int highest_weight(DATA&);
	/// @}

	/// @name Accessors & Helpers
	/// @{
	int get_color(int piece) const { return color[piece]; }
	int get_pos(int piece) const { return pos[piece]; }
	void set_color(int piece, int new_color) { color[piece] = new_color; }

	int get_winner() { return this->winner; }
	int get_nplies() { return this->n_plies; }

	// Direct access for MCTS (Oracle/Cheating mode)
	const int* get_full_colors() const { return color; }

	// Direct access for ISMCTS (Fog of War handling)
	const bool* get_revealed() const { return revealed; }
	/// @}
};

#endif	// GST_HPP