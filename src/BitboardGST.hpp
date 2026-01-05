/**
 * @file BitboardGST.hpp
 * @brief 基於 Bitboard (位元棋盤) 的遊戲狀態管理類別
 * @details 使用 uint64_t 的位元運算取代傳統的 int 陣列，大幅提升移動生成與勝負判斷的效能。
 * 適用於 6x6 棋盤 (36 squares)。
 */

#ifndef GST_HPP
#define GST_HPP

#include "4T_DATA.hpp"

// 棋盤遮罩 (只取低 36 位元)
constexpr uint64_t BOARD_MASK = 0xFFFFFFFFF;

// 邊界遮罩：防止左右移動時「穿牆」
// A 行 (Col 0) 的所有位置 (0, 6, 12, 18, 24, 30)
constexpr uint64_t NOT_FILE_A = 0xAAAAAAAAA;  // Binary: ...101010
// F 行 (Col 5) 的所有位置 (5, 11, 17, 23, 29, 35)
constexpr uint64_t NOT_FILE_F = 0x555555555;  // Binary: ...010101

// 特殊移動代碼 (用於逃脫勝利)
// 設定為 60, 61 是因為棋盤只有 0~35，這些數字不會衝突
constexpr int ESCAPE_LEFT_TARGET = 60;
constexpr int ESCAPE_RIGHT_TARGET = 61;

// 用來儲存預先計算好的 Pattern 資訊
struct PatternInfo {
	int sq[4];		// 該 Pattern 包含的 4 個格子位置 (0~35)
	int trans_idx;	// 對應權重檔中的 Address (從 d.trans 查來的)
};

class DATA;

/// @brief Global constant for UCB exploration (Standard value: sqrt(2) approx 1.414)
constexpr double EXPLORATION_PARAM = 1.414;

// Forward declarations
class ISMCTS;
class MCTS;

class GST {
	friend class ISMCTS;
	friend class MCTS;
	friend int main();

   public:
	// ==========================================
	// 核心棋盤狀態 (Core State) - 僅需 40 Bytes
	// ==========================================
	uint64_t my_red = 0;	///< 我方紅棋
	uint64_t my_blue = 0;	///< 我方藍棋
	uint64_t emy_red = 0;	///< 敵方紅棋 (已翻開)
	uint64_t emy_blue = 0;	///< 敵方藍棋 (已翻開)

	// 遊戲進程狀態
	int nowTurn = 0;		 ///< 0: User, 1: Enemy
	int winner = -1;		 ///< -1: None, 0: User, 1: Enemy
	int n_plies = 0;		 ///< 回合數
	bool is_escape = false;	 ///< 是否藉由逃脫獲勝
	int history[1000];		 ///< 移動歷史

	// ==========================================
	// 輔助查詢 (Inline Helpers)
	// ==========================================

	/** @brief 取得所有被佔用的位置 (不分敵我) */
	inline uint64_t occupied() const { return my_red | my_blue | emy_red | emy_blue; }

	/** @brief 取得所有空格 */
	inline uint64_t empty() const { return ~occupied() & BOARD_MASK; }

	/** @brief 取得我方所有棋子 */
	inline uint64_t get_us() const {
		return (nowTurn == 0) ? (my_red | my_blue) : (emy_red | emy_blue);
	}

	/** @brief 取得敵方所有棋子 */
	inline uint64_t get_them() const {
		return (nowTurn == 0) ? (emy_red | emy_blue) : (my_red | my_blue);
	}

	// ==========================================
	// 核心功能方法
	// ==========================================

	/** @brief 初始化棋盤 (隨機佈局請在此實作) */
	void init_board();

	static void init_lookup_tables();

	static uint64_t KING_MOVES[36];

	/** @brief 檢查移動是否獲勝 */
	bool check_win_move(int move);
	/** * @brief 生成所有合法移動
	 * @param move_arr 傳入的陣列指標，用於儲存移動編碼
	 * @return 生成的移動數量
	 */
	int gen_all_move(int* move_arr);

	/**
	 * @brief 執行移動
	 * @param move 移動編碼 (Format: From << 8 | To)
	 */
	void do_move(int move);

	void undo();

	/**
	 * @brief 取得該位置的特徵值 (供 N-Tuple Network 使用)
	 * @param sq 棋盤位置 (0~35)
	 * @return 特徵 ID (0:空, 1:紅, 2:藍, 3:敵/未知)
	 */
	int get_feature_at(int sq) const;

	/** @brief 判斷遊戲是否結束 */
	bool is_over();

	// 為了除錯，可以保留原本的 print
	void print_board();

	static std::vector<PatternInfo> pat_1x4;
	static std::vector<PatternInfo> pat_2x2;
	static std::vector<PatternInfo> pat_4x1;

	// 【新增】初始化函式 (必須在 main 一開始呼叫，並傳入 loaded 的 data)
	static void init_tuple_tables(DATA& d);

	// 【重構】極速版的權重計算
	float compute_board_weight(DATA& d);

	int highest_weight(DATA& d);

	int get_feature_from_state(uint64_t mask, int turn, uint64_t u_red, uint64_t u_blue,
							   uint64_t e_red, uint64_t e_blue);

	float compute_weight_from_state(DATA& d, int turn, uint64_t u_red, uint64_t u_blue,
									uint64_t e_red, uint64_t e_blue);

	float predict_move_weight(int move, DATA& d);

	int get_winner() const { return winner; }
};

#endif