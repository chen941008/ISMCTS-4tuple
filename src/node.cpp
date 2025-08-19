#include "node.hpp"

// =============================
// Node 實現：MCTS 樹節點建構子
// =============================
Node::Node(int move) : 
    move(move),        // 由父節點到此節點的移動
    wins(0),           // 勝利分數初始化
    visits(0),         // 訪問次數初始化
    avail(0),          // availability count初始化
    parent(nullptr)    // 父節點初始化
{}
