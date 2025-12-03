#ifndef MCTS_HPP
#define MCTS_HPP

#include "4T_GST.hpp"
#include "4T_header.h"
#include "node.hpp"

// =============================
// MCTS 類別：標準蒙地卡羅樹搜尋
// =============================
class MCTS {
   private:
	int simulations;			 // 模擬次數
	std::mt19937 rng;			 // 隨機數生成器
	std::unique_ptr<Node> root;	 // 樹根節點

	// MCTS 四大階段
	void selection(Node*& node, GST& state);							  // 節點選擇
	void expansion(Node* node, GST& state);								  // 節點擴展
	int simulation(GST& state);											  // 隨機模擬
	void backpropagation(Node* node, int result);						  // 反向傳播
	double calculateUCB(const Node* node) const;						  // 計算 UCB
	bool would_eat_enemy_red(const GST& game, int piece, int dst) const;  // 是否吃掉敵方紅棋

	// 棋盤移動方向常量（上、左、右、下）
	static constexpr int dir_val[4] = {-6, -1, 1, 6};

   public:
	explicit MCTS(int simulations);	 // 建構子
	void reset();					 // 重置 MCTS 狀態
	int findBestMove(GST& game);	 // 執行多次模擬，選出最佳移動
};

#endif	// MCTS_HPP
