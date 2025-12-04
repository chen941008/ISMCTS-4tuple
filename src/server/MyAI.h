/**
 * @file MyAI.hpp
 * @brief Definition of the MyAI class.
 * * This class acts as the main interface for the AI server, handling protocol commands
 * * and managing the internal game state for decision making.
 * @author Original Project Team (Inherited Code)
 * @author Chen You-Kai (Optimization & Docs)
 */

#ifndef MYAI_INCLUDED
#define MYAI_INCLUDED

#include "../4T_DATA.hpp"
#include "../4T_header.h"

using std::stoi;
using std::string;

#define BOARD_SIZE 6
#define COMMAND_NUM 7

/**
 * @class MyAI
 * @brief Main AI Server Class.
 * * Handles communication with the game judge/server using the specified protocol.
 * * Parses commands, maintains local state, and triggers the AI logic (GST/MCTS).
 */
class MyAI {
   private:
	/// @name Command Protocol
	/// @{
	const char* commands_name[COMMAND_NUM] = {"name", "version", "time_setting", "board_setting",
											  "ini",  "get",	 "exit"};
	/// @}

	/// @name Game State Data
	/// @{
	bool p1_exist[PIECES];				///< Presence flags for Player 1's pieces
	bool p2_exist[PIECES];				///< Presence flags for Player 2's pieces
	int player;							///< Current player ID assigned to this AI
	int p1_time;						///< Time remaining/limit for Player 1
	int p2_time;						///< Time remaining/limit for Player 2
	int board_size;						///< Dimensions of the board (Standard: 6)
	int dice;							///< Dice value (if used in protocol)
	int board[BOARD_SIZE][BOARD_SIZE];	///< Local 2D board representation
	int p1_piece_num;					///< Number of pieces remaining for Player 1
	int p2_piece_num;					///< Number of pieces remaining for Player 2
	char piece_colors[PIECES * 2];		///< Color array for all pieces
	int piece_pos[PIECES * 2];			///< Position array for all pieces
	/// @}

	/// @name Board Operations
	/// @{
	/**
	 * @brief Parses the initial board string to set up internal state.
	 * @param position String representation of the board.
	 */
	void Init_board_state(char* position);

	/**
	 * @brief Updates the board state based on a position string.
	 * @param position String representation of the board.
	 */
	void Set_board(char* position);

	/**
	 * @brief Debug helper: Prints the current chessboard to console.
	 */
	void Print_chessboard();

	/**
	 * @brief Core Logic: Generates the best move using AI algorithms.
	 * @param move Buffer to store the generated move string.
	 */
	void Generate_move(char* move);
	/// @}

   public:
	/**
	 * @brief Constructor.
	 */
	MyAI(void);

	/**
	 * @brief Destructor.
	 */
	~MyAI(void);

	/// @name Server Command Handlers
	/// @{
	/**
	 * @brief Handles the 'ini' command (Initialization).
	 * @param data Command arguments.
	 * @param response Buffer for the response.
	 */
	void Ini(const char* data[], char* response);

	/**
	 * @brief Handles the 'set' command (Set Red pieces / Setup).
	 * @param response Buffer for the response.
	 */
	void Set(char* response);

	/**
	 * @brief Handles the 'get' command (Request Move).
	 * * This triggers the AI to think and return a move.
	 * @param data Command arguments.
	 * @param response Buffer for the response (the move).
	 */
	void Get(const char* data[], char* response);

	/**
	 * @brief Handles the 'exit' command.
	 * @param data Command arguments.
	 * @param response Buffer for the response.
	 */
	void Exit(const char* data[], char* response);
	/// @}
};

#endif	// MYAI_INCLUDED