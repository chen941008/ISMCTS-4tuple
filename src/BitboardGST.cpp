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
static thread_local pcg32 rng(0);

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
static bool _is_valid_logical(int base_pos, const int* offset) {
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

uint64_t GST::KING_MOVES[64];

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
	if (!(((to & 7) + 1) & 6)) {  // 逃脫位置
		return 1.0f;			  // 必勝移動，直接最高分
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
		{42, 43, 44, 45, 50, 51, 52, 53},  // User (Player 0)
		{21, 20, 19, 18, 13, 12, 11, 10}   // Enemy (Player 1)
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

	constexpr uint64_t MASK_POS_9 = 1ULL << 9;
	constexpr uint64_t MASK_POS_14 = 1ULL << 14;
	constexpr uint64_t MASK_POS_49 = 1ULL << 49;
	constexpr uint64_t MASK_POS_54 = 1ULL << 54;

	if (nowTurn == 0) {	 // User Turn
		// 檢查 Pos 0 (直接 AND Mask，非 0 即為真)
		if (my_blue & MASK_POS_9) {
			move_arr[count++] = (9 << 8) | 8;
		}
		// 檢查 Pos 5
		if (my_blue & MASK_POS_14) {
			move_arr[count++] = (14 << 8) | 15;
		}
	} else {  // Enemy Turn
		// 檢查 Pos 30
		if (emy_blue & MASK_POS_49) {
			move_arr[count++] = (49 << 8) | 48;
		}
		// 檢查 Pos 35
		if (emy_blue & MASK_POS_54) {
			move_arr[count++] = (54 << 8) | 55;
		}
	}

	return count;
}

bool GST::check_win_move(int move) {
	int to = move & 0xFF;

	if (!(((to & 7) + 1) & 6)) {
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
	if (!(((to & 7) + 1) & 6)) {
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

	printf("\n   A   B   C   D   E   F\n");	 // 頂部座標 (A-F 對應 Col 0-5)
	printf(" +-----------------------+\n");

	// 【修改 1】迴圈只跑 0 到 5 (共 6 行)
	for (int row = 0; row < 6; row++) {
		printf("%d|", row);	 // 左側座標 (0-5)

		// 【修改 2】迴圈只跑 0 到 5 (共 6 列)
		for (int col = 0; col < 6; col++) {
			// 【關鍵轉換】
			// 顯示座標 (row, col) 對應到 物理座標 (row+1, col+1)
			// 物理 Index = (物理 Row) * 8 + (物理 Col)
			int sq = (row + 1) * 8 + (col + 1);

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
				printf(" .  ");	 // 普通空地 (不再印護城河的 < > 符號)
			}
		}
		printf("|\n");
	}
	printf(" +-----------------------+\n");

	// ==========================================
	// 統計資訊 (保持不變)
	// ==========================================

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
		  ismcts_wins(0),
		  ismcts_escape(0),
		  ismcts_enemy_red(0),
		  ismcts_enemy_blue(0),
		  ismcts_total_steps(0),
		  ismcts_total_times(0.0),
		  mcts_wins(0),
		  mcts_escape(0),
		  mcts_enemy_red(0),
		  mcts_enemy_blue(0),
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

// 邏輯座標 (0~35) -> 物理座標 (0~63)
// 邏輯 (r, c) -> 物理 (r+1, c+1) -> Index: (r+1)*8 + (c+1)
static inline int _to_phy(int logical_sq) {
	int r = logical_sq / 6;
	int c = logical_sq % 6;
	return (r + 1) * 8 + (c + 1);
}

// ==========================================
// 初始化：預先計算所有 Lookups
// ==========================================
void GST::init_tuple_tables(DATA& d) {
	if (!pat_1x4.empty()) return;

	// 【保持不變】這裡的 Offset 必須維持 6x6 的邏輯！
	// 因為 _get_loc 函式是用來算 pattern ID 去查 d.trans 的，
	// 而 d.trans 是用 6x6 邏輯訓練的。
	static const int off_1x4[4] = {0, 1, 2, 3};
	static const int off_2x2[4] = {0, 1, 6, 7};
	static const int off_4x1[4] = {0, 6, 12, 18};

	// 【保持不變】遍歷邏輯基底 (0~35)
	for (int base = 0; base < 36; base++) {
		// Lambda: 封裝重複邏輯，避免寫錯
		auto process_pattern = [&](const int* logical_offset, std::vector<PatternInfo>& out_vec) {
			// 使用前面改名過的 _is_valid_logical (檢查 6x6 邊界)
			if (_is_valid_logical(base, logical_offset)) {
				PatternInfo p;

				// 1. 查權重 (Brain)：使用「邏輯座標」計算 Pattern ID
				// 這樣才能跟 DATA 裡的舊權重對上
				p.trans_idx = d.trans[_get_loc(base, logical_offset)];

				// 2. 存座標 (Body)：【必須修改】轉成「物理座標」存起來
				// 這樣 compute_board_weight 跑迴圈時，才能直接拿 p.sq 去對 8x8 Bitboard 做位元運算
				for (int i = 0; i < 4; i++) {
					// logical_sq = base + logical_offset[i]
					// 轉成 physical (8x8)
					p.sq[i] = _to_phy(base + logical_offset[i]);
				}

				out_vec.push_back(p);
			}
		};

		// 處理三種 Pattern
		process_pattern(off_1x4, pat_1x4);
		process_pattern(off_2x2, pat_2x2);
		process_pattern(off_4x1, pat_4x1);
	}
}

void GST::init_lookup_tables() {
	// 【修正 1】範圍擴大為 0 ~ 63 (8x8)
	for (int sq = 0; sq < 64; sq++) {
		uint64_t mask = 0;

		// 【修正 2】座標計算基於寬度 8
		int r = sq / 8;	 // Row 0~7
		int c = sq % 8;	 // Col 0~7

		// 【修正 3】位移量調整 (上下變成 +/- 8)

		// Left (向左 -1)
		// 只要不是在第 1 行 (Col 1)，就可以往左
		if (c > 1) mask |= (1ULL << (sq - 1));

		// Right (向右 +1)
		// 只要不是在第 6 行 (Col 6)，就可以往右
		if (c < 6) mask |= (1ULL << (sq + 1));

		// Up (向上 -8)
		// 只要不是在第 1 列 (Row 1)，就可以往上
		if (r > 1) mask |= (1ULL << (sq - 8));

		// Down (向下 +8)
		// 只要不是在第 6 列 (Row 6)，就可以往下
		if (r < 6) mask |= (1ULL << (sq + 8));

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
	float WEIGHT[MAX_MOVES] = {0};
	int root_nmove;
	int root_moves[MAX_MOVES];
	root_nmove = gen_all_move(root_moves);

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

		int p_row = sq / 8;
		int p_col = sq % 8;

		// 計算到 4 個角落的曼哈頓距離
		int dist_to_0 = p_row + p_col;
		int dist_to_5 = p_row + (7 - p_col);
		int dist_to_30 = (7 - p_row) + p_col;
		int dist_to_35 = (7 - p_row) + (7 - p_col);

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

	// 【修正】範圍擴大為 64，因為 sq 是物理座標 (0~63)
	bool pos_assigned[64];

	bool corner_assigned[4];  // 這個不用改，因為角落只有 4 個

	// 【修正】Map: [Position 0~63] -> AssignedCornerID (0~3)
	int assigned_corner_for_pos[64];

	// 初始化 (std::begin/end 會自動抓到新的大小，所以這裡不用動)
	std::fill(std::begin(pos_assigned), std::end(pos_assigned), false);
	std::fill(std::begin(corner_assigned), std::end(corner_assigned), false);
	std::fill(std::begin(assigned_corner_for_pos), std::end(assigned_corner_for_pos), -1);

	// 執行貪婪分配 (邏輯不用變)
	for (const auto& tuple : pieces_distances) {
		int sq = std::get<0>(tuple);  // 這裡是 0~63
		int corner = std::get<1>(tuple);

		if (!pos_assigned[sq] && !corner_assigned[corner]) {
			pos_assigned[sq] = true;
			corner_assigned[corner] = true;
			assigned_corner_for_pos[sq] = corner;
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

		if (diff == -8)		// 【修正 2】原本是 -6，現在 8x8 改成 -8
			direction = 0;	// Up
		else if (diff == -1)
			direction = 1;	// Left (包含一般向左 & 左邊逃脫)
		else if (diff == 1)
			direction = 2;	 // Right (包含一般向右 & 右邊逃脫)
		else if (diff == 8)	 // 【修正 3】原本是 6，現在 8x8 改成 8
			direction = 3;	 // Down

		// 判斷該棋子顏色 (用於特殊規則)
		bool is_my_blue = (nowTurn == 0) ? (my_blue & (1ULL << from)) : (emy_blue & (1ULL << from));
		bool is_my_red = (nowTurn == 0) ? (my_red & (1ULL << from)) : (emy_red & (1ULL << from));
		bool is_special_move = false;

		// ---------------------------------------------------
		// 特殊寫死規則 (Hardcoded Heuristics) - Bitboard 版
		// ---------------------------------------------------

		// 規則 1: 藍棋已經在門口，且往外走 -> 必勝 (權重 1.0)
		// pos[piece] == 0 && direction == 1 (Left) && nowTurn == USER && board[0] == BLUE
		if (from == 9 && direction == 1 && nowTurn == 0 && is_my_blue) {
			WEIGHT[move_index] = 1.0f;
			is_special_move = true;
		} else if (from == 14 && direction == 2 && nowTurn == 0 && is_my_blue) {
			WEIGHT[move_index] = 1.0f;
			is_special_move = true;
		} else if (from == 49 && direction == 1 && nowTurn == 1 && is_my_blue) {
			WEIGHT[move_index] = 1.0f;
			is_special_move = true;
		} else if (from == 54 && direction == 2 && nowTurn == 1 && is_my_blue) {
			WEIGHT[move_index] = 1.0f;
			is_special_move = true;
		}
		// ---------------------------------------------------
		// 規則 2: 藍棋準備衝門 (Checkmate Setups) - 8x8 修正版
		// ---------------------------------------------------

		// Case A: User (上) 在 (1,5) 準備往右衝向 (1,6) [舊座標 4->5]
		// From: 13 (1,5), Dir: 2 (Right)
		else if (from == 13 && direction == 2 && nowTurn == 0 && is_my_blue) {
			// 檢查目標 (1,6) 是否為空 [Index 14]
			bool pos14_empty = !((my_red | my_blue | emy_red | emy_blue) & (1ULL << 14));
			// 檢查下方威脅 (2,6) 是否安全 [Index 22]
			bool pos22_safe = !((emy_red | emy_blue) & (1ULL << 22));

			if (pos14_empty && pos22_safe) {
				WEIGHT[move_index] = 1.0f;
				is_special_move = true;
			}
		}
		// Case B: User (上) 在 (1,2) 準備往左衝向 (1,1) [舊座標 1->0]
		// From: 10 (1,2), Dir: 1 (Left)
		else if (from == 10 && direction == 1 && nowTurn == 0 && is_my_blue) {
			// 檢查目標 (1,1) 是否為空 [Index 9]
			bool pos9_empty = !((my_red | my_blue | emy_red | emy_blue) & (1ULL << 9));
			// 檢查下方威脅 (2,1) 是否安全 [Index 17]
			bool pos17_safe = !((emy_red | emy_blue) & (1ULL << 17));

			if (pos9_empty && pos17_safe) {
				WEIGHT[move_index] = 1.0f;
				is_special_move = true;
			}
		}
		// Case C: Enemy (下) 在 (6,5) 準備往右衝向 (6,6) [舊座標 34->35]
		// From: 53 (6,5), Dir: 2 (Right)
		else if (from == 53 && direction == 2 && nowTurn == 1 && is_my_blue) {
			// 檢查目標 (6,6) 是否為空 [Index 54]
			bool pos54_empty = !((my_red | my_blue | emy_red | emy_blue) & (1ULL << 54));
			// 檢查上方威脅 (5,6) 是否安全 [Index 46]
			bool pos46_safe = !((my_red | my_blue) & (1ULL << 46));

			if (pos54_empty && pos46_safe) {
				WEIGHT[move_index] = 1.0f;
				is_special_move = true;
			}
		}
		// Case D: Enemy (下) 在 (6,2) 準備往左衝向 (6,1) [舊座標 31->30]
		// From: 50 (6,2), Dir: 1 (Left)
		else if (from == 50 && direction == 1 && nowTurn == 1 && is_my_blue) {
			// 檢查目標 (6,1) 是否為空 [Index 49]
			bool pos49_empty = !((my_red | my_blue | emy_red | emy_blue) & (1ULL << 49));
			// 檢查上方威脅 (5,1) 是否安全 [Index 41]
			bool pos41_safe = !((my_red | my_blue) & (1ULL << 41));

			if (pos49_empty && pos41_safe) {
				WEIGHT[move_index] = 1.0f;
				is_special_move = true;
			}
		}

		// ===================================================
		// 一般評估 (General Eval)
		// ===================================================
		// 這裡不再用 else 包住，也不用 goto 跳過來
		// 只要上面沒有任何規則將 is_special_move 設為 true，就會執行這裡
		if (!is_special_move) {
			WEIGHT[move_index] = predict_move_weight(mv, d);
		}

		// ---------------------------------------------------
		// 套用角落獎勵 (Corner Bonus)
		// ---------------------------------------------------
		int dst = to;
		int row = dst / 8;
		int col = dst % 8;

		int d0 = row + col;
		int d5 = row + (7 - col);
		int d30 = (7 - row) + col;
		int d35 = (7 - row) + (7 - col);

		float corner_bonus = 1.0;

		// 檢查這顆棋子(from)是否有被指派任務
		if (assigned_corner_for_pos[from] != -1) {
			int assigned_corner = assigned_corner_for_pos[from];
			int current_dist_val = 999;

			// 計算移動前的距離
			int s_row = from / 8;
			int s_col = from % 8;
			if (assigned_corner == 0) {
				current_dist_val = s_row + s_col;
				if (d0 < current_dist_val) corner_bonus = 1.01;	 // 變近了！
			} else if (assigned_corner == 1) {
				current_dist_val = s_row + (7 - s_col);
				if (d5 < current_dist_val) corner_bonus = 1.01;
			} else if (assigned_corner == 2) {
				current_dist_val = (7 - s_row) + s_col;
				if (d30 < current_dist_val) corner_bonus = 1.01;
			} else if (assigned_corner == 3) {
				current_dist_val = (7 - s_row) + (7 - s_col);
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
	/*
	// ================================================================
	// [新增] Bitboard Debug Log: 記錄所有候選步的權重與路徑
	// ================================================================
	{
		static std::ofstream wlog("bitboard_weight_debug_log.txt", std::ios::out | std::ios::app);

		if (wlog.is_open()) {
			wlog << "--------------------------------------------------\n";
			wlog << "Turn: " << (nowTurn == 0 ? "USER (Player 0)" : "ENEMY (Player 1)") << "\n";

			for (int i = 0; i < root_nmove; ++i) {
				int m = root_moves[i];
				int from = (m >> 8) & 0xFF;	 // Bitboard Move Format: High 8 bits = From
				int to = m & 0xFF;			 // Bitboard Move Format: Low 8 bits = To

				// 判斷移動棋子的顏色 (為了 Log 中標示 ESCAPE)
				// 這裡重新抓一次顏色，確保沒錯
				bool is_blue_piece = false;
				if (nowTurn == 0) {
					if (my_blue & (1ULL << from)) is_blue_piece = true;
				} else {
					if (emy_blue & (1ULL << from)) is_blue_piece = true;
				}

				// --- 座標轉字串 Helper ---
				// --- 座標轉字串 Helper ---
				auto to_str = [&](int idx, bool is_dest) -> std::string {
					// 1. 特殊處理：Bitboard 的特殊 Escape Target 座標 (位於護城河)
					if (is_dest) {
						if (idx == 8 || idx == 48) return "ESC_L";	 // (1,0) (6,0)
						if (idx == 15 || idx == 55) return "ESC_R";	 // (1,7) (6,7)
					}

					// 2. 取得物理行列 (0~7)
					int p_row = idx / 8;
					int p_col = idx % 8;

					// 3. 轉回邏輯行列 (0~5)
					// 物理盤面的有效區域是 1~6，所以要減 1 才是人類習慣的 A0~F5
					int l_row = p_row - 1;
					int l_col = p_col - 1;

					// 4. 邊界檢查 (確保是有效棋盤格)
					// 原本 idx >= 36 是舊邏輯，現在物理 index 最大到 63
					// 但我們只關心 l_row, l_col 是否在 0~5 之間
					if (l_row < 0 || l_row > 5 || l_col < 0 || l_col > 5) return "OUT";

					// 5. 格式化字串 (A~F + 0~5)
					char c = 'A' + l_col;
					return std::string(1, c) + std::to_string(l_row);
				};

				std::string from_s = to_str(from, false);
				std::string to_s = to_str(to, true);

				// 算出方向 ID 方便對照 (雖非必要但好讀)
				int diff = to - from;
				int dir_id = -1;
				if (diff == -8)
					dir_id = 0;	 // N
				else if (diff == -1)
					dir_id = 1;	 // W
				else if (diff == 1)
					dir_id = 2;	 // E
				else if (diff == 8)
					dir_id = 3;	 // S

				wlog << "Move: " << from_s << " -> " << to_s << " | Dir: " << dir_id
					 << " | Weight: " << std::fixed << std::setprecision(5) << WEIGHT[i] << "\n";
			}
			wlog.flush();
		}
	}
	*/
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

// 輔助函式：將移動和棋盤狀態寫入檔案
void log_to_file(std::ofstream& file, GST& game, int move, int player_id, int turn_count) {
	if (!file.is_open()) return;

	// 1. 解析移動 (Decode Move)
	int from = (move >> 8) & 0xFF;
	int to = move & 0xFF;

	// 轉換座標顯示 (例如 0 -> A0)
	auto to_coord = [](int pos) -> std::string {
		if (pos == 8 || pos == 48) return "ESC_L";	// 8x8 特殊位置
		if (pos == 15 || pos == 55) return "ESC_R";

		int p_row = pos / 8;  // 改成 8
		int p_col = pos % 8;  // 改成 8

		// 轉回閱讀用的 A0~F5 (0~5)
		// 物理座標 (1,1) -> 邏輯 (0,0) -> 'A' + 0, '0' + 0
		char col_char = 'A' + (p_col - 1);
		int row_num = p_row - 1;

		return std::string(1, col_char) + std::to_string(row_num);
	};

	std::string player_name = (player_id == 0) ? "Player 1 (ISMCTS)" : "Player 2 (MCTS)";

	file << "========================================\n";
	file << "回合: " << turn_count << " | " << player_name << "\n";
	file << "移動: " << to_coord(from) << " -> " << to_coord(to) << "\n";
	file << "----------------------------------------\n";

	// 2. 繪製棋盤 (複製原本 print_board 的邏輯，但改用 file <<)
	file << "   A   B   C   D   E   F\n";
	file << " +-----------------------+\n";

	for (int row = 1; row <= 6; row++) {
		file << (row - 1) << "|";
		for (int col = 1; col <= 6; col++) {
			int sq = row * 8 + col;
			uint64_t mask = 1ULL << sq;

			if (game.my_red & mask)
				file << " R  ";
			else if (game.my_blue & mask)
				file << " B  ";
			else if (game.emy_red & mask)
				file << " r  ";
			else if (game.emy_blue & mask)
				file << " b  ";
			else {
				if (row == 0 && col == 0)
					file << " <  ";
				else if (row == 0 && col == 5)
					file << " >  ";
				else
					file << " .  ";
			}
		}
		file << "|\n";
	}
	file << " +-----------------------+\n";

	// 3. 顯示剩餘棋子資訊
	// 這裡需要用 popcount64，如果在 main 裡沒有定義，可能需要用 __builtin_popcountll 或手動呼叫
	// 假設你可以存取 popcount64 (通常在 helper header 裡)
	// 如果編譯報錯說找不到 popcount64，可以暫時拿掉下面這幾行
	/*
	file << "My(Red/Blue): " << popcount64(game.my_red) << "/" << popcount64(game.my_blue) << "\n";
	file << "Emy(Red/Blue): " << popcount64(game.emy_red) << "/" << popcount64(game.emy_blue) <<
	"\n";
	*/
	file << "\n";
	file.flush();  // 確保立即寫入
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

	std::ofstream logFile("game_log.txt", std::ios::out | std::ios::trunc);

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
		if (logFile.is_open()) {
			logFile << "\n****************************************\n";
			logFile << "Game #" << game_num << " Start\n";
			logFile << "****************************************\n\n";
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
				log_to_file(logFile, game, move, 0, game.n_plies + 1);
				game.do_move(move);
			} else {
				if (num_games == 1) std::cout << "Player 2 (MCTS) 思考中...\n";
				int move = mcts.findBestMove(game);
				if (move == -1) break;
				log_to_file(logFile, game, move, 0, game.n_plies + 1);
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
		if (logFile.is_open()) {
			int w = game.get_winner();
			logFile << "Game Over! Winner: "
					<< (w == -2 ? "Draw" : (w == 0 ? "Player 1" : "Player 2")) << "\n";
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