#ifndef NODE_HPP
#define NODE_HPP

#include "4T_GST.hpp"
#include <vector>
#include <memory>

// =============================
// Node 類別：MCTS 樹節點
// =============================
class Node {
public:
    int move;                         // 由父節點到此節點的移動（-1:根節點）
    double wins;                      // 節點累積勝利分數
    int visits;                       // 節點被訪問次數
    std::unordered_map<int,int> avail_cnt;
    Node* parent;                     // 父節點指標
    std::vector<std::unique_ptr<Node>> children; // 子節點列表

    Node(int move = -1);   // 建構子

    // 遞迴清理整棵子樹
    static void cleanup(std::unique_ptr<Node>& node) {
        if (node) {
            // 遞迴清理子節點
            for (auto& child : node->children) {
                cleanup(child);
            }
            // 清理當前節點
            node->children.clear();
            // 重置根節點
            node.reset();
        }
    }
};

#endif // NODE_HPP
