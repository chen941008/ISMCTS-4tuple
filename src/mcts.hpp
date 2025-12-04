/**
 * @file MCTS.hpp
 * @brief Definition of the MCTS class for standard Monte Carlo Tree Search
 * @author Original Project Team (Inherited Code)
 * @author Chen You-Kai (Optimization & Docs)
 */

#ifndef MCTS_HPP
#define MCTS_HPP

#include "4T_GST.hpp"
#include "4T_header.h"
#include "node.hpp"

/**
 * @class MCTS
 * @brief Implements the Standard Monte Carlo Tree Search algorithm.
 * * This class manages the 4 stages of MCTS: Selection, Expansion,
 * Simulation, and Backpropagation to find the best move for a given game state.
 */
class MCTS {
   private:
	/// @name Configuration & State
	/// @{
	int simulations;			 ///< Number of simulations to perform per search
	std::mt19937 rng;			 ///< Random number generator (Mersenne Twister)
	std::unique_ptr<Node> root;	 ///< Smart pointer to the root node of the search tree
	/// @}

	/// @name MCTS Core Stages
	/// @{
	/**
	 * @brief Selection Phase: Traverses down the tree to find a leaf node.
	 * @param node Reference to the current node pointer (updated during traversal).
	 * @param state The current game state (simulated during traversal).
	 */
	void selection(Node*& node, GST& state);

	/**
	 * @brief Expansion Phase: Adds child nodes to the selected leaf node.
	 * @param node Pointer to the leaf node to expand.
	 * @param state The game state at this node.
	 */
	void expansion(Node* node, GST& state);

	/**
	 * @brief Simulation Phase (Rollout): Simulates a random game until terminal state.
	 * @param state The game state to start simulation from.
	 * @return int The simulation result (e.g., 1 for win, 0 for loss).
	 */
	int simulation(GST& state);

	/**
	 * @brief Backpropagation Phase: Updates stats (wins/visits) up the tree.
	 * @param node The node where simulation started.
	 * @param result The result of the simulation.
	 */
	void backpropagation(Node* node, int result);
	/// @}

	/// @name Helper Functions
	/// @{
	/**
	 * @brief Calculates the Upper Confidence Bound (UCB1) value.
	 * @param node The node to evaluate.
	 * @return double The calculated UCB value.
	 */
	double calculateUCB(const Node* node) const;

	/**
	 * @brief Heuristic check: Determines if a move captures an enemy red piece.
	 * @param game The current game object.
	 * @param piece The piece being moved.
	 * @param dst The destination index.
	 * @return true If the move captures a red piece.
	 */
	bool would_eat_enemy_red(const GST& game, int piece, int dst) const;
	/// @}

	/**
	 * @brief Board movement direction offsets.
	 * * Values correspond to: Up (-6), Left (-1), Right (+1), Down (+6).
	 */
	static constexpr int dir_val[4] = {-6, -1, 1, 6};

   public:
	/**
	 * @brief Construct a new MCTS object.
	 * @param simulations Total number of simulations to run for each search.
	 */
	explicit MCTS(int simulations);

	/**
	 * @brief Resets the MCTS state (clears the tree).
	 */
	void reset();

	/**
	 * @brief Executes MCTS to find the optimal move.
	 * * Runs the 4 stages of MCTS for the specified number of simulations.
	 * @param game The current game state.
	 * @return int The best move index found.
	 */
	int findBestMove(GST& game);
};

#endif	// MCTS_HPP