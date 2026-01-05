/**
 * @file BitboardGST.cpp
 * @brief BitboardGST 的實作細節
 */

#include "BitboardGST.hpp"

#include "4T_header.h"
#include "ismcts.hpp"
#include "mcts.hpp"

// ==========================================
// Selection Strategy Configuration
// ==========================================
// SELECTION_MODE = 2 -> Softmax Sampling (Default)
// SELECTION_MODE = 1 -> Linear Weight Sampling (p_i = w_i / Σw)
// SELECTION_MODE = 0 -> Argmax (Greedy)
// Compatibility for old flags: -DUSE_SOFTMAX_SELECTION=1/0 maps to 2/1
#ifndef SELECTION_MODE
#ifdef USE_SOFTMAX_SELECTION
#if USE_SOFTMAX_SELECTION
#define SELECTION_MODE 2
#else
#define SELECTION_MODE 1
#endif
#else
#define SELECTION_MODE 2
#endif
#endif

#if SELECTION_MODE == 2
#pragma message("Compiling with softmax selection")
#elif SELECTION_MODE == 1
#pragma message("Compiling with linear selection")
#else
#pragma message("Compiling with argmax selection")
#endif

// ==========================================
// 【通用轉接頭】(Universal Wrappers)
// 這裡的寫法保證在 C++11 ~ C++20 都能跑，
// 自動偵測是 Mac/Linux (GCC/Clang) 還是 Windows (MSVC)
// ==========================================

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

// ==========================================
// 轉接頭結束，下面是你的邏輯代碼
// ==========================================
static thread_local pcg32 rng(std::random_device{}());

// Helper lambda: Generates double u in [0, 1)
auto next_u01 = []() {
	return static_cast<double>(rng()) / (static_cast<double>(pcg32::max()) + 1.0);
};

// 定義靜態變數
std::vector<PatternInfo> GST::pat_1x4;
std::vector<PatternInfo> GST::pat_2x2;
std::vector<PatternInfo> GST::pat_4x1;

// ==========================================
// 輔助函式 (僅用於初始化階段，複製原本的 Array 邏輯)
// ==========================================
static bool _is_valid(int base_pos, const int* offset) {
	int base_row = base_pos / 6;
	int base_col = base_pos % 6;
	if (offset[1] == 1 && offset[2] == 2) {	 // 1x4
		if (base_col > 2) return false;
	} else if (offset[1] == 1 && offset[2] == 6) {	// 2x2
		if (base_col > 4 || base_row > 4) return false;
	} else if (offset[1] == 6) {  // 4x1
		if (base_row > 2) return false;
	}
	return true;
}

static int _get_loc(int base_pos, const int* offset) {
	int p[4];
	for (int i = 0; i < 4; i++) p[i] = base_pos + offset[i];
	return (p[0] * 46656 + p[1] * 1296 + p[2] * 36 + p[3]);	 // 36^3, 36^2...
}

uint64_t GST::KING_MOVES[36];

// ==========================================
// 1. 靜態 Helper: 狀態特徵提取
// ==========================================
// 建議加上 inline 以便編譯器展開，減少函式呼叫開銷
inline int GST::get_feature_from_state(uint64_t mask, int turn, uint64_t u_red, uint64_t u_blue,
									   uint64_t e_red, uint64_t e_blue) {
	if (turn == 0) {							// User 視角 (User 是 "我")
		if (u_red & mask) return 1;				// My Red
		if (u_blue & mask) return 2;			// My Blue
		if ((e_red | e_blue) & mask) return 3;	// Enemy
		return 0;								// Empty
	} else {									// Enemy 視角 (Enemy 是 "我")
		if (e_red & mask) return 1;				// My Red (對 Enemy 來說)
		if (e_blue & mask) return 2;			// My Blue
		if ((u_red | u_blue) & mask) return 3;	// Enemy (User)
		return 0;
	}
}

// ==========================================
// 2. 核心計算: 針對給定的盤面狀態計算分數
// ==========================================
float GST::compute_weight_from_state(DATA& d, int turn, uint64_t u_red, uint64_t u_blue,
									 uint64_t e_red, uint64_t e_blue) {
	float total_weight = 0.0f;

	// A. 選擇權重表 (LUT Selection)
	// 注意：這裡必須使用「傳入的參數」來計算數量，而不是用 this->my_red
	const float* selected_LUT;

	// 計算棋子數量 (使用 popcount)
	// 根據 C++ 版本，可使用 std::popcount 或 __builtin_popcountll
	int cnt_u_red = popcount64(u_red);
	int cnt_u_blue = popcount64(u_blue);
	int cnt_e_red = popcount64(e_red);
	int cnt_e_blue = popcount64(e_blue);

	if (turn == 0) {  // User Turn
		if (cnt_e_red == 1)
			selected_LUT = d.LUTwr_U_R1;
		else if (cnt_u_blue == 1)
			selected_LUT = d.LUTwr_U_B1;
		else
			selected_LUT = d.LUTwr_U;
	} else {  // Enemy Turn
		if (cnt_u_red == 1)
			selected_LUT = d.LUTwr_E_R1;
		else if (cnt_e_blue == 1)
			selected_LUT = d.LUTwr_E_B1;
		else
			selected_LUT = d.LUTwr_E;
	}

	// B. 遍歷 1x4 Patterns
	// 使用之前建立好的預計算列表 pat_1x4
	for (const auto& p : pat_1x4) {
		// 從參數中提取特徵
		int f0 = get_feature_from_state(1ULL << p.sq[0], turn, u_red, u_blue, e_red, e_blue);
		int f1 = get_feature_from_state(1ULL << p.sq[1], turn, u_red, u_blue, e_red, e_blue);
		int f2 = get_feature_from_state(1ULL << p.sq[2], turn, u_red, u_blue, e_red, e_blue);
		int f3 = get_feature_from_state(1ULL << p.sq[3], turn, u_red, u_blue, e_red, e_blue);

		// 組合特徵索引 (Base-4)
		int feature_idx = (f0 << 6) | (f1 << 4) | (f2 << 2) | f3;

		// 查表並累加
		total_weight += selected_LUT[d.LUT_idx(p.trans_idx, feature_idx)];
	}

	// C. 遍歷 2x2 Patterns
	for (const auto& p : pat_2x2) {
		int f0 = get_feature_from_state(1ULL << p.sq[0], turn, u_red, u_blue, e_red, e_blue);
		int f1 = get_feature_from_state(1ULL << p.sq[1], turn, u_red, u_blue, e_red, e_blue);
		int f2 = get_feature_from_state(1ULL << p.sq[2], turn, u_red, u_blue, e_red, e_blue);
		int f3 = get_feature_from_state(1ULL << p.sq[3], turn, u_red, u_blue, e_red, e_blue);

		int feature_idx = (f0 << 6) | (f1 << 4) | (f2 << 2) | f3;
		total_weight += selected_LUT[d.LUT_idx(p.trans_idx, feature_idx)];
	}

	// D. 遍歷 4x1 Patterns
	for (const auto& p : pat_4x1) {
		int f0 = get_feature_from_state(1ULL << p.sq[0], turn, u_red, u_blue, e_red, e_blue);
		int f1 = get_feature_from_state(1ULL << p.sq[1], turn, u_red, u_blue, e_red, e_blue);
		int f2 = get_feature_from_state(1ULL << p.sq[2], turn, u_red, u_blue, e_red, e_blue);
		int f3 = get_feature_from_state(1ULL << p.sq[3], turn, u_red, u_blue, e_red, e_blue);

		int feature_idx = (f0 << 6) | (f1 << 4) | (f2 << 2) | f3;
		total_weight += selected_LUT[d.LUT_idx(p.trans_idx, feature_idx)];
	}

	return total_weight / (float)TUPLE_NUM;
}

// ==========================================
// 3. 虛擬移動預判 (Predict Move)
// ==========================================
float GST::predict_move_weight(int move, DATA& d) {
	// 0. 特殊檢查：如果是逃脫步 (Escape Move)，直接回傳極高分
	// 因為 N-Tuple 通常只評估盤面，不懂「遊戲結束」的規則
	int to = move & 0xFF;
	if (to == ESCAPE_LEFT_TARGET || to == ESCAPE_RIGHT_TARGET) {
		return 999999.0f;  // 必勝移動，直接最高分
	}

	// A. 快速備份當前狀態到「區域變數」 (Register Copy)
	uint64_t tmp_my_red = my_red;
	uint64_t tmp_my_blue = my_blue;
	uint64_t tmp_emy_red = emy_red;
	uint64_t tmp_emy_blue = emy_blue;

	// B. 在區域變數上模擬移動 (Local Simulation)
	int from = (move >> 8) & 0xFF;
	// to 已經在上面取過了

	uint64_t from_mask = 1ULL << from;
	uint64_t to_mask = 1ULL << to;
	uint64_t move_mask = from_mask | to_mask;

	// 模擬移動邏輯 (Bitwise Operation)
	if (nowTurn == 0) {	 // User Moving
		// 1. 吃子：如果目標格有敵人，把它移除
		tmp_emy_red &= ~to_mask;
		tmp_emy_blue &= ~to_mask;

		// 2. 移動自己：從 from 移到 to
		if (tmp_my_red & from_mask)
			tmp_my_red ^= move_mask;
		else
			tmp_my_blue ^= move_mask;

	} else {  // Enemy Moving
		// 1. 吃子
		tmp_my_red &= ~to_mask;
		tmp_my_blue &= ~to_mask;

		// 2. 移動自己
		if (tmp_emy_red & from_mask)
			tmp_emy_red ^= move_mask;
		else
			tmp_emy_blue ^= move_mask;
	}

	// C. 計算移動後的分數
	// 這裡傳入 nowTurn ^ 1 (換對方)，因為我們模擬了一步，現在輪到對方
	// 這樣 compute_weight 會使用對方的視角來評估盤面 (或使用對應的 LUT)
	// 注意：這取決於原本 highest_weight 的邏輯，如果原本是 do_move 後算分，那這就是正確的。
	return compute_weight_from_state(d, nowTurn, tmp_my_red, tmp_my_blue, tmp_emy_red,
									 tmp_emy_blue);
}

// ==========================================
// 類別方法實作
// ==========================================

void GST::init_board() {
	// 1. 清空狀態
	my_red = my_blue = emy_red = emy_blue = 0;
	nowTurn = 0;  // USER (0)
	winner = -1;
	n_plies = 0;
	is_escape = false;

	// 定義初始位置 (完全對照原版 gst.cpp)
	static const int init_pos[2][8] = {
		{25, 26, 27, 28, 31, 32, 33, 34},  // User (Player 0)
		{10, 9, 8, 7, 4, 3, 2, 1}		   // Enemy (Player 1)
	};

	// ==========================================
	// 2. 決定 User 的顏色分配 (完全復刻原版邏輯)
	// ==========================================
	// 我們需要這兩個陣列來暫存結果，以確保 RNG 呼叫順序不變
	bool user_is_red[8] = {false};
	int red_cnt = 0;

	// 原版邏輯：拒絕採樣 (Rejection Sampling)
	while (red_cnt != 4) {
		int x = rng(8);	 // 呼叫全域的 pcg32 rng
		if (!user_is_red[x]) {
			user_is_red[x] = true;
			red_cnt++;
		}
	}

	// ==========================================
	// 3. 決定 Enemy 的顏色分配 (完全復刻原版邏輯)
	// ==========================================
	bool emy_is_red[8] = {false};
	red_cnt = 0;

	while (red_cnt != 4) {
		int x = rng(8);	 // 繼續呼叫同一個 rng
		if (!emy_is_red[x]) {
			emy_is_red[x] = true;
			red_cnt++;
		}
	}

	// ==========================================
	// 4. 將分配結果填入 Bitboard
	// ==========================================

	// 填入 User 棋子
	for (int i = 0; i < 8; ++i) {
		int pos = init_pos[0][i];
		if (user_is_red[i]) {
			my_red |= (1ULL << pos);  // 是紅棋
		} else {
			my_blue |= (1ULL << pos);  // 是藍棋
		}
	}

	// 填入 Enemy 棋子
	for (int i = 0; i < 8; ++i) {
		int pos = init_pos[1][i];
		if (emy_is_red[i]) {
			emy_red |= (1ULL << pos);  // 是紅棋
		} else {
			emy_blue |= (1ULL << pos);	// 是藍棋
		}
	}
}

int GST::gen_all_move(int* move_arr) {
	int count = 0;
	uint64_t us = (nowTurn == 0) ? (my_red | my_blue) : (emy_red | emy_blue);
	uint64_t temp_us = us;

	// 1. 一般移動
	while (temp_us) {
		int from = bit_scan_forward(temp_us);  // 找出棋子位置 (等同 bit_scan_forward)
		temp_us &= temp_us - 1;				   // 移除這顆棋子

		// 【精華】直接查表，並扣除自己人的位置
		// KING_MOVES[from]: 這顆棋子能去哪
		// ~us: 不能是自己人的位置
		uint64_t valid_targets = KING_MOVES[from] & ~us;

		// 遍歷所有合法目標 (沒有 if 判斷了！)
		while (valid_targets) {
			int to = bit_scan_forward(valid_targets);
			valid_targets &= valid_targets - 1;

			move_arr[count++] = (from << 8) | to;
		}
	}

	// ------------------------------------------------
	// 特殊移動：藍棋逃脫
	// ------------------------------------------------

	constexpr uint64_t MASK_POS_0 = 1ULL << 0;
	constexpr uint64_t MASK_POS_5 = 1ULL << 5;
	constexpr uint64_t MASK_POS_30 = 1ULL << 30;
	constexpr uint64_t MASK_POS_35 = 1ULL << 35;

	if (nowTurn == 0) {	 // User Turn
		// 檢查 Pos 0 (直接 AND Mask，非 0 即為真)
		if (my_blue & MASK_POS_0) {
			move_arr[count++] = (0 << 8) | ESCAPE_LEFT_TARGET;
		}
		// 檢查 Pos 5
		if (my_blue & MASK_POS_5) {
			move_arr[count++] = (5 << 8) | ESCAPE_RIGHT_TARGET;
		}
	} else {  // Enemy Turn
		// 檢查 Pos 30
		if (emy_blue & MASK_POS_30) {
			move_arr[count++] = (30 << 8) | ESCAPE_LEFT_TARGET;
		}
		// 檢查 Pos 35
		if (emy_blue & MASK_POS_35) {
			move_arr[count++] = (35 << 8) | ESCAPE_RIGHT_TARGET;
		}
	}

	return count;
}

bool GST::check_win_move(int move) {
	int to = move & 0xFF;

	if (to == ESCAPE_LEFT_TARGET || to == ESCAPE_RIGHT_TARGET) {
		return true;
	}

	return false;
}

void GST::do_move(int move) {
	// 確保只取低 16 位作為基礎移動資訊 (過濾掉可能殘留的舊 flag)
	int from = (move >> 8) & 0xFF;
	int to = move & 0xFF;
	int captured_type = 0;	// 0: None, 1: Red, 2: Blue

	// 準備 Bitmasks
	uint64_t from_mask = 1ULL << from;
	uint64_t move_mask = 0;	 // 稍後計算

	// ==========================================
	// 1. 處理特殊勝利 (Escape)
	// ==========================================
	if (check_win_move(move)) {
		winner = nowTurn;
		is_escape = true;

		// 記錄這是一個逃脫步 (雖然沒吃子，但為了 undo 邏輯一致)
		// 這裡不需要特別標記 capture，因為逃脫一定沒吃子

		if (nowTurn == 0)
			my_blue ^= from_mask;
		else
			emy_blue ^= from_mask;

		// 【關鍵修正】即使結束了也要存入歷史，這樣搜尋樹才能 backtrack (undo)
		history[n_plies++] = move;
		nowTurn ^= 1;
		return;
	}

	// ==========================================
	// 2. 偵測並處理吃子 (Capture Detection)
	// ==========================================
	uint64_t to_mask = 1ULL << to;
	move_mask = from_mask | to_mask;

	if (nowTurn == 0) {	 // User Turn
		// 檢查目標格是否有敵方紅棋
		if (emy_red & to_mask) {
			captured_type = 1;	 // 記住：吃了紅棋
			emy_red ^= to_mask;	 // 移除它 (XOR 1->0)
		}
		// 檢查目標格是否有敵方藍棋
		else if (emy_blue & to_mask) {
			captured_type = 2;	  // 記住：吃了藍棋
			emy_blue ^= to_mask;  // 移除它
		}
		// 移動己方棋子
		if (my_red & from_mask)
			my_red ^= move_mask;
		else
			my_blue ^= move_mask;

	} else {  // Enemy Turn
		// 檢查目標格是否有我方紅棋
		if (my_red & to_mask) {
			captured_type = 1;
			my_red ^= to_mask;
		} else if (my_blue & to_mask) {
			captured_type = 2;
			my_blue ^= to_mask;
		}

		// 移動敵方棋子
		if (emy_red & from_mask)
			emy_red ^= move_mask;
		else
			emy_blue ^= move_mask;
	}

	// ==========================================
	// 3. 將「吃子資訊」打包進 move 並存入歷史
	// ==========================================
	// 將 captured_type 放在第 16, 17 bit (<< 16)
	int recorded_move = move | (captured_type << 16);

	history[n_plies++] = recorded_move;
	nowTurn ^= 1;
}

void GST::undo() {
	// 1. 還原基本狀態
	winner = -1;
	is_escape = false;
	nowTurn ^= 1;  // 切換回上一個玩家
	n_plies--;

	// 2. 取出最後一步
	int move = history[n_plies];
	int from = (move >> 8) & 0xFF;
	int to = move & 0xFF;
	int captured_type = (move >> 16) & 0x3;	 // 取出第 16-17 bit (0, 1, 2)

	uint64_t from_mask = 1ULL << from;

	// ==========================================
	// 3. 處理特殊勝利 (Escape) 的倒帶
	// ==========================================
	if (to == ESCAPE_LEFT_TARGET || to == ESCAPE_RIGHT_TARGET) {
		// 把逃走的棋子放回 `from`
		if (nowTurn == 0)
			my_blue |= from_mask;
		else
			emy_blue |= from_mask;
		return;
	}

	// ==========================================
	// 4. 處理一般移動的倒帶
	// ==========================================
	uint64_t to_mask = 1ULL << to;
	uint64_t move_mask = from_mask | to_mask;

	// A. 把移動的棋子「瞬移」回去 (從 to 變回 from)
	if (nowTurn == 0) {	 // User Turn
		// 檢查現在 to 位置上是誰，就把它移回 from
		if (my_red & to_mask)
			my_red ^= move_mask;
		else
			my_blue ^= move_mask;
	} else {  // Enemy Turn
		if (emy_red & to_mask)
			emy_red ^= move_mask;
		else
			emy_blue ^= move_mask;
	}

	// B. 復活被吃掉的棋子 (如果有)
	if (captured_type != 0) {
		// 如果是 User 剛走完這步，那被吃的一定是 Enemy (emy_*)
		// 如果是 Enemy 剛走完這步，那被吃的一定是 User (my_*)

		uint64_t* target_board = nullptr;

		if (nowTurn == 0) {	 // User 剛剛走了這步，吃了 Enemy
			if (captured_type == 1)
				target_board = &emy_red;
			else
				target_board = &emy_blue;
		} else {  // Enemy 剛剛走了這步，吃了 User
			if (captured_type == 1)
				target_board = &my_red;
			else
				target_board = &my_blue;
		}

		// 復活！ (XOR 0->1)
		*target_board ^= to_mask;
	}
}

bool GST::is_over() {
	if (n_plies >= 200) {
		winner = -2;  // Draw (Rule: 200 plies limit)
		return true;
	}
	if (winner != -1) return true;

	// 使用我們自定義的通用函式 popcount64
	int u_red = popcount64(my_red);
	int u_blue = popcount64(my_blue);
	int e_red = popcount64(emy_red);
	int e_blue = popcount64(emy_blue);

	if (u_red == 0 || e_blue == 0) {
		winner = USER;	// User Wins
		return true;
	}
	if (e_red == 0 || u_blue == 0) {
		winner = ENEMY;	 // Enemy Wins
		return true;
	}

	return false;
}

int GST::get_feature_at(int sq) const {
	uint64_t mask = 1ULL << sq;

	if (nowTurn == 0) {
		if (my_red & mask) return 1;
		if (my_blue & mask) return 2;
		if ((emy_red | emy_blue) & mask) return 3;
		return 0;
	} else {
		if (emy_red & mask) return 1;
		if (emy_blue & mask) return 2;
		if ((my_red | my_blue) & mask) return 3;
		return 0;
	}
}

void GST::print_board() {
	// 符號定義：
	// R: 我方紅 (My Red)
	// B: 我方藍 (My Blue)
	// r: 敵方紅 (Enemy Red) - 用小寫區分
	// b: 敵方藍 (Enemy Blue) - 用小寫區分

	printf("\n   A   B   C   D   E   F\n");	 // 頂部座標
	printf(" +-----------------------+\n");

	for (int row = 0; row < ROW; row++) {
		printf("%d|", row);	 // 左側座標

		for (int col = 0; col < COL; col++) {
			int sq = row * COL + col;
			uint64_t mask = 1ULL << sq;

			// 檢查這一格是誰
			if (my_red & mask) {
				printf(" R  ");
			} else if (my_blue & mask) {
				printf(" B  ");
			} else if (emy_red & mask) {
				printf(" r  ");	 // 敵方用小寫
			} else if (emy_blue & mask) {
				printf(" b  ");	 // 敵方用小寫
			} else {
				// 處理空地與出入口符號
				if (row == 0 && col == 0)
					printf(" <  ");	 // 入口
				else if (row == 0 && col == COL - 1)
					printf(" >  ");	 // 出口
				else
					printf(" .  ");	 // 普通空地
			}
		}
		printf("|\n");
	}
	printf(" +-----------------------+\n");

	// ==========================================
	// 統計資訊 (因為 Bitboard 沒有 ID，改印數量)
	// ==========================================

	// 使用之前的 helper 或直接用 popcount64
	int my_r_cnt = popcount64(my_red);
	int my_b_cnt = popcount64(my_blue);
	int emy_r_cnt = popcount64(emy_red);
	int emy_b_cnt = popcount64(emy_blue);

	printf("\n[User Status] (Upper Case)\n");
	printf("  Red Left : %d (Eaten: %d)\n", my_r_cnt, 4 - my_r_cnt);
	printf("  Blue Left: %d (Eaten: %d)\n", my_b_cnt, 4 - my_b_cnt);

	printf("\n[Enemy Status] (Lower Case)\n");
	printf("  Red Left : %d (Eaten: %d)\n", emy_r_cnt, 4 - emy_r_cnt);
	printf("  Blue Left: %d (Eaten: %d)\n", emy_b_cnt, 4 - emy_b_cnt);
	printf("\n");
}

// ==========================================
// Statistics & Utilities
// ==========================================

struct GameStats {
	int total_games;
	// ISMCTS (Player 1) Stats
	int ismcts_wins;
	int ismcts_escape;
	int ismcts_enemy_red;
	int ismcts_enemy_blue;
	int ismcts_total_steps;
	double ismcts_total_times;
	// MCTS (Player 2) Stats
	int mcts_wins;
	int mcts_escape;
	int mcts_enemy_red;
	int mcts_enemy_blue;
	// Draws
	int draws;

	GameStats()
		: total_games(0),
		  mcts_wins(0),
		  mcts_escape(0),
		  mcts_enemy_red(0),
		  mcts_enemy_blue(0),
		  ismcts_total_steps(0),
		  ismcts_total_times(0.0),
		  ismcts_wins(0),
		  ismcts_escape(0),
		  ismcts_enemy_red(0),
		  ismcts_enemy_blue(0),
		  draws(0) {}
};

void print_game_stats(const GameStats& stats) {
	std::cout << "\n===== 統計結果 =====\n";
	std::cout << "總場次: " << stats.total_games << "\n\n";

	std::cout << "ISMCTS 獲勝: " << stats.ismcts_wins << " 場\n";
	std::cout << "  - 藍子逃脫: " << stats.ismcts_escape << " 場\n";
	std::cout << "  - 紅子被吃光: " << stats.ismcts_enemy_red << " 場\n";
	std::cout << "  - 吃光對手藍子: " << stats.ismcts_enemy_blue << " 場\n\n";
	std::cout << "ISMCTS 總思考步數: " << stats.ismcts_total_steps << "\n";
	std::cout << "ISMCTS 總思考時間: " << stats.ismcts_total_times << " ms\n";
	std::cout << "ISMCTS 平均思考時間: " << (stats.ismcts_total_times / stats.ismcts_total_steps)
			  << " ms\n";

	std::cout << "MCTS 獲勝: " << stats.mcts_wins << " 場\n";
	std::cout << "  - 藍子逃脫: " << stats.mcts_escape << " 場\n";
	std::cout << "  - 紅子被吃光: " << stats.mcts_enemy_red << " 場\n";
	std::cout << "  - 吃光對手藍子: " << stats.mcts_enemy_blue << " 場\n\n";

	std::cout << "平局: " << stats.draws << " 場\n";
}

void print_progress_bar(int current, int total) {
	const int bar_width = 50;
	float progress = (float)current / total;
	int pos = bar_width * progress;

	std::cout << "\r[";
	for (int i = 0; i < bar_width; ++i) {
		if (i < pos)
			std::cout << "=";
		else if (i == pos)
			std::cout << ">";
		else
			std::cout << " ";
	}
	std::cout << "] " << int(progress * 100.0) << "% (" << current << "/" << total << ")"
			  << std::flush;
}

// ==========================================
// 初始化：預先計算所有 Lookups
// ==========================================
void GST::init_tuple_tables(DATA& d) {
	// 確保只初始化一次
	if (!pat_1x4.empty()) return;

	static const int off_1x4[4] = {0, 1, 2, 3};
	static const int off_2x2[4] = {0, 1, 6, 7};
	static const int off_4x1[4] = {0, 6, 12, 18};

	// 遍歷所有可能的基底位置 (0~35)
	for (int base = 0; base < 36; base++) {
		// 1. 處理 1x4
		if (_is_valid(base, off_1x4)) {
			PatternInfo p;
			for (int i = 0; i < 4; i++) p.sq[i] = base + off_1x4[i];
			// 關鍵：直接存好查表後的 index，保持檔案相容性
			p.trans_idx = d.trans[_get_loc(base, off_1x4)];
			pat_1x4.push_back(p);
		}

		// 2. 處理 2x2
		if (_is_valid(base, off_2x2)) {
			PatternInfo p;
			for (int i = 0; i < 4; i++) p.sq[i] = base + off_2x2[i];
			p.trans_idx = d.trans[_get_loc(base, off_2x2)];
			pat_2x2.push_back(p);
		}

		// 3. 處理 4x1
		if (_is_valid(base, off_4x1)) {
			PatternInfo p;
			for (int i = 0; i < 4; i++) p.sq[i] = base + off_4x1[i];
			p.trans_idx = d.trans[_get_loc(base, off_4x1)];
			pat_4x1.push_back(p);
		}
	}
}

void GST::init_lookup_tables() {
	// 預先計算每一格的合法移動 (這段只跑一次，慢沒關係)
	for (int sq = 0; sq < 36; sq++) {
		uint64_t mask = 0;
		int r = sq / 6;
		int c = sq % 6;

		if (c > 0) mask |= (1ULL << (sq - 1));	// Left
		if (c < 5) mask |= (1ULL << (sq + 1));	// Right
		if (r > 0) mask |= (1ULL << (sq - 6));	// Up
		if (r < 5) mask |= (1ULL << (sq + 6));	// Down

		KING_MOVES[sq] = mask;
	}
}

// ==========================================
// 執行期：極速權重計算
// ==========================================
float GST::compute_board_weight(DATA& d) {
	float total_weight = 0.0f;

	// 1. 決定要查哪一張權重表 (LUT Selection)
	// 直接用 popcount 取代原本的 piece_nums 陣列
	const float* selected_LUT;

	if (nowTurn == 0) {	 // User
		if (popcount64(emy_red) == 1)
			selected_LUT = d.LUTwr_U_R1;
		else if (popcount64(my_blue) == 1)
			selected_LUT = d.LUTwr_U_B1;
		else
			selected_LUT = d.LUTwr_U;
	} else {  // Enemy
		if (popcount64(my_red) == 1)
			selected_LUT = d.LUTwr_E_R1;
		else if (popcount64(emy_blue) == 1)
			selected_LUT = d.LUTwr_E_B1;
		else
			selected_LUT = d.LUTwr_E;
	}

	// 2. 遍歷 1x4 Patterns
	for (const auto& p : pat_1x4) {
		// 利用 get_feature_at 快速提取特徵 (0,1,2,3)
		// 並組合成 Tuple Feature Index (Base-4 Number)
		int feature_idx = (get_feature_at(p.sq[0]) << 6) |	// * 64
						  (get_feature_at(p.sq[1]) << 4) |	// * 16
						  (get_feature_at(p.sq[2]) << 2) |	// * 4
						  (get_feature_at(p.sq[3]));		// * 1

		// 從 d 取得最終索引: LUT_idx(trans_address, feature)
		// 這裡 d.LUT_idx 通常是: trans_idx * 256 + feature_idx
		// 為了極致效能，可以直接展開:
		int final_idx = d.LUT_idx(p.trans_idx, feature_idx);

		total_weight += selected_LUT[final_idx];
	}

	// 3. 遍歷 2x2 Patterns
	for (const auto& p : pat_2x2) {
		int feature_idx = (get_feature_at(p.sq[0]) << 6) | (get_feature_at(p.sq[1]) << 4) |
						  (get_feature_at(p.sq[2]) << 2) | (get_feature_at(p.sq[3]));

		total_weight += selected_LUT[d.LUT_idx(p.trans_idx, feature_idx)];
	}

	// 4. 遍歷 4x1 Patterns
	for (const auto& p : pat_4x1) {
		int feature_idx = (get_feature_at(p.sq[0]) << 6) | (get_feature_at(p.sq[1]) << 4) |
						  (get_feature_at(p.sq[2]) << 2) | (get_feature_at(p.sq[3]));

		total_weight += selected_LUT[d.LUT_idx(p.trans_idx, feature_idx)];
	}

	return total_weight / (float)TUPLE_NUM;
}

int GST::highest_weight(DATA& d) {
	// 準備權重陣列
	float WEIGHT[MAX_MOVES] = {0};
	// 這裡為了保險起見，設大一點，雖然 gen_all_move 通常不會超過 32
	int root_moves[MAX_MOVES * 2];
	int root_nmove = gen_all_move(root_moves);

	// ==========================================
	// 1. 角落策略 (Corner Heuristics) - Bitboard 版
	// ==========================================

	// 用來儲存：(位置 sq, 角落 ID, 距離 distance)
	std::vector<std::tuple<int, int, int>> pieces_distances;
	pieces_distances.reserve(32);  // 預先分配記憶體

	// 找出當前玩家的所有棋子位置
	uint64_t my_pieces = (nowTurn == 0) ? (my_red | my_blue) : (emy_red | emy_blue);
	uint64_t temp_scan = my_pieces;

	while (temp_scan) {
		int sq = bit_scan_forward(temp_scan);  // 找出棋子位置
		temp_scan &= temp_scan - 1;

		int p_row = sq / 6;
		int p_col = sq % 6;

		// 計算到 4 個角落的曼哈頓距離
		int dist_to_0 = p_row + p_col;
		int dist_to_5 = p_row + (5 - p_col);
		int dist_to_30 = (5 - p_row) + p_col;
		int dist_to_35 = (5 - p_row) + (5 - p_col);

		// 將這顆棋子(sq)對 4 個角落的數據加入列表
		pieces_distances.push_back(std::make_tuple(sq, 0, dist_to_0));
		pieces_distances.push_back(std::make_tuple(sq, 1, dist_to_5));
		pieces_distances.push_back(std::make_tuple(sq, 2, dist_to_30));
		pieces_distances.push_back(std::make_tuple(sq, 3, dist_to_35));
	}

	// 排序：距離越短的越優先
	std::sort(pieces_distances.begin(), pieces_distances.end(),
			  [](const std::tuple<int, int, int>& a, const std::tuple<int, int, int>& b) {
				  return std::get<2>(a) < std::get<2>(b);
			  });

	// 分配狀態追蹤
	// 原本是用 piece_idx (0~15)，這裡改用 pos (0~35) 來標記某個位置的棋子是否已分配
	bool pos_assigned[36];
	bool corner_assigned[4];
	// Map: [Position 0~35] -> AssignedCornerID (0~3)
	int assigned_corner_for_pos[36];

	std::fill(std::begin(pos_assigned), std::end(pos_assigned), false);
	std::fill(std::begin(corner_assigned), std::end(corner_assigned), false);
	std::fill(std::begin(assigned_corner_for_pos), std::end(assigned_corner_for_pos), -1);

	// 執行貪婪分配
	for (const auto& tuple : pieces_distances) {
		int sq = std::get<0>(tuple);
		int corner = std::get<1>(tuple);

		if (!pos_assigned[sq] && !corner_assigned[corner]) {
			pos_assigned[sq] = true;
			corner_assigned[corner] = true;
			assigned_corner_for_pos[sq] = corner;  // 記住：位於 sq 的棋子負責去 corner
		}

		if (corner_assigned[0] && corner_assigned[1] && corner_assigned[2] && corner_assigned[3]) {
			break;
		}
	}

	// ==========================================
	// 2. 評估每一種走法
	// ==========================================
	for (int m = 0; m < root_nmove; m++) {
		int move_index = m;
		int mv = root_moves[m];
		int from = (mv >> 8) & 0xFF;
		int to = mv & 0xFF;

		// 算出方向 (為了相容舊邏輯的寫死判斷)
		// dir_val[4] = {-COL, -1, 1, COL} -> 0:N, 1:W, 2:E, 3:S
		// 簡單逆推一下：
		int diff = to - from;
		int direction = -1;
		if (diff == -6)
			direction = 0;	// Up
		else if (diff == -1)
			direction = 1;	// Left
		else if (diff == 1)
			direction = 2;	// Right
		else if (diff == 6)
			direction = 3;	// Down

		// 判斷該棋子顏色 (用於特殊規則)
		bool is_my_blue = (nowTurn == 0) ? (my_blue & (1ULL << from)) : (emy_blue & (1ULL << from));
		bool is_my_red = (nowTurn == 0) ? (my_red & (1ULL << from)) : (emy_red & (1ULL << from));

		// ---------------------------------------------------
		// 特殊寫死規則 (Hardcoded Heuristics) - Bitboard 版
		// ---------------------------------------------------

		// 規則 1: 藍棋已經在門口，且往外走 -> 必勝 (權重 1.0)
		// pos[piece] == 0 && direction == 1 (Left) && nowTurn == USER && board[0] == BLUE
		if (from == 0 && direction == 1 && nowTurn == 0 && is_my_blue) {
			WEIGHT[move_index] = 1;
		} else if (from == 5 && direction == 2 && nowTurn == 0 && is_my_blue) {
			WEIGHT[move_index] = 1;
		} else if (from == 30 && direction == 1 && nowTurn == 1 && is_my_blue) {
			WEIGHT[move_index] = 1;
		} else if (from == 35 && direction == 2 && nowTurn == 1 && is_my_blue) {
			WEIGHT[move_index] = 1;
		}
		// 規則 2: 藍棋準備衝門 (Checkmate Setups)
		// pos == 4, move Right(2), USER, Blue. If board[5] empty & board[11] occupied(blocked?)
		// 原意：如果 5 是空的，且 11 有棋子(可能擋住敵人?)，就衝過去
		else if (from == 4 && direction == 2 && nowTurn == 0 && is_my_blue) {
			// board[5] == 0: 檢查 (my_all | emy_all) & (1<<5) 是否為 0
			bool pos5_empty = !((my_red | my_blue | emy_red | emy_blue) & (1ULL << 5));
			// board[11] >= 0: 原意是 User(>=0) 或 Empty(0)。也就是沒有敵人(<0)。
			// Bitboard: !((emy_red | emy_blue) & (1<<11))
			bool pos11_safe = !((emy_red | emy_blue) & (1ULL << 11));

			if (pos5_empty && pos11_safe)
				WEIGHT[move_index] = 1;
			else
				goto GENERAL_EVAL;
		} else if (from == 1 && direction == 1 && nowTurn == 0 && is_my_blue) {
			bool pos0_empty = !((my_red | my_blue | emy_red | emy_blue) & (1ULL << 0));
			bool pos6_safe = !((emy_red | emy_blue) & (1ULL << 6));

			if (pos0_empty && pos6_safe)
				WEIGHT[move_index] = 1;
			else
				goto GENERAL_EVAL;
		}
		// Enemy mirror cases
		else if (from == 34 && direction == 2 && nowTurn == 1 && is_my_blue) {
			bool pos35_empty = !((my_red | my_blue | emy_red | emy_blue) & (1ULL << 35));
			// board[29] <= 0: Enemy or Empty. i.e., No User.
			bool pos29_safe = !((my_red | my_blue) & (1ULL << 29));

			if (pos35_empty && pos29_safe)
				WEIGHT[move_index] = 1;
			else
				goto GENERAL_EVAL;
		} else if (from == 31 && direction == 1 && nowTurn == 1 && is_my_blue) {
			bool pos30_empty = !((my_red | my_blue | emy_red | emy_blue) & (1ULL << 30));
			bool pos24_safe = !((my_red | my_blue) & (1ULL << 24));

			if (pos30_empty && pos24_safe)
				WEIGHT[move_index] = 1;
			else
				goto GENERAL_EVAL;
		} else {
		GENERAL_EVAL:
			// ---------------------------------------------------
			// 一般評估：使用快速預判 (Predict Move)
			// ---------------------------------------------------
			// 這取代了原本的 do_move -> compute -> undo
			WEIGHT[move_index] = predict_move_weight(mv, d);
		}

		// ---------------------------------------------------
		// 套用角落獎勵 (Corner Bonus)
		// ---------------------------------------------------
		int dst = to;
		int row = dst / 6;
		int col = dst % 6;

		int d0 = row + col;
		int d5 = row + (5 - col);
		int d30 = (5 - row) + col;
		int d35 = (5 - row) + (5 - col);

		float corner_bonus = 1.0;

		// 檢查這顆棋子(from)是否有被指派任務
		if (assigned_corner_for_pos[from] != -1) {
			int assigned_corner = assigned_corner_for_pos[from];
			int current_dist_val = 999;

			// 計算移動前的距離
			int s_row = from / 6;
			int s_col = from % 6;

			if (assigned_corner == 0) {
				current_dist_val = s_row + s_col;
				if (d0 < current_dist_val) corner_bonus = 1.01;	 // 變近了！
			} else if (assigned_corner == 1) {
				current_dist_val = s_row + (5 - s_col);
				if (d5 < current_dist_val) corner_bonus = 1.01;
			} else if (assigned_corner == 2) {
				current_dist_val = (5 - s_row) + s_col;
				if (d30 < current_dist_val) corner_bonus = 1.01;
			} else if (assigned_corner == 3) {
				current_dist_val = (5 - s_row) + (5 - s_col);
				if (d35 < current_dist_val) corner_bonus = 1.01;
			}
		}

		WEIGHT[move_index] *= corner_bonus;

		// 最後一項微調：如果敵方紅棋少於等於1，且目標格是空的，鼓勵佔地
		// piece_nums[2] -> count_pieces(2) (Enemy Red)
		// board[dst] == 0 -> !((all_pieces) & (1<<dst))
		bool dst_empty = !((my_red | my_blue | emy_red | emy_blue) & (1ULL << dst));
		if (popcount64(emy_red) <= 1 && dst_empty) {
			WEIGHT[move_index] *= 1.01;
		}
	}

	// ==========================================
	// 3. 選擇策略 (Selection Logic) - 完全復刻
	// ==========================================
	float max_weight = -std::numeric_limits<float>::infinity();
	float min_weight = std::numeric_limits<float>::infinity();
	std::vector<int> best_candidates;
	best_candidates.reserve(root_nmove);

	for (int i = 0; i < root_nmove; ++i) {
		const float wi = WEIGHT[i];
		if (!(wi == wi)) continue;	// Skip NaN
		if (wi > max_weight) {
			max_weight = wi;
			best_candidates.clear();
			best_candidates.push_back(i);
		} else if (wi == max_weight) {
			best_candidates.push_back(i);
		}
		if (wi < min_weight) {
			min_weight = wi;
		}
	}

	int best_idx = -1;
	if (!best_candidates.empty()) {
		// 使用你的全域 RNG
		best_idx = best_candidates[rng(best_candidates.size())];
	}
	if (best_idx < 0) {
		best_idx = 0;  // Fallback
		max_weight = 0.0f;
		min_weight = 0.0f;
	}

	int chosen_idx = best_idx;	// Default to argmax

#if SELECTION_MODE == 2
	// Softmax Probability Sampling
	const double temperature = 1.0;
	const double T = std::max(1e-9, temperature);
	std::vector<double> probs(root_nmove, 0.0);
	double sumProb = 0.0;
	for (int i = 0; i < root_nmove; i++) {
		double wi = static_cast<double>(WEIGHT[i]);
		if (!(wi == wi)) {	// NaN -> 0
			probs[i] = 0.0;
			continue;
		}
		double v = std::exp((wi - static_cast<double>(max_weight)) / T);
		if (!std::isfinite(v)) v = 0.0;
		probs[i] = v;
		sumProb += v;
	}
	if (sumProb > 0.0 && std::isfinite(sumProb)) {
		double u = next_u01();	// 呼叫你的 lambda
		double target = u * sumProb;
		double acc = 0.0;
		for (int i = 0; i < root_nmove; i++) {
			acc += probs[i];
			if (target < acc) {
				chosen_idx = i;
				break;
			}
		}
		if (chosen_idx < 0) chosen_idx = best_idx;
	}
#elif SELECTION_MODE == 1
	// Linear Weight Sampling (Shift negative values)
	const double shift = (min_weight < 0.0f) ? -static_cast<double>(min_weight) : 0.0;
	std::vector<double> w(root_nmove, 0.0);
	double sumW = 0.0;
	for (int i = 0; i < root_nmove; i++) {
		double wi = static_cast<double>(WEIGHT[i]);
		if (!(wi == wi)) {	// NaN -> 0
			w[i] = 0.0;
			continue;
		}
		double vi = wi + shift;
		if (vi < 0.0) vi = 0.0;
		w[i] = vi;
		sumW += vi;
	}
	if (sumW > 0.0 && std::isfinite(sumW)) {
		double u = next_u01();
		double target = u * sumW;
		double acc = 0.0;
		for (int i = 0; i < root_nmove; i++) {
			acc += w[i];
			if (target < acc) {
				chosen_idx = i;
				break;
			}
		}
		if (chosen_idx < 0) chosen_idx = best_idx;
	}
#else
	// Argmax (Already calculated in best_idx)
#endif

	// Final safety check
	if (chosen_idx < 0 || chosen_idx >= root_nmove) chosen_idx = best_idx;
	return root_moves[chosen_idx];
}

// ==========================================
// Main Application Entry
// ==========================================

DATA data;

int main() {
	std::random_device rd;
	std::uniform_int_distribution<> dist(0, 7);

	int num_games;
	std::cout << "請輸入要進行的遊戲場數: ";
	std::cin >> num_games;

	data.init_data();
	data.read_data_file(500000);

	GameStats stats;
	stats.total_games = num_games;

	if (num_games > 1) {
		std::cout << "\n開始進行多場遊戲模擬...\n";
	}

	for (int game_num = 1; game_num <= num_games; game_num++) {
		GST game;
		MCTS mcts(5000);
		ISMCTS ismcts(5000);
		game.init_lookup_tables();
		game.init_tuple_tables(data);
		bool my_turn = true;

		// Reset all states
		game.init_board();
		mcts.reset();
		ismcts.reset();

		if (num_games == 1) {
			std::cout << "\n===== 遊戲開始 =====\n\n";
			game.print_board();
		} else {
			print_progress_bar(game_num - 1, num_games);
		}

		// Main Game Loop
		while (!game.is_over()) {
			if (my_turn) {
				if (num_games == 1) std::cout << "Player 1 (ISMCTS) 思考中...\n";
				stats.ismcts_total_steps++;
				auto start = std::chrono::steady_clock::now();

				int move = ismcts.findBestMove(game, data);

				auto end = std::chrono::steady_clock::now();
				stats.ismcts_total_times +=
					std::chrono::duration<double, std::milli>(end - start).count();
				if (move == -1) break;
				game.do_move(move);
			} else {
				if (num_games == 1) std::cout << "Player 2 (MCTS) 思考中...\n";
				int move = mcts.findBestMove(game);
				if (move == -1) break;
				game.do_move(move);
			}

			if (num_games == 1) {
				game.print_board();
				std::cout << "當前回合數: " << game.n_plies << std::endl;
			}

			my_turn = !my_turn;
		}

		// Update statistics based on result
		int winner = game.get_winner();
		if (winner == -2) {
			stats.draws++;
		} else if (winner == USER) {
			stats.ismcts_wins++;
			if (game.is_escape) {
				stats.ismcts_escape++;
			} else if (popcount64(game.my_red) == 0) {
				stats.ismcts_enemy_red++;
			} else if (popcount64(game.emy_blue) == 0) {
				stats.ismcts_enemy_blue++;
			}
		} else if (winner == ENEMY) {
			stats.mcts_wins++;
			if (game.is_escape) {
				stats.mcts_escape++;
			} else if (popcount64(game.emy_red) == 0) {
				stats.mcts_enemy_red++;
			} else if (popcount64(game.my_blue) == 0) {
				stats.mcts_enemy_blue++;
			}
		}

		// Only show detailed output for single game
		if (num_games == 1) {
			if (winner == -2) {
				printf("遊戲結束！達到200回合，判定為平局！\n");
			} else {
				printf("遊戲結束！%s 獲勝！\n", winner ? "Player 2 (MCTS)" : "Player 1 (ISMCTS)");
				if (game.is_escape) {
					printf("勝利方式：藍色棋子成功逃脫！\n");
				} else if (popcount64(game.my_red) == 0) {
					printf("勝利方式：Player 1 的紅色棋子全部被吃光！\n");
				} else if (popcount64(game.emy_red) == 0) {
					printf("勝利方式：Player 2 的紅色棋子全部被吃光！\n");
				} else if (popcount64(game.my_blue) == 0) {
					printf("勝利方式：Player 1 的藍色棋子全部被吃光！\n");
				} else if (popcount64(game.emy_blue) == 0) {
					printf("勝利方式：Player 2 的藍色棋子全部被吃光！\n");
				}
			}
		}
	}

	// Complete the progress bar and print final stats
	if (num_games > 1) {
		print_progress_bar(num_games, num_games);
		std::cout << "\n\n";
		print_game_stats(stats);
	}

	return 0;
}