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
	rng.seed(0);
}

/**
 * @brief Resets the ISMCTS state, clearing the tree and statistical data.
 */
void ISMCTS::reset() {
	auto seed = std::chrono::high_resolution_clock::now().time_since_epoch().count();
	rng.seed(0);

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
	// 1. 判斷誰是「對手」(Unknown Side)
	uint64_t* target_red_ptr;
	uint64_t* target_blue_ptr;
	// 用來查表時判斷視角的基準 (誰在思考，誰就是 Root)
	int root_player_for_stats = state.nowTurn;

	if (state.nowTurn == 0) {  // 我是 User
		target_red_ptr = &state.emy_red;
		target_blue_ptr = &state.emy_blue;
	} else {  // 我是 Enemy
		target_red_ptr = &state.my_red;
		target_blue_ptr = &state.my_blue;
	}

	// 2. 找出所有「對手」棋子的位置 (Bitboard 掃描)
	uint64_t opponent_mask = *target_red_ptr | *target_blue_ptr;
	if (!opponent_mask) return;

	std::vector<int> opponent_positions;
	opponent_positions.reserve(16);

	uint64_t temp_mask = opponent_mask;
	while (temp_mask) {
		int sq = bit_scan_forward(temp_mask);
		opponent_positions.push_back(sq);
		temp_mask &= temp_mask - 1;
	}

	// 3. 準備顏色包 (1: Red, 2: Blue)
	int red_count = popcount64(*target_red_ptr);
	int blue_count = popcount64(*target_blue_ptr);

	std::vector<int> colors;
	colors.reserve(16);
	for (int i = 0; i < red_count; ++i) colors.push_back(1);
	for (int i = 0; i < blue_count; ++i) colors.push_back(2);

	// =================================================================
	// 策略分歧點 (復刻原版邏輯)
	// =================================================================

	// 只有在後半段模擬才使用推論統計 (Inference Stats)
	bool use_stats = (current_iteration >= simulations / 2);

	if (!use_stats) {
		// [Strategy A] 純隨機洗牌 (Pure Random Shuffle)
		std::shuffle(colors.begin(), colors.end(), rng);

		// 寫回 Bitboard
		*target_red_ptr = 0;
		*target_blue_ptr = 0;
		for (size_t i = 0; i < opponent_positions.size(); ++i) {
			if (colors[i] == 1)
				*target_red_ptr |= (1ULL << opponent_positions[i]);
			else
				*target_blue_ptr |= (1ULL << opponent_positions[i]);
		}
		return;
	}

	// [Strategy B] 基於推論的加權洗牌 (Inference-based Weighted Shuffle)
	// 目標：窮舉所有可能的顏色排列，計算權重，然後選一個

	// 1. 必須先排序，這是 std::next_permutation 的要求
	std::sort(colors.begin(), colors.end());

	struct Candidate {
		uint64_t red_config;
		double weight;
	};
	std::vector<Candidate> candidates;
	double total_weight = 0.0;

	// 2. 窮舉所有排列 (Generate All Arrangements)
	// 暗棋最多 4紅4藍，排列組合數 C(8,4)=70，迴圈次數很少，效能極高
	do {
		// 根據當前排列，組合成一個暫時的紅棋 Bitboard
		uint64_t tmp_red = 0;
		for (size_t i = 0; i < opponent_positions.size(); ++i) {
			if (colors[i] == 1) {  // Red
				tmp_red |= (1ULL << opponent_positions[i]);
			}
		}

		// 3. 查表計算勝率 (Calculate Win Rate)
		// Key 就是紅棋的 Bitboard (tmp_red)
		auto it = arrangement_stats.find(tmp_red);
		double win_rate = 0.5;	// 預設 50%

		if (it != arrangement_stats.end() && it->second.second > 0) {
			win_rate = it->second.first / (double)it->second.second;
		}

		// 4. 計算權重 (Inverse Win Rate Logic)
		// 邏輯：選擇那些「讓我方勝率低」(即敵人很強) 的配置，進行針對性訓練
		// Bias 0.05 是為了避免權重為 0
		double weight = 1.0 - win_rate + 0.05;

		candidates.push_back({tmp_red, weight});
		total_weight += weight;

	} while (std::next_permutation(colors.begin(), colors.end()));

	// 5. 加權隨機選擇 (Weighted Random Selection)
	std::uniform_real_distribution<> dist(0.0, total_weight);
	double r = dist(rng);
	double cumulative = 0.0;
	uint64_t selected_red_config = 0;

	// 如果因為某些原因沒生成候選者 (防呆)，預設用最後一個
	if (!candidates.empty()) selected_red_config = candidates.back().red_config;

	for (const auto& cand : candidates) {
		cumulative += cand.weight;
		if (r <= cumulative) {
			selected_red_config = cand.red_config;
			break;
		}
	}

	// 6. 將選中的配置應用到 State
	*target_red_ptr = selected_red_config;
	// 藍棋就是「所有位置」扣掉「紅棋位置」
	*target_blue_ptr = opponent_mask ^ selected_red_config;
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

		// =========================================================
		// Step E: Update Inference Stats (Bitboard Version)
		// =========================================================

		// 取得代表這次「猜測配置」的唯一 Key
		// 如果我是 User，我在猜 Enemy 的紅棋位置 (emy_red)
		// 如果我是 Enemy，我在猜 User 的紅棋位置 (my_red)
		uint64_t arrangementKey = 0;
		if (root_player == 0) {
			arrangementKey = determinizedState.emy_red;
		} else {
			arrangementKey = determinizedState.my_red;
		}

		// 存入 Map
		// stats.first: 勝場數 (累積 result, 贏是+1)
		// stats.second: 該配置被模擬的總次數
		auto& stats = arrangement_stats[arrangementKey];
		if (result > 0) stats.first += 1.0;
		stats.second += 1;

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

	{
		// 使用 append 模式 (std::ios::app)，這樣資料會一直往下寫而不會覆蓋
		static std::ofstream tlog("ismcts_tree_debug_log.txt", std::ios::out | std::ios::app);

		if (tlog.is_open()) {
			tlog << "--------------------------------------------------\n";
			tlog << "ISMCTS Root Stats | Turn: " << (game.nowTurn == 0 ? "USER" : "ENEMY") << "\n";

			// 為了方便閱讀，我們把子節點依照「訪問次數」由高到低排序
			std::vector<Node*> sortedChildren;
			sortedChildren.reserve(root->children.size());
			for (auto& c : root->children) {
				sortedChildren.push_back(c.get());
			}
			std::sort(sortedChildren.begin(), sortedChildren.end(), [](Node* a, Node* b) {
				return a->visits > b->visits;  // 降冪排序
			});

			for (Node* child : sortedChildren) {
				int move = child->move;
				int from = (move >> 8) & 0xFF;
				int to = move & 0xFF;

				// --- 座標轉字串 Helper (與 BitboardGST.cpp 邏輯一致) ---
				auto to_str = [&](int idx, bool is_dest) -> std::string {
					if (is_dest) {
						if (idx == 8 || idx == 48) return "ESC_L";
						if (idx == 15 || idx == 55) return "ESC_R";
					}
					int p_row = idx / 8;
					int p_col = idx % 8;
					int l_row = p_row - 1;
					int l_col = p_col - 1;
					if (l_row < 0 || l_row > 5 || l_col < 0 || l_col > 5) return "OUT";
					char c = 'A' + l_col;
					return std::string(1, c) + std::to_string(l_row);
				};

				std::string from_s = to_str(from, false);
				std::string to_s = to_str(to, true);

				// 計算勝率
				double winRate = (child->visits > 0)
									 ? (static_cast<double>(child->wins) / child->visits * 100.0)
									 : 0.0;

				// 寫入檔案：移動 | 訪問數 | 勝場 | 勝率
				tlog << "Move: " << from_s << " -> " << to_s << " | Visits: " << std::setw(5)
					 << child->visits << " | Wins: " << std::setw(6) << std::fixed
					 << std::setprecision(1) << child->wins << " | Rate: " << std::setw(5)
					 << std::fixed << std::setprecision(1) << winRate << "%"
					 << "\n";
			}
			tlog << std::endl;	// 空一行分隔
			tlog.flush();
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