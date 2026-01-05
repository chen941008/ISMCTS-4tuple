/**
 * @file ISMCTS.cpp
 * @brief Implementation of Information Set Monte Carlo Tree Search
 * @author Original Project Team (Inherited Code)
 * @author Chen You-Kai (Optimization & Docs)
 */

#include "ismcts.hpp"

// Definition of static constant
constexpr int ISMCTS::dir_val[4];

#if defined(_MSC_VER)
// Windows (Visual Studio) 環境
#include <intrin.h>

inline int popcount64(uint64_t b) { return __popcnt64(b); }

inline int bit_scan_forward(uint64_t b) {
	if (b == 0) return 64;
	unsigned long index;
	_BitScanForward64(&index, b);
	return index;
}

#else
// Mac / Linux (GCC, Clang) 環境
// 這些是編譯器內建指令，所有現代 GCC/Clang 版本都支援

inline int popcount64(uint64_t b) {
	return __builtin_popcountll(b);	 // 注意：一定要用 ll (long long)
}

inline int bit_scan_forward(uint64_t b) {
	if (b == 0) return 64;
	return __builtin_ctzll(b);	// ctz = Count Trailing Zeros
}
#endif

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
	// 1. 找出所有敵方棋子的位置 (Bitboard 掃描)
	// 假設 ISMCTS 是站在 User (我方) 的角度思考，我們要隨機化的是 Enemy (敵方) 的配置
	uint64_t enemy_mask = state.emy_red | state.emy_blue;

	// 如果沒有敵人，直接返回
	if (!enemy_mask) return;

	std::vector<int> enemy_positions;
	enemy_positions.reserve(8);

	uint64_t temp_mask = enemy_mask;
	while (temp_mask) {
		int sq = bit_scan_forward(temp_mask);
		enemy_positions.push_back(sq);
		temp_mask &= temp_mask - 1;
	}

	// 2. 統計目前盤面上敵人的紅藍數量
	// 注意：這裡是取 "當前 determinization" 的數量，通常這會符合真實剩餘數量
	int red_count = popcount64(state.emy_red);
	int blue_count = popcount64(state.emy_blue);

	// 3. 準備洗牌用的顏色包 (1: Red, 2: Blue)
	std::vector<int> colors;
	colors.reserve(8);
	for (int i = 0; i < red_count; ++i) colors.push_back(1);
	for (int i = 0; i < blue_count; ++i) colors.push_back(2);

	// 4. 執行洗牌 (Strategy A: Pure Random)
	// 註：原本的 Strategy B (Inference) 需要 Piece ID 才能追蹤特定組合的勝率。
	// 在 Bitboard 化後，因為失去 ID，實作 Inference 變得非常複雜且效益不高。
	// 建議先只使用純隨機洗牌，這在 ISMCTS 中已經非常強大。
	std::shuffle(colors.begin(), colors.end(), rng);

	// 5. 將洗牌後的結果寫回 Bitboard
	state.emy_red = 0;
	state.emy_blue = 0;

	for (size_t i = 0; i < enemy_positions.size(); ++i) {
		int sq = enemy_positions[i];
		int color = colors[i];

		if (color == 1) {  // Red
			state.emy_red |= (1ULL << sq);
		} else {  // Blue
			state.emy_blue |= (1ULL << sq);
		}
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
		/*
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
		*/

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
	/*
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
	*/
	// Optional: Debug Output
	if (bestChild) {
		// 1. 解碼 Bitboard 移動格式
		int move = bestChild->move;
		int from = (move >> 8) & 0xFF;
		int to = move & 0xFF;

		// 2. 逆推方向 (Direction)
		const char* dirName = "Unknown";
		int diff = to - from;

		// 處理特殊逃脫座標 (60, 61)
		if (to == 60)
			dirName = "ESC_L";	// ESCAPE_LEFT_TARGET
		else if (to == 61)
			dirName = "ESC_R";	// ESCAPE_RIGHT_TARGET
		else {
			if (diff == -6)
				dirName = "N";	// Up (-ROW)
			else if (diff == 6)
				dirName = "S";	// Down (+ROW)
			else if (diff == -1)
				dirName = "W";	// Left (-1)
			else if (diff == 1)
				dirName = "E";	// Right (+1)
		}

		// 3. 將座標轉為 A0~F5 格式，方便閱讀
		char colChar = 'A' + (from % 6);
		int rowNum = from / 6;

		// 印出：位置座標 -> 方向
		fprintf(stderr, "ISMCTS Selected: Pos %c%d %s\n", colChar, rowNum, dirName);

		// 印出勝率 (維持不變)
		fprintf(stderr, "Win Rate: %.2f%% (Visits: %d)\n",
				bestChild->visits > 0
					? static_cast<double>(bestChild->wins) / bestChild->visits * 100
					: 0.0,
				bestChild->visits);
	}

	fprintf(stderr, "Returning Move: %d\n", bestChild ? bestChild->move : -1);

	return bestChild->move;
}