/**
 * @file ISMCTS.hpp
 * @brief Definition of the Information Set Monte Carlo Tree Search (ISMCTS) class.
 * @author Original Project Team (Inherited Code)
 * @author Chen You-Kai (Optimization & Docs)
 */

#ifndef ISMCTS_HPP
#define ISMCTS_HPP

#include "4T_GST.hpp"
#include "node.hpp"

/**
 * @class ISMCTS
 * @brief Implements Information Set Monte Carlo Tree Search (ISMCTS).
 * * Unlike standard MCTS, ISMCTS is designed for games with imperfect information.
 * It uses "Determinization" to sample concrete game states from the current information set
 * before running MCTS iterations.
 */
class ISMCTS {
   private:
	/// @name Configuration & State
	/// @{
	int simulations;			 ///< Number of simulations to perform per search
	std::mt19937 rng;			 ///< Random number generator (Mersenne Twister)
	std::unique_ptr<Node> root;	 ///< Root node of the search tree

	/**
	 * @brief Statistics for unknown piece arrangements.
	 * * Maps a string representation of a piece arrangement to a pair of <wins, count>.
	 * * Used to bias determinization based on historical performance (Inference Strategy).
	 */
	std::unordered_map<std::string, std::pair<int, int>> arrangement_stats;
	/// @}

	/// @name MCTS Core Stages
	/// @{
	/**
	 * @brief Phase 1: Selection
	 * * Traverses the tree based on UCB values using the specific determinized state.
	 * @param node Reference to the current node pointer (updated during traversal).
	 * @param d The determinized game state (concrete sample).
	 */
	void selection(Node*& node, GST& d);

	/**
	 * @brief Phase 2: Expansion
	 * * Expands the tree by adding ONE new child node.
	 * * Identifies a legal move (valid in the determinized state) that has not yet
	 * been expanded from the current node, creates a child for it, and returns it.
	 * @param node Pointer to the leaf node to expand.
	 * @param determinizedState The specific sampled state used for this iteration.
	 * @return Node* Pointer to the newly created child node.
	 */
	Node* expansion(Node* node, GST& determinizedState);

	/**
	 * @brief Phase 3: Simulation (Rollout)
	 * * Simulates a game to completion using a random or heuristic policy.
	 * @param state The starting state for simulation.
	 * @param d Shared data context.
	 * @param root_player The ID of the player at the root (to calculate relative reward).
	 * @return double The simulation result (reward).
	 */
	double simulation(GST& state, DATA& d, int root_player);

	/**
	 * @brief Phase 4: Backpropagation
	 * * Propagates the simulation result up the tree, updating visit counts and win scores.
	 * @param leaf The leaf node where simulation started.
	 * @param result The outcome value to propagate.
	 */
	void backpropagation(Node* leaf, double result);
	/// @}

	/// @name Determinization & Helpers
	/// @{
	/**
	 * @brief Calculates the Upper Confidence Bound (UCB1) value.
	 * @param node The node to evaluate.
	 * @return double The calculated UCB score.
	 */
	double calculateUCB(const Node* node) const;

	/**
	 * @brief Creates a concrete game state from the current information set.
	 * * Samples a specific world by assigning colors/types to hidden pieces.
	 * @param originalState The current game state with hidden info.
	 * @param current_iteration Current simulation index (used for adaptive strategies).
	 * @return GST A fully determined game state.
	 */
	GST getDeterminizedState(const GST& originalState, int current_iteration);

	/**
	 * @brief Randomizes unrevealed pieces on the board.
	 * * Strategy may shift from pure random to statistically weighted based on 'current_iteration'.
	 */
	void randomizeUnrevealedPieces(GST& state, int current_iteration);
	/// @}

	/**
	 * @brief Board movement direction offsets.
	 * * Values: Up (-6), Left (-1), Right (+1), Down (+6).
	 */
	static constexpr int dir_val[4] = {-6, -1, 1, 6};

   public:
	/**
	 * @brief Construct a new ISMCTS object.
	 * @param simulations Number of iterations to run per search.
	 */
	ISMCTS(int simulations);

	/**
	 * @brief Resets the ISMCTS tree and state.
	 */
	void reset();

	/**
	 * @brief Executes ISMCTS to find the optimal move.
	 * @param game The current game state (containing hidden info).
	 * @param d Shared data object.
	 * @return int The best move index found.
	 */
	int findBestMove(GST& game, DATA& d);

	/**
	 * @brief Debug helper: Recursively prints tree statistics.
	 * @param node The node to start printing from.
	 * @param indent Indentation level for visualization.
	 */
	void printNodeStats(const Node* node, int indent = 0) const;
};

#endif	// ISMCTS_HPP