/**
 * @file node.hpp
 * @brief Define MCTS node structure and related functions
 * @author Original Project Team (Inherited Code)
 * @author Chen You-Kai (Optimization & Docs)
 */

#ifndef NODE_HPP
#define NODE_HPP

#include "4T_header.h"

/**
 * @class Node
 * @brief Represents a node in the Monte Carlo Tree Search (MCTS) tree.
 * * This class stores node statistics (wins, visits), the move associated
 * with the node, and pointers to maintain the tree structure.
 */
class Node {
   public:
	/// @name Node Statistics
	/// @{
	int move;	  ///< The move taken from parent to reach this node (-1 indicates root)
	double wins;  ///< Accumulated win score from simulations
	int visits;	  ///< Total number of times this node has been visited
	std::unordered_map<int, int> avail_cnt;	 ///< Tracks available moves or state counts
	/// @}

	/// @name Tree Structure
	/// @{
	Node* parent;  ///< Pointer to parent node (does not own memory)
	std::vector<std::unique_ptr<Node>>
		children;  ///< List of child nodes (owns memory via unique_ptr)
	/// @}

	/**
	 * @brief Construct a new Node object.
	 * @param move The move leading to this node (default is -1 for root).
	 */
	Node(int move = -1);

	/**
	 * @brief Recursively cleans up the entire subtree.
	 * * @note This function provides a safe way to destroy deep trees, preventing
	 * potential stack overflow issues that might occur with default destructors.
	 * * @param node Reference to the unique_ptr of the node to be cleaned.
	 */
	static void cleanup(std::unique_ptr<Node>& node) {
		if (node) {
			// Recursively clean up child nodes
			for (auto& child : node->children) {
				cleanup(child);
			}
			// Clear the children vector of the current node
			node->children.clear();
			// Reset the current node (releases memory)
			node.reset();
		}
	}
};

#endif	// NODE_HPP