#ifndef GST_HPP
#define GST_HPP

#include "4T_header.h"

class DATA;

// 共用參數
constexpr double EXPLORATION_PARAM = 1.414;	 // UCB 探索參數

// 前向聲明
class ISMCTS;  // ISMCTS 演算法類別
class MCTS;	   // MCTS 演算法類別

// =============================
// GST (Game State) 棋盤狀態類別
// =============================
class GST {
	friend class ISMCTS;
	friend class MCTS;
	friend int main();	// 允許 main 直接存取私有成員

   private:
	int board[ROW * COL];				  // 棋盤格子顏色（0:空, 1:紅, 2:藍, -1:敵方紅, -2:敵方藍）
	int piece_board[ROW * COL];			  // 棋盤格子上的棋子編號（-1:無棋子, 0~15:棋子編號）
	int pos[PIECES * 2];				  // 棋子位置（0~7:我方, 8~15:敵方）
	int color[PIECES * 2];				  // 棋子顏色（1:紅, 2:藍, -1:敵方紅, -2:敵方藍）
	int piece_nums[4];					  // 棋子數量（0:我紅, 1:我藍, 2:敵紅, 3:敵藍）
	int nowTurn;						  // 當前輪到哪一方（USER/ENEMY）
	int winner;							  // 勝利者（USER/ENEMY/-1:未分勝負）
	bool is_escape = false;				  // 是否有逃脫
	bool revealed[PIECES * 2] = {false};  // 棋子是否已揭示顏色

	int history[1000];	// 歷史移動紀錄
	int n_plies;		// 已下步數

   public:
	// =============================
	// 棋盤初始化、顯示、移動、判斷結束等主要方法
	// =============================
	void init_board();												   // 初始化棋盤
	void print_board();												   // 印出棋盤狀態
	int gen_all_move(int* move_arr);								   // 產生所有合法移動
	int gen_move(int* move_arr, int piece, int location, int& count);  // 產生指定棋子的合法移動
	void do_move(int move);											   // 執行移動
	void undo();													   // 回復到上一步
	bool is_over();													   // 判斷遊戲是否結束
	bool is_revealed(int piece) const { return revealed[piece]; }	   // 查詢棋子是否已揭示

	bool is_valid_pattern(int base_pos, const int* offset);	 // 檢查4-tuple pattern是否合法
	int get_loc(int base_pos, const int* offset);			 // 取得4-tuple pattern的位置編碼
	int get_feature_unknown(int base_pos, const int* offset,
							const int* feature_cache);	// 取得4-tuple pattern的特徵編碼
	float get_weight(int base_pos, const int* offset, DATA& d,
					 const int* feature_cache);	 // 取得4-tuple pattern的權重
	float compute_board_weight(DATA&);			 // 計算整個棋盤的平均權重
	int highest_weight(DATA&);
	// 取得權重最高的合法移動
	int get_color(int piece) const { return color[piece]; }					// 取得棋子顏色
	int get_pos(int piece) const { return pos[piece]; }						// 取得棋子位置
	void set_color(int piece, int new_color) { color[piece] = new_color; }	// 設定棋子顏色

	int get_winner() { return this->winner; }	// 取得勝利者
	int get_nplies() { return this->n_plies; }	// 取得已下步數

	// MCTS 作弊用：取得所有棋子顏色
	const int* get_full_colors() const { return color; }
	// ISMCTS 查詢revealed狀態
	const bool* get_revealed() const { return revealed; }
};

#endif