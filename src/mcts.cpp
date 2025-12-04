/**
 * @file MCTS.cpp
 * @brief Implementation of the Standard Monte Carlo Tree Search
 * @author Original Project Team (Inherited Code)
 * @author Chen You-Kai (Optimization & Docs)
 */

#include "mcts.hpp"

// Definition of static constant
constexpr int MCTS::dir_val[4];

// =============================
// Constructor & Lifecycle
// =============================

/**
 * @brief Construct a new MCTS object and seed the RNG.
 */
MCTS::MCTS(int simulations) : simulations(simulations) {
	auto seed = std::chrono::high_resolution_clock::now().time_since_epoch().count();
	rng.seed(static_cast<unsigned int>(seed));
}

/**
 * @brief Resets the RNG and clears the entire search tree.
 */
void MCTS::reset() {
	auto seed = std::chrono::high_resolution_clock::now().time_since_epoch().count();
	rng.seed(static_cast<unsigned int>(seed));

	Node::cleanup(root);
}

// =============================
// Helper Functions
// =============================

/**
 * @brief Heuristic filter: Checks if a move targets an enemy Red piece.
 * * Used to prune or skip specific moves during expansion.
 */
bool MCTS::would_eat_enemy_red(const GST& game, int piece, int dst) const {
	int target_piece = game.piece_board[dst];
	if (target_piece == -1) return false;

	// Check if pieces belong to different camps (Enemy check)
	bool is_enemy =
		((piece < PIECES && target_piece >= PIECES) || (piece >= PIECES && target_piece < PIECES));

	if (!is_enemy) return false;

	int target_color = game.get_color(target_piece);
	return std::abs(target_color) == RED;
}

/**
 * @brief Calculates the Upper Confidence Bound (UCB1) value.
 * * Uses standard formula: exploitation + C * exploration
 */
double MCTS::calculateUCB(const Node* node) const {
	if (node->visits == 0) return std::numeric_limits<double>::infinity();

	double exploitation = static_cast<double>(node->wins) / node->visits;
	// Exploration parameter controls the balance between width and depth search
	double exploration =
		EXPLORATION_PARAM * std::sqrt(std::log(node->parent->visits) / node->visits);

	return exploitation + exploration;
}

// =============================
// MCTS Core Stages
// =============================

/**
 * @brief Phase 1: Selection
 * * Traverses the tree from root to leaf using UCB policy.
 */
void MCTS::selection(Node*& node, GST& state) {
	while (!state.is_over() && !node->children.empty()) {
		Node* bestChild = nullptr;
		double bestUCB = -std::numeric_limits<double>::infinity();

		for (auto& child : node->children) {
			// If a child has never been visited, prioritize it immediately (Infinite UCB)
			if (child->visits == 0) {
				node = child.get();
				return;
			}

			double ucb = calculateUCB(child.get());
			if (ucb > bestUCB) {
				bestUCB = ucb;
				bestChild = child.get();
			}
		}

		if (bestChild)
			node = bestChild;
		else
			break;
	}
}

/**
 * @brief Phase 2: Expansion
 * * Generates all valid moves for the leaf node and adds them as children.
 * * Applies heuristics to skip certain moves (e.g., eating red pieces).
 */
void MCTS::expansion(Node* node, GST& state) {
	if (state.is_over()) return;

	int moves[MAX_MOVES];
	int moveCount = state.gen_all_move(moves);

	for (int i = 0; i < moveCount; i++) {
		int move = moves[i];
		int piece = move >> 4;
		int dir = move & 0xf;
		int dst = state.get_pos(piece) + dir_val[dir];

		// Apply heuristic pruning
		if (would_eat_enemy_red(state, piece, dst)) continue;

		// Create new child node
		GST newState = state;
		newState.do_move(move);

		std::unique_ptr<Node> newNode(new Node(move));
		newNode->parent = node;
		node->children.push_back(std::move(newNode));
	}
}

/**
 * @brief Phase 3: Simulation (Rollout)
 * * Plays a random game from the current state until terminal state or depth limit.
 * @return 1 if Enemy wins, -1 otherwise (from the perspective of current player)
 */
int MCTS::simulation(GST& state) {
	GST simState = state;
	int moves[MAX_MOVES];
	int moveCount;
	int maxDepth = 1000;
	int depth = 0;

	std::uniform_int_distribution<> dist(0, INT_MAX);

	while (!simState.is_over() && depth < maxDepth) {
		moveCount = simState.gen_all_move(moves);
		if (moveCount == 0) break;

		// Pure random policy
		int randomIndex = dist(rng) % moveCount;
		simState.do_move(moves[randomIndex]);
		depth++;
	}

	// Handle game ended or depth limit reached
	if (!simState.is_over() && depth >= maxDepth) return 0;	 // Draw/Timeout
	return simState.get_winner() == ENEMY ? 1 : -1;
}

/**
 * @brief Phase 4: Backpropagation
 * * Updates the win/visit statistics from the simulation node back up to the root.
 */
void MCTS::backpropagation(Node* node, int result) {
	while (node != nullptr) {
		node->visits += 1;
		node->wins += result;
		result = -result;  // Toggle result for Minimax (switch perspective)
		node = node->parent;
	}
}

// =============================
// Main Interface
// =============================

/**
 * @brief Executes the MCTS algorithm to find the optimal move.
 */
int MCTS::findBestMove(GST& game) {
	// 1. Clean up previous tree and initialize root
	Node::cleanup(root);
	root.reset(new Node());

	// 2. Main MCTS Loop
	for (int i = 0; i < simulations; i++) {
		Node* currentNode = root.get();

		// Stage 1: Selection
		selection(currentNode, game);

		// Stage 2: Expansion
		// Expand if node is a leaf and game is not over
		if (currentNode->children.empty() && !game.is_over()) {
			expansion(currentNode, game);
		}

		// Determine which node to simulate
		Node* nodeToSimulate = currentNode;

		// Randomly pick a child to simulate if children exist
		if (!currentNode->children.empty()) {
			std::uniform_int_distribution<> dist(0, currentNode->children.size() - 1);
			int randomIndex = dist(rng);
			auto it = std::next(currentNode->children.begin(), randomIndex);
			nodeToSimulate = it->get();
		}

		// Stage 3: Simulation
		GST simulationState = game;
		if (nodeToSimulate->move != -1) {  // Apply move if not root
			simulationState.do_move(nodeToSimulate->move);
		}
		int result = simulation(simulationState);

		// Stage 4: Backpropagation
		backpropagation(nodeToSimulate, result);
	}

	// 3. Select Best Move (Robust Child)
	Node* bestChild = nullptr;
	int maxVisits = -1;

	for (auto& child : root->children) {
		if (child->visits > maxVisits) {
			maxVisits = child->visits;
			bestChild = child.get();
		}
	}

	// Optional: Debug Output
	if (bestChild) {
		int piece = bestChild->move >> 4;
		int direction = bestChild->move & 0xf;
		const char* dirNames[] = {"N", "W", "E", "S"};

		std::cout << "MCTS Selected Move: ";
		if (piece < PIECES)
			std::cout << static_cast<char>('A' + piece % PIECES);
		else
			std::cout << static_cast<char>('a' + (piece - PIECES) % PIECES);
		std::cout << " " << dirNames[direction] << std::endl;
	}

	return bestChild ? bestChild->move : -1;
}