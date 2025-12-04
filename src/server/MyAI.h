#ifndef MYAI_INCLUDED
#define MYAI_INCLUDED

#include "../4T_DATA.hpp"
#include "../4T_header.h"

using std::stoi;
using std::string;

#define BOARD_SIZE 6
#define COMMAND_NUM 7

// =============================
// MyAI 類別：AI 伺服器主體
// =============================
class MyAI {
	// 指令名稱列表
	const char* commands_name[COMMAND_NUM] = {"name", "version", "time_setting", "board_setting",
											  "ini",  "get",	 "exit"};

   public:
	MyAI(void);	  // 建構子
	~MyAI(void);  // 解構子

	// 伺服器指令處理
	void Ini(const char* data[], char* response);	// 初始化
	void Set(char* response);						// 設定紅棋
	void Get(const char* data[], char* response);	// 取得移動
	void Exit(const char* data[], char* response);	// 離開

   private:
	// 棋子存在狀態
	bool p1_exist[PIECES], p2_exist[PIECES];
	int player;							// 玩家編號
	int p1_time, p2_time;				// 玩家時間
	int board_size;						// 棋盤大小
	int dice;							// 骰子
	int board[BOARD_SIZE][BOARD_SIZE];	// 棋盤
	int p1_piece_num, p2_piece_num;		// 棋子數量
	char piece_colors[PIECES * 2];		// 棋子顏色
	int piece_pos[PIECES * 2];			// 棋子位置

	// Board 操作
	void Init_board_state(char* position);	// 初始化棋盤狀態
	void Set_board(char* position);			// 設定棋盤
	void Print_chessboard();				// 印出棋盤
	void Generate_move(char* move);			// 產生移動
};

#endif