/**
 * @file node.cpp
 * @brief Implementation of the MCTS Node structure
 * @author Original Project Team (Inherited Code)
 * @author Chen You-Kai (Optimization & Docs)
 */

#include "node.hpp"

// =============================
// Node Implementation
// =============================

/**
 * @brief Construct a new Node object.
 * * Initializes statistical counters (wins, visits) to zero and sets the
 * parent pointer to nullptr.
 * * @param move The move that led to this node (corresponds to the default value in header).
 */
Node::Node(int move)
	: move(move),	   // The move from parent to this node
	  wins(0),		   // Initialize win score to 0
	  visits(0),	   // Initialize visit count to 0
	  parent(nullptr)  // Initialize parent pointer to nullptr (assigned later)
{}