#ifndef ISMCTS_HPP
#define ISMCTS_HPP

#include "4T_GST.hpp"
#include "node.hpp"

// =============================
// ISMCTS (Information Set MCTS) 演算法類別
// =============================
class ISMCTS {
private:
    // 模擬次數（每回合執行的MCTS模擬數）
    int simulations;
    // 隨機數生成器
    std::mt19937 rng;
    // MCTS 樹的根節點
    std::unique_ptr<Node> root;

    // =============================
    // MCTS 四大階段的私有方法
    // =============================
    // 節點選擇（根據 UCB）
    void selection(Node*& node, GST& d);
    // 節點擴展（產生所有合法子節點）
    Node* expansion(Node* node, GST& determinizedState);
    // 隨機模擬（模擬遊戲直到結束，回傳勝負）
    double simulation(GST& state, DATA& d, int root_player);
    // 反向傳播（將結果回傳至路徑上的所有節點）
    void backpropagation(Node* leaf, double result);
    // 計算 UCB 值（平衡探索與利用）
    double calculateUCB(const Node* node) const;
    // 取得確定化狀態（隨機化未知棋子顏色）
    GST getDeterminizedState(const GST& originalState, int current_iteration);
    // 隨機化未知顏色的棋子（早期純隨機，後期根據統計加權）
    void randomizeUnrevealedPieces(GST& state, int current_iteration);
    // 未知棋子排列的統計資料（排列字串 -> <勝場, 次數>）
    std::unordered_map<std::string, std::pair<int, int>> arrangement_stats;

    // =============================
    // 棋盤移動方向常量（上、左、右、下）
    static constexpr int dir_val[4] = {-6, -1, 1, 6}; // 上、左、右、下

public:
    // =============================
    // 建構子：指定模擬次數
    ISMCTS(int simulations);
    // 重置 MCTS 狀態
    void reset();
    // 執行多次模擬，選出最佳移動
    int findBestMove(GST& game, DATA& d);
    // 輸出節點統計資訊（遞迴印出樹狀結構）
    void printNodeStats(const Node* node, int indent = 0) const;
};

#endif // ISMCTS_HPP
