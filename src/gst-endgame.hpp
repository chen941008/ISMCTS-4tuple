#ifndef GST_HPP
#define GST_HPP

#include "4T_header.h"

class DATA;

// 共用參數
constexpr double EXPLORATION_PARAM = 1.414;	 // UCB 探索參數

// 前向聲明
class ISMCTS;
class MCTS;

// =============================
// GST (Game State) 棋盤狀態類別
// =============================
class GST {
	friend class ISMCTS;
	friend class MCTS;
	friend int main();

   private:
	int board[ROW * COL];				  // 棋盤格子顏色
	int piece_board[ROW * COL];			  // 棋盤格子上的棋子編號
	int pos[PIECES * 2];				  // 棋子位置
	int color[PIECES * 2];				  // 棋子顏色
	int piece_nums[4];					  // 棋子數量
	int nowTurn;						  // 當前輪到哪一方
	int winner;							  // 勝利者
	bool is_escape = false;				  // 是否有逃脫
	bool revealed[PIECES * 2] = {false};  // 棋子是否已揭示顏色

	int history[1000];	// 歷史移動紀錄
	int n_plies;		// 已下步數

	int step;  // 當前步數

   public:
	void init_board();												   // 初始化棋盤
	void print_board();												   // 印出棋盤狀態
	int gen_all_move(int* move_arr);								   // 產生所有合法移動
	int gen_move(int* move_arr, int piece, int location, int& count);  // 產生指定棋子的合法移動
	void do_move(int move);											   // 執行移動
	void undo();													   // 回復到上一步
	bool is_over();													   // 判斷遊戲是否結束
	bool is_revealed(int piece) const { return revealed[piece]; }

	bool is_valid_pattern(int base_pos, const int* offset);	   // 檢查4-tuple pattern是否合法
	int get_loc(int base_pos, const int* offset);			   // 取得4-tuple pattern的位置編碼
	int get_feature_unknown(int base_pos, const int* offset);  // 取得4-tuple pattern的特徵編碼
	float get_weight(int base_pos, const int* offset, DATA&);  // 取得4-tuple pattern的權重
	float compute_board_weight(DATA&);						   // 計算整個棋盤的平均權重
	int highest_weight(DATA&);								   // 取得權重最高的合法移動

	int get_color(int piece) const { return color[piece]; }
	int get_pos(int piece) const { return pos[piece]; }
	void set_color(int piece, int new_color) { color[piece] = new_color; }

	int get_winner() { return this->winner; }
	int get_nplies() { return this->n_plies; }

	// MCTS 作弊用
	const int* get_full_colors() const { return color; }
	// ISMCTS 查詢revealed狀態
	const bool* get_revealed() const { return revealed; }
};

#endif