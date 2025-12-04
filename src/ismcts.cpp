/**
 * @file ISMCTS.cpp
 * @brief Implementation of Information Set Monte Carlo Tree Search
 * @author Original Project Team (Inherited Code)
 * @author Chen You-Kai (Optimization & Docs)
 */

#include "ismcts.hpp"

// Definition of static constant
constexpr int ISMCTS::dir_val[4];

// =============================
// Constructor & Lifecycle
// =============================

/**
 * @brief Construct a new ISMCTS object and seed the RNG.
 */
ISMCTS::ISMCTS(int simulations) : simulations(simulations) {
	auto seed = std::chrono::high_resolution_clock::now().time_since_epoch().count();
	rng.seed(static_cast<unsigned int>(seed));
}

/**
 * @brief Resets the ISMCTS state, clearing the tree and statistical data.
 */
void ISMCTS::reset() {
	auto seed = std::chrono::high_resolution_clock::now().time_since_epoch().count();
	rng.seed(static_cast<unsigned int>(seed));

	Node::cleanup(root);
	arrangement_stats.clear();
}

// =============================
// Determinization Strategy
// =============================

/**
 * @brief Creates a concrete game state (determinization) from the information set.
 * * Calls randomizeUnrevealedPieces to guess hidden information.
 */
GST ISMCTS::getDeterminizedState(const GST& originalState, int current_iteration) {
	GST determinizedState = originalState;
	randomizeUnrevealedPieces(determinizedState, current_iteration);
	return determinizedState;
}

/**
 * @brief Randomizes the colors of unrevealed pieces.
 * * Strategy:
 * * - Early iterations: Pure random shuffling.
 * * - Later iterations: Weighted random based on historical win rates (Inference).
 */
void ISMCTS::randomizeUnrevealedPieces(GST& state, int current_iteration) {
	const bool* revealed = state.get_revealed();
	std::vector<int> unrevealed_pieces;
	int redCount = 0, blueCount = 0;

	// 1. Identify all unrevealed pieces and count revealed colors
	for (int i = PIECES; i < PIECES * 2; i++) {
		if (revealed[i]) {
			if (state.get_color(i) == -RED)
				redCount++;
			else
				blueCount++;
		} else {
			unrevealed_pieces.push_back(i);
		}
	}

	int totalRed = 4;
	int totalBlue = 4;
	int redRemaining = totalRed - redCount;
	int blueRemaining = totalBlue - blueCount;

	if (unrevealed_pieces.empty()) return;

	// 2. Decide strategy based on iteration count
	// Use inference stats only in the latter half of simulations
	bool use_stats = (current_iteration >= simulations / 2);

	if (!use_stats) {
		// Strategy A: Pure Random Shuffle
		std::shuffle(unrevealed_pieces.begin(), unrevealed_pieces.end(), rng);
		for (size_t i = 0; i < unrevealed_pieces.size(); i++) {
			int piece = unrevealed_pieces[i];
			if (i < redRemaining) {
				state.set_color(piece, -RED);
			} else {
				state.set_color(piece, -BLUE);
			}
		}
		return;
	}

	// Strategy B: Inference-based Weighted Shuffle
	// Generate all valid arrangements of remaining pieces
	std::vector<std::vector<int>> arrangements;
	int total_pieces = redRemaining + blueRemaining;
	int total_combinations = 1 << total_pieces;

	for (int mask = 0; mask < total_combinations; mask++) {
		std::vector<int> arrangement;
		int red = 0, blue = 0;
		for (int i = 0; i < total_pieces; i++) {
			if (mask & (1 << i)) {
				arrangement.push_back(-RED);
				red++;
			} else {
				arrangement.push_back(-BLUE);
				blue++;
			}
		}
		if (red == redRemaining && blue == blueRemaining) {
			arrangements.push_back(arrangement);
		}
	}

	// Calculate win probability for each arrangement
	std::vector<double> win_rates;
	for (const auto& arrangement : arrangements) {
		std::string key;
		for (auto color : arrangement) {
			key += (color == -RED) ? 'R' : 'B';
		}

		auto it = arrangement_stats.find(key);
		if (it == arrangement_stats.end() || it->second.second == 0) {
			win_rates.push_back(0.5);  // Default to 50% if no data
		} else {
			double win_rate = static_cast<double>(it->second.first) / it->second.second;
			win_rates.push_back(win_rate);
		}
	}

	// Calculate selection weights (inverse win rate logic to find harder scenarios?)
	std::vector<double> weights;
	double total_weight = 0.0;
	for (double rate : win_rates) {
		double weight = 1.0 - rate + 0.05;	// Add bias
		weights.push_back(weight);
		total_weight += weight;
	}

	for (auto& w : weights) {
		w /= total_weight;
	}

	// Select an arrangement using weighted random distribution
	std::uniform_real_distribution<> dist(0.0, 1.0);
	double r = dist(rng);
	double cumulative = 0.0;
	int selected_idx = 0;
	for (size_t i = 0; i < weights.size(); i++) {
		cumulative += weights[i];
		if (r <= cumulative) {
			selected_idx = i;
			break;
		}
	}

	// Apply the selected arrangement to the state
	const auto& selected_arrangement = arrangements[selected_idx];
	for (size_t i = 0; i < unrevealed_pieces.size(); i++) {
		state.set_color(unrevealed_pieces[i], selected_arrangement[i]);
	}
}

// =============================
// Helper Functions
// =============================

/**
 * @brief Calculates UCB1 value for ISMCTS.
 * * Note: Uses 'avail_cnt' from parent to account for how often this move
 * * was available in determinizations, rather than just visits.
 */
double ISMCTS::calculateUCB(const Node* node) const {
	// Infinite priority for unvisited nodes
	if (!node || node->visits == 0) return std::numeric_limits<double>::infinity();

	// Exploitation: Average reward (-1 to 1)
	double mean = static_cast<double>(node->wins) / node->visits;

	// Exploration: Based on availability count from parent
	int avail = 1;	// Avoid log(0)
	if (node->parent) {
		const auto& m = node->parent->avail_cnt;
		auto it = m.find(node->move);
		if (it != m.end()) avail = it->second;
	}

	int visits = std::max(1, node->visits);

	// UCB Formula
	double exploration =
		EXPLORATION_PARAM * std::sqrt(std::log(static_cast<double>(avail)) / visits);

	return mean + exploration;
}

// =============================
// ISMCTS Core Stages
// =============================

/**
 * @brief Phase 1: Selection
 * * Traverses the tree. Because edges vary by determinization, we check
 * * if the current node is "fully expanded" relative to the current determinized state.
 */
void ISMCTS::selection(Node*& node, GST& d) {
	while (!d.is_over()) {
		int moves[MAX_MOVES];
		int n = d.gen_all_move(moves);
		if (n == 0) break;

		// Update availability count for these compatible moves
		for (int i = 0; i < n; ++i) {
			node->avail_cnt[moves[i]]++;
		}

		// Check if node is fully expanded w.r.t the current determinization (d)
		// i.e., Do all valid moves in 'd' already have corresponding children?
		bool fully = true;
		for (int i = 0; i < n; ++i) {
			bool found = false;
			for (auto& ch : node->children)
				if (ch->move == moves[i]) {
					found = true;
					break;
				}
			if (!found) {
				fully = false;
				break;
			}
		}

		// If not fully expanded, stop selection here and proceed to expansion phase
		if (!fully) return;

		// Filter children: only consider children compatible with current 'd'
		std::vector<Node*> cand;
		cand.reserve(node->children.size());
		for (auto& ch : node->children)
			if (std::find(moves, moves + n, ch->move) != moves + n) cand.push_back(ch.get());

		if (cand.empty()) return;  // Defense check

		// Heuristic: Prioritize unvisited compatible children first
		std::vector<Node*> unvisited;
		for (auto* c : cand)
			if (c->visits == 0) unvisited.push_back(c);

		if (!unvisited.empty()) {
			std::uniform_int_distribution<int> pick(0, (int)unvisited.size() - 1);
			Node* next = unvisited[pick(rng)];
			node = next;
			d.do_move(node->move);
			continue;
		}

		// Standard UCB Selection among compatible children
		Node* best = nullptr;
		double bestU = -1e100;

		for (auto* c : cand) {
			const double u = calculateUCB(c);
			if (u > bestU) {
				bestU = u;
				best = c;
			}
		}
		node = best;
		d.do_move(node->move);
	}
}

/**
 * @brief Phase 2: Expansion
 * * Expands the tree by adding ONE new child node valid in the current determinization.
 */
Node* ISMCTS::expansion(Node* node, GST& determinizedState) {
	if (determinizedState.is_over()) return nullptr;

	int moves[MAX_MOVES];
	int moveCount = determinizedState.gen_all_move(moves);

	// U = Set of legal moves in 'd' that do NOT have children yet
	std::vector<int> U;
	U.reserve(moveCount);
	for (int i = 0; i < moveCount; ++i) {
		bool used = false;
		for (auto& ch : node->children)
			if (ch->move == moves[i]) {
				used = true;
				break;
			}
		if (!used) U.push_back(moves[i]);
	}

	if (U.empty()) return nullptr;

	// Pick one unexpanded move randomly
	int move = U[std::uniform_int_distribution<int>(0, (int)U.size() - 1)(rng)];

	auto newNode = std::make_unique<Node>(move);
	newNode->parent = node;
	Node* ret = newNode.get();
	node->children.push_back(std::move(newNode));
	return ret;
}

/**
 * @brief Phase 3: Simulation (Rollout)
 * * Epsilon-greedy simulation using weighted heuristics (DATA& d).
 * @return 1.0 if root_player wins, -1.0 otherwise.
 */
double ISMCTS::simulation(GST& state, DATA& d, int root_player) {
	GST simState = state;

	int moves[MAX_MOVES];
	int moveCount;
	int maxMoves = 200;
	int step = 0;

	std::uniform_real_distribution<> probDist(0.0, 1.0);

	while (!simState.is_over() && step < maxMoves) {
		moveCount = simState.gen_all_move(moves);
		if (moveCount == 0) break;

		std::uniform_int_distribution<int> pick(0, moveCount - 1);

		int move;
		// Decaying epsilon: exploring less as game progresses
		double epsilon = std::max(0.1, 1.0 - static_cast<double>(step) / maxMoves);

		if (simState.nowTurn == USER) {
			// User Policy: Epsilon-Greedy
			if (probDist(rng) < epsilon) {
				move = moves[pick(rng)];
			} else {
				move = simState.highest_weight(d);	// Greedy choice based on weights
			}
		} else {
			// Enemy Policy: Random
			move = moves[pick(rng)];
		}

		simState.do_move(move);
		++step;
	}

	if (!simState.is_over() && step >= maxMoves) return 0.0;  // Draw / Timeout

	int winner = simState.get_winner();
	return (winner == root_player) ? 1.0 : -1.0;
}

/**
 * @brief Phase 4: Backpropagation
 * * Updates stats. Note: This assumes fixed root perspective (wins are accumulated).
 */
void ISMCTS::backpropagation(Node* leaf, double result) {
	// Standard backprop (Non-Minimax update)
	// Wins are always from the perspective of the root player
	for (Node* p = leaf; p; p = p->parent) {
		p->visits += 1;
		p->wins += result;
	}
}

// =============================
// Main Interface
// =============================

/**
 * @brief Main ISMCTS Loop
 */
int ISMCTS::findBestMove(GST& game, DATA& d) {
	// 1. Reset Tree
	Node::cleanup(root);
	root.reset(new Node());
	arrangement_stats.clear();

	// Identify Root Player to anchor simulation results
	int root_player = game.nowTurn;

	// 2. Main Simulation Loop
	for (int i = 0; i < simulations; i++) {
		Node* currentNode = root.get();

		// Step A: Determinization (Sample a specific world)
		GST determinizedState = getDeterminizedState(game, i);

		// Step B: Selection
		selection(currentNode, determinizedState);

		// Step C: Expansion (if needed)
		if (!determinizedState.is_over()) {
			Node* added = expansion(currentNode, determinizedState);
			if (added) {
				currentNode = added;
				determinizedState.do_move(currentNode->move);  // Advance state to new node
			}
		}

		// Step D: Simulation
		double result = simulation(determinizedState, d, root_player);

		// Step E: Update Inference Stats (Arrangement Win Rates)
		std::string arrangementKey;
		const bool* revealed = game.get_revealed();
		for (int i = PIECES; i < PIECES * 2; i++) {
			if (!revealed[i]) {
				int color = determinizedState.get_color(i);
				arrangementKey += (color == -RED ? 'R' : 'B');
			}
		}
		auto& stats = arrangement_stats[arrangementKey];
		if (result > 0) stats.first += 1;  // Wins
		stats.second += 1;				   // Total simulations for this arrangement

		// Step F: Backpropagation
		backpropagation(currentNode, result);
	}

	// 3. Select Best Move
	Node* bestChild = nullptr;
	int maxVisits = -1;
	const char* dirNames[] = {"N", "W", "E", "S"};
	bool hasValidMoves = false;

	// Robust Child Criteria: Pick the most visited node
	for (auto& child : root->children) {
		if (child->visits > maxVisits) {
			maxVisits = child->visits;
			bestChild = child.get();
			hasValidMoves = true;
		}
	}

	if (!hasValidMoves) {
		fprintf(stderr, "No valid moves found. This might indicate the game is already over.\n");
		return -1;
	}

	// Optional: Debug Output
	if (bestChild) {
		int piece = bestChild->move >> 4;
		int direction = bestChild->move & 0xf;

		fprintf(stderr, "ISMCTS Selected: Piece %d, Dir %s\n", piece, dirNames[direction]);
		fprintf(stderr, "Win Rate: %.2f%%\n",
				bestChild->visits > 0
					? static_cast<double>(bestChild->wins) / bestChild->visits * 100
					: 0.0);
	}

	fprintf(stderr, "Returning Move: %d\n", bestChild->move);

	return bestChild->move;
}