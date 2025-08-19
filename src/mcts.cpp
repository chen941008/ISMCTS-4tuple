#include "mcts.hpp"
#include <algorithm>
#include <chrono>
#include <limits>
#include <iostream>
#include <iomanip>
#include <cassert>

constexpr int MCTS::dir_val[4];

// =============================
// MCTS 建構子
// =============================
MCTS::MCTS(int simulations) : simulations(simulations) {
    auto seed = std::chrono::high_resolution_clock::now().time_since_epoch().count();
    rng.seed(static_cast<unsigned int>(seed));
}

// =============================
// 重置 MCTS 狀態
// =============================
void MCTS::reset() {
    auto seed = std::chrono::high_resolution_clock::now().time_since_epoch().count();
    rng.seed(static_cast<unsigned int>(seed));
    
    Node::cleanup(root);
}

// =============================
// 檢查是否會吃掉敵方紅棋
// =============================
bool MCTS::would_eat_enemy_red(const GST& game, int piece, int dst) const {
    int target_piece = game.piece_board[dst];
    if (target_piece == -1) return false;

    bool is_enemy = ((piece < PIECES && target_piece >= PIECES) ||
                     (piece >= PIECES && target_piece < PIECES));
    if (!is_enemy) return false;

    int target_color = game.get_color(target_piece);
    return std::abs(target_color) == RED;
}

// =============================
// 選擇階段：根據 UCB 選擇最佳子節點
// =============================
void MCTS::selection(Node*& node, GST& state) {
    while (!state.is_over() && !node->children.empty()) {
        Node* bestChild = nullptr;
        double bestUCB = -std::numeric_limits<double>::infinity();

        for (auto& child : node->children) {
            if (child->visits == 0) {
                node = child.get(); // 立即選擇未訪問節點
                return;
            }
            double ucb = calculateUCB(child.get());
            if (ucb > bestUCB) {
                bestUCB = ucb;
                bestChild = child.get();
            }
        }

        if (bestChild) node = bestChild;
        else break;
    }
}

// =============================
// 擴展階段：產生所有合法子節點
// =============================
void MCTS::expansion(Node* node, GST& state) {
    if (state.is_over()) return;

    int moves[MAX_MOVES];
    int moveCount = state.gen_all_move(moves);

    for (int i = 0; i < moveCount; i++) {
        int move = moves[i];
        int piece = move >> 4;
        int dir = move & 0xf;
        int dst = state.get_pos(piece) + dir_val[dir];
    
        if (would_eat_enemy_red(state, piece, dst)) continue;
    
        GST newState = state;
        newState.do_move(move);
        std::unique_ptr<Node> newNode(new Node(move));
        newNode->parent = node;
        node->children.push_back(std::move(newNode));
    }
}

// =============================
// 模擬階段：隨機模擬遊戲直到結束
// =============================
int MCTS::simulation(GST& state) {
    GST simState = state;
    int moves[MAX_MOVES];
    int moveCount;
    int maxDepth = 1000;
    int depth = 0;
    
    std::uniform_int_distribution<> dist(0, INT_MAX);

    while (!simState.is_over() && depth < maxDepth) {
        moveCount = simState.gen_all_move(moves);
        if (moveCount == 0) break;

        // 純隨機模擬
        int randomIndex = dist(rng) % moveCount;
        simState.do_move(moves[randomIndex]);
        depth++;
    }

    // 遊戲結束狀態
    if (!simState.is_over() && depth >= maxDepth) return 0;
    return simState.get_winner() == ENEMY ? 1 : -1;
}

// =============================
// 反向傳播階段：將模擬結果回傳至路徑上的所有節點
// =============================
void MCTS::backpropagation(Node* node, int result) {
    while (node != nullptr) {
        node->visits += 1;
        node->wins += result;
        result = -result;  // 在每一層交替結果
        node = node->parent;
    }
}

// =============================
// 計算 UCB 值
// =============================
double MCTS::calculateUCB(const Node* node) const {
    if (node->visits == 0) return std::numeric_limits<double>::infinity();

    double exploitation = static_cast<double>(node->wins) / node->visits;
    double exploration = EXPLORATION_PARAM * std::sqrt(std::log(node->parent->visits) / node->visits);

    return exploitation + exploration;
}

// =============================
// 尋找最佳移動 (AI主程式)
// =============================
int MCTS::findBestMove(GST& game) {
    Node::cleanup(root);
    root.reset(new Node());
    
    for (int i = 0; i < simulations; i++) {
        Node* currentNode = root.get();

        // 選擇階段
        selection(currentNode, game);
        
        // 如果節點沒有子節點且遊戲未結束，進行擴展
        if (currentNode->children.empty() && !game.is_over()) {
            expansion(currentNode, game);
        }
        
        // 確保有子節點可選擇
        Node* nodeToSimulate = currentNode;

        // 隨機選擇一個子節點進行模擬
        if (!currentNode->children.empty()) {
            std::uniform_int_distribution<> dist(0, currentNode->children.size() - 1);
            int randomIndex = dist(rng);
            auto it = std::next(currentNode->children.begin(), randomIndex);
            nodeToSimulate = it->get();
        }

        // 執行這個 move
        GST simulationState = game;
        simulationState.do_move(nodeToSimulate->move);
        int result = simulation(simulationState);
        
        // 反向傳播結果
        backpropagation(nodeToSimulate, result);
    }

    Node* bestChild = nullptr;
    int maxVisits = -1;

    // 尋找訪問次數最多的子節點(最佳解)
    for (auto& child : root->children) {
        if (child->visits > maxVisits) {
            maxVisits = child->visits;
            bestChild = child.get();
        }
    }
    
    // 輸出最佳子節點的資訊
    if (bestChild) {
        int piece = bestChild->move >> 4;
        int direction = bestChild->move & 0xf;
        const char* dirNames[] = {"N", "W", "E", "S"};

        std::cout << "MCTS 選擇移動: ";
        if (piece < PIECES) std::cout << static_cast<char>('A' + piece % PIECES);
        else std::cout << static_cast<char>('a' + (piece - PIECES) % PIECES);
        std::cout << " " << dirNames[direction] << std::endl;
    }

    return bestChild ? bestChild->move : -1;
}