#define _CRT_RAND_S

#include "gst.hpp"
#include "ismcts.hpp"
#include "mcts.hpp" 
#include <random>

#include <cmath>    // exp, isfinite
#include <limits>   // numeric_limits

// 選擇策略（編譯期旗標）：
// SELECTION_MODE = 2 -> softmax 抽樣（預設）
// SELECTION_MODE = 1 -> 線性權重抽樣 p_i = w_i / Σw（負值自動平移）
// SELECTION_MODE = 0 -> 直接取最高分（argmax）
// 相容舊旗標：-DUSE_SOFTMAX_SELECTION=1/0 分別對應 softmax/線性
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

// 放在函式內最上面（或檔案區域）：一次播種、整段重用
static thread_local pcg32 rng(std::random_device{}());
// 產生 u ∈ [0,1)
auto next_u01 = []() {
    return static_cast<double>(rng()) / (static_cast<double>(pcg32::max()) + 1.0);
};

// =============================
// 靜態變數：棋子、方向、初始位置、pattern offset
// =============================
static std::map<char, int> piece_index = {
    {'A', 0}, {'B', 1}, {'C', 2}, {'D', 3}, {'E', 4}, {'F', 5}, {'G', 6}, {'H', 7},
    {'a', 8}, {'b', 9}, {'c', 10}, {'d', 11}, {'e', 12}, {'f', 13}, {'g', 14}, {'h', 15}
}; // 棋子字元對應編號
static std::map<char, int> dir_index = {{'N', 0}, {'W', 1}, {'E', 2}, {'S', 3}}; // 方向字元對應編號
static std::map<int, char> print_piece = {
    {0, 'A'}, {1, 'B'}, {2, 'C'}, {3, 'D'}, {4, 'E'}, {5, 'F'}, {6, 'G'}, {7, 'H'},
    {8, 'a'}, {9, 'b'}, {10, 'c'}, {11, 'd'}, {12, 'e'}, {13, 'f'}, {14, 'g'}, {15, 'h'}
}; // 棋子編號對應字元
static const int init_pos[2][PIECES] = {{25,26,27,28,31,32,33,34}, {10,9,8,7,4,3,2,1}}; // 初始位置
static const int dir_val[4] = {-COL, -1, 1, COL}; // 方向偏移量
static const int offset_1x4[4] = {0, 1, 2, 3};    // 橫向 1x4 pattern
static const int offset_2x2[4] = {0, 1, 6, 7};    // 2x2 pattern
static const int offset_4x1[4] = {0, 6, 12, 18};  // 縱向 4x1 pattern

// =============================
// GST::init_board
// 初始化棋盤、隨機分配紅棋
// =============================
void GST::init_board(){
    /*
        A  B  C  D  E  F  G  H  a  b  c  d  e  f  g  h
        0  1  2  3  4  5  6  7  8  9 10 11 12 13 14 15  <-piece index, used in pos and color
       25 26 27 28 31 32 33 34 10  9  8  7  4  3  2  1  <-position on board
    */

    memset(board, 0, sizeof(board));
    memset(pos, 0, sizeof(pos));
    memset(revealed, false, sizeof(revealed));
    for(int i=0; i<ROW*COL; i++) piece_board[i] = -1;
    for(int i=0; i<4; i++) piece_nums[i] = 4;
    for(int i=0; i<PIECES; i++) {   //set color
        color[i] = BLUE;
        color[i+8] = -BLUE;
    }
    nowTurn = USER;
    winner = -1;
    n_plies = 0;
    is_escape = false;

    // random set red pieces
    int red_num = 0;        // 現在有幾個紅棋
    bool red_or_not[8];     // 哪個位置已經是紅棋了
    std::fill(std::begin(red_or_not), std::end(red_or_not), false);
    char red[4];
    char red2[4];

    while(red_num != 4){
        int x = rng(8);
        if(!red_or_not[x]){
            red[red_num] = print_piece[x];
            red_or_not[x] = true;
            red_num += 1;
        }
    }

    red_num = 0;
    std::fill(std::begin(red_or_not), std::end(red_or_not), false);
    while(red_num != 4){
        int x = rng(8);
        if(!red_or_not[x]){
            red2[red_num] = print_piece[x+8];
            red_or_not[x] = true;
            red_num += 1;
        }
    }
    
    for(int i=0; i<4; i++){
        color[piece_index[red[i]]] = RED;
        color[piece_index[red2[i]]] = -RED;
        
        if (piece_index[red2[i]] >= PIECES) {
            revealed[piece_index[red2[i]]] = true;
        }
    }
    for(int i=PIECES; i<PIECES*2; i++) {
        if(color[i] == -BLUE) {
            revealed[i] = true;
        }
    }

    //set all pieces position and board
    int offset = 0;
    for(int player=0; player<2; player++){
        for(int i=0; i<PIECES; i++){
            board[init_pos[player][i]] = color[i+offset];   //board: record the color in this location(0~3)
            piece_board[init_pos[player][i]] = i + offset;  //piece_booard: write chess number in board, chess number(0~15)/ space(-1)
            pos[i+offset] = init_pos[player][i];    //pos: record the chess index(0~15)
        }
        offset+=8;
    }

    return;
}

// =============================
// GST::print_board
// 印出棋盤、剩餘棋子、被吃棋子
// =============================
void GST::print_board(){
    for (int i = 0; i < ROW; i++) {
        for (int j = 0; j < COL; j++) {
            if(piece_board[i * ROW + j] != -1) 
                printf("%4c", print_piece[piece_board[i * ROW + j]]);
            else if(i == 0 && j == 0) 
                printf("%4c", '<');
            else if(i == 0 && j == COL-1) 
                printf("%4c", '>');
            else 
                printf("%4c", '-');
        }
        printf("\n");
    }
    printf("\n");
    printf("User remaining ghosts: ");
    for(int i=0; i<PIECES; i++){
        if(pos[i] != -1) printf("%c: %s ", print_piece[i], color[i]==RED ? "red":"blue");
    }
    printf("\n");
    printf("Eaten enemy ghosts: ");
    for(int i=8; i<PIECES*2; i++){
        if(pos[i] == -1) printf("%c: %s ", print_piece[i], color[i]==-RED ? "red":"blue");
    }
    printf("\n");
}

// =============================
// GST::gen_move
// 產生指定棋子的所有合法移動
// =============================
int GST::gen_move(int* move_arr, int piece, int location, int& count){  //generate the possible step
    int row = location / ROW;
    int col = location % COL;
    
    if(nowTurn == USER){
        if(row != 0 && board[location-6] <= 0) move_arr[count++] = piece << 4;            //up
        if(row != ROW-1 && board[location+6] <= 0) move_arr[count++] = piece << 4 | 3;    //down
        if(col != 0 && board[location-1] <= 0) move_arr[count++] = piece << 4 | 1;        //left
        if(col != COL-1 && board[location+1] <= 0) move_arr[count++] = piece << 4 | 2;    //right
        if(color[piece] == BLUE){                                                         //exit move  
            if(location == 0) move_arr[count++] = piece << 4 | 1;
            if(location == 5) move_arr[count++] = piece << 4 | 2;
        }
    }else{
        if(row != 0 && board[location-6] >= 0) move_arr[count++] = piece << 4;            
        if(row != ROW-1 && board[location+6] >= 0) move_arr[count++] = piece << 4 | 3;    
        if(col != 0 && board[location-1] >= 0) move_arr[count++] = piece << 4 | 1;        
        if(col != COL-1 && board[location+1] >= 0) move_arr[count++] = piece << 4 | 2;
        if(color[piece] == -BLUE){
            if(location == 30) move_arr[count++] = piece << 4 | 1;
            if(location == 35) move_arr[count++] = piece << 4 | 2;
        }
    }
    return count;   //the number of possible step
}

// =============================
// GST::gen_all_move
// 產生所有合法移動
// =============================
int GST::gen_all_move(int* move_arr){   //gernerate all posibility of chess step
    int count = 0;
    int offset = nowTurn == ENEMY ? PIECES : 0;
    int* nowTurn_pos = pos + offset;

    for(int i=0; i<PIECES; i++){
        if(pos[i+offset] != -1){
            gen_move(move_arr, i+offset, nowTurn_pos[i], count);
        }
    }

    return count;
}

// =============================
// check_win_move
// 判斷移動是否可直接獲勝
// =============================
bool check_win_move(int location, int dir){     //if chess is in corner, check next move will win or not
    if(location == 0 || location == 30) return dir == 1 ? true : false;
    else if(location == 5 || location == 35) return dir == 2 ? true : false;
    return false;
}

// =============================
// GST::do_move
// 執行移動，更新棋盤、吃棋、勝負判斷
// =============================
void GST::do_move(int move){    //move chess
    int piece = move >> 4;
    int direction = move & 0xf;

    if(abs(color[piece]) == BLUE){
        if(check_win_move(pos[piece], direction)){
            winner = nowTurn;
            n_plies++;
            nowTurn ^= 1;
            is_escape = true;
            return;
        }
    }
    if (n_plies == 1000) {
        fprintf(stderr, "cannot do anymore moves\n");
        exit(1);
    }

    int dst = pos[piece] + dir_val[direction];
    //dst: the chess's location after move / pos: the location of chess / dir_val: up down left right
    
    if(board[dst] < 0){     //Enemy's color
        pos[piece_board[dst]] = -1;     //chess is eaten
        move |= piece_board[dst] << 8;
        revealed[piece_board[dst]] = true;
        if(color[piece_board[dst]] == -RED) piece_nums[2] -= 1; //check the remain color of red & blue chess
        else if(color[piece_board[dst]] == -BLUE) piece_nums[3] -= 1;
        else if(color[piece_board[dst]] == -UNKNOWN){}  //先什麼都不做
        else{
            fprintf(stderr, "do_move error, eaten color wrong!\n");
            exit(1);
        }
    }
    else if(board[dst] > 0){    //User's color
        pos[piece_board[dst]] = -1;
        move |= piece_board[dst] << 8;
        if(color[piece_board[dst]] == RED) piece_nums[0] -= 1;
        else if(color[piece_board[dst]] == BLUE) piece_nums[1] -= 1;
        else if(color[piece_board[dst]] == UNKNOWN){}  //先什麼都不做
        else{
            fprintf(stderr, "do_move error, eaten color wrong!\n");
            exit(1);
        }
    }
    else{
        move |= 0x1000;
    }

    board[pos[piece]] = 0;          //set 0 at the location which stay before => space: color = 0
    piece_board[pos[piece]] = -1;   //set 0 at the location which stay before => space: no chess
    board[dst] = color[piece];      //color the chess color at the location after move
    piece_board[dst] = piece;       //set chess number at the location after move
    pos[piece] = dst;               //the location of chess now
    history[n_plies++] = move;
    nowTurn ^= 1; //change player
}

// =============================
// GST::undo
// 回復到上一步
// =============================
void GST::undo(){   //return to last move(use to return status of random move)
    if(winner != -1) winner = -1;
    
    if (n_plies == 0) {
        fprintf(stderr, "no history\n");
        exit(1);
    }
    nowTurn ^= 1; //change player

    int move = history[--n_plies];
    int check_eaten = move >> 12;
    int eaten_piece = (move & 0xfff) >> 8;
    int piece = (move & 0xff) >> 4;
    int direction = move & 0xf;
    int src = pos[piece] - dir_val[direction];  //location of last move

    if(is_escape){
        is_escape = false;
        return;
    }

    if (check_eaten != 0x1) {
        board[pos[piece]] = color[eaten_piece];
        piece_board[pos[piece]] = eaten_piece;
        pos[eaten_piece] = pos[piece];
        if(nowTurn == USER){
            if(color[eaten_piece] == -RED) piece_nums[2] += 1;
            else if(color[eaten_piece] == -BLUE) piece_nums[3] += 1;
            else if(color[eaten_piece] == -UNKNOWN){}  //先什麼都不做
            else{
                fprintf(stderr, "undo error, eaten color wrong!");
                exit(1);
            }
        }else{
            if(color[eaten_piece] == RED) piece_nums[0] += 1;
            else if(color[eaten_piece] == BLUE) piece_nums[1] += 1;
            else if(color[eaten_piece] == UNKNOWN){}  //先什麼都不做
            else{
                fprintf(stderr, "undo error, eaten color wrong!");
                exit(1);
            }
        }
    }
    else{
        board[pos[piece]] = 0;
        piece_board[pos[piece]] = -1;
    } 
    board[src] = color[piece];
    piece_board[src] = piece;
    pos[piece] = src;
}

// =============================
// GST::is_over
// 判斷遊戲是否結束
// =============================
bool GST::is_over(){    //game end or not => the number of remain chess color
    if(n_plies >= 200) {
        winner = -2; // -2表示平局
        return true;
    }
    if(winner != -1) return true;
    else{
        if(piece_nums[0] == 0 || piece_nums[3] == 0){
            winner = USER;
            return true;
        }
        else if(piece_nums[1] == 0 || piece_nums[2] == 0){
            winner = ENEMY;
            return true;
        }
    }
    return false;
}

// int move_index = 0;

// int flat_mc(GST& game, int simu_times){
//     int root_nmove, n_move;
//     int root_moves[MAX_MOVES];
//     int moves[MAX_MOVES];
//     int wins, count;
//     int maxWins = -1;
//     int move_index = 0;
    
//     root_nmove = game.gen_all_move(root_moves);
//     for(int m=0; m<root_nmove; m++){
//         game.do_move(root_moves[m]);
//         //random simu
//         wins = 0;
//         for(int t=0; t<simu_times; t++){
//             count = 0;
//             while(!game.is_over()){
//                 n_move = game.gen_all_move(moves);
//                 int x = rand();
//                 game.do_move(moves[x%n_move]);
//                 count++;
//             }
            
//             if(game.get_winner() == ENEMY){
//                 wins++;
//             }

//             for(int c=0; c<count; c++){
//                 game.undo();
//             }
//         }
//         if(wins > maxWins){
//             maxWins = wins;
//             move_index = m;
//         }
//         printf("move %d, win rate = %f\n", m, (float)wins/(float)simu_times);
//         game.undo();
//     }
//     return root_moves[move_index];
// }

// 使用ismcts替代flat_mc函數
// int ismcts_move(GST& game, int simu_times) {
//     ISMCTS ismcts(simu_times);
//     return ismcts.findBestMove(game);
// }


//新增統計結構
struct GameStats {
    int total_games;
    // ISMCTS (Player 1) 統計
    int ismcts_wins;
    int ismcts_escape;
    int ismcts_enemy_red;
    int ismcts_enemy_blue;
    int ismcts_total_steps;
    double ismcts_total_times;
    // MCTS (Player 2) 統計
    int mcts_wins;
    int mcts_escape;
    int mcts_enemy_red;
    int mcts_enemy_blue;
    // 平局
    int draws;

    GameStats() : total_games(0), mcts_wins(0), mcts_escape(0), mcts_enemy_red(0), mcts_enemy_blue(0), ismcts_total_steps(0), ismcts_total_times(0.0),
                  ismcts_wins(0), ismcts_escape(0), ismcts_enemy_red(0), ismcts_enemy_blue(0), draws(0) {}
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
    std::cout << "ISMCTS 平均思考時間: " << (stats.ismcts_total_times / stats.ismcts_total_steps) << " ms\n";

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
        if (i < pos) std::cout << "=";
        else if (i == pos) std::cout << ">";
        else std::cout << " ";
    }
    std::cout << "] " << int(progress * 100.0) << "% (" << current << "/" << total << ")" << std::flush;
}
// =============================
// GST::is_valid_pattern
// 檢查給定的 pattern 偏移量是否有效
// =============================
bool GST::is_valid_pattern(int base_pos, const int* offset){
    int base_row = base_pos / COL;
    int base_col = base_pos % COL;
    
    if(offset == offset_1x4){
        if(base_col > 2) return false;
    }
    else if(offset == offset_2x2){
        if(base_col > 4 || base_row > 4) return false;
    }
    else if(offset == offset_4x1){
        if(base_row > 2) return false;
    }

    return true;
}
// =============================
// GST::get_loc
// 取得給定位置和偏移量的唯一編碼
// =============================
int GST::get_loc(int base_pos, const int* offset){ 
    int position[4];
    for(int i = 0; i < 4; i++){
        position[i] = base_pos + offset[i];
    }
    return (position[0] * 36 * 36 * 36 + position[1] * 36 * 36 + position[2] * 36 + position[3]);
}
// =============================
// GST::get_feature_unknown
// 獲取給定位置和偏移量的特徵編碼
// =============================
int GST::get_feature_unknown(int base_pos, const int* offset){           // the piece color
    int features[4];
    for(int i = 0; i < 4; i++){
        int pos = base_pos + offset[i];
        if(nowTurn == USER){
            features[i] = (board[pos] < 0) ? 3 : board[pos];
        }
        else{
            features[i] = (board[pos] > 0) ? 3 : -board[pos];
        }
    }
    return (features[0] * 64 + features[1] * 16 + features[2] * 4 + features[3]);
}
// =============================
// GST::get_weight
// 獲取給定位置和偏移量的權重值
// =============================
float GST::get_weight(int base_pos, const int* offset, DATA& d){
    int feature = get_feature_unknown(base_pos, offset);
    int LUTidx = d.LUT_idx(d.trans[get_loc(base_pos, offset)], feature);
    float weight = 0;
    if(nowTurn == USER){
        if(piece_nums[2] == 1){   // E R = 1
            weight = (float)(d.LUTw_U_R1[LUTidx]) / (float)(d.LUTv_U_R1[LUTidx]);
            // printf("U R1 using\n");
        }else if(piece_nums[1] == 1){   // U B = 1
            weight = (float)(d.LUTw_U_B1[LUTidx]) / (float)(d.LUTv_U_B1[LUTidx]);
        }else{
            weight = (float)(d.LUTw_U[LUTidx]) / (float)(d.LUTv_U[LUTidx]);
        }
    }else{
        if(piece_nums[0] == 1){   // U R = 1
            weight = (float)(d.LUTw_E_R1[LUTidx]) / (float)(d.LUTv_E_R1[LUTidx]);
            // printf("E R1 using\n");
        }else if(piece_nums[3] == 1){   // E B = 1
            weight = (float)(d.LUTw_E_B1[LUTidx]) / (float)(d.LUTv_E_B1[LUTidx]);
        }else{
            weight = (float)(d.LUTw_E[LUTidx]) / (float)(d.LUTv_E[LUTidx]);
        }
    }

    //printf("location = %d, feature = %d, LUTidx = %d, weight = %f\n", d.trans[get_loc(base_pos, offset)], feature, LUTidx, weight);

    return weight;
}
// =============================
// GST::compute_board_weight
// 計算整個棋盤的權重值
// =============================
float GST::compute_board_weight(DATA& d){
    float total_weight = 0;
    
    for(int pos = 0; pos < ROW * COL; pos++) {
        int row = pos / COL;
        int col = pos % COL;
        
        if(is_valid_pattern(pos, offset_1x4)){
            total_weight += get_weight(pos, offset_1x4, d);
        }
        if(is_valid_pattern(pos, offset_4x1)){
            total_weight += get_weight(pos, offset_4x1, d);
        }
        if(is_valid_pattern(pos, offset_2x2)){
            total_weight += get_weight(pos, offset_2x2, d);
        }
    }

    return total_weight / (float)TUPLE_NUM;
}
// =============================
// GST::highest_weight
// 找出權重最高的移動
// =============================
int GST::highest_weight(DATA& d){
    float WEIGHT[MAX_MOVES] = {0};
    int root_nmove;
    int root_moves[MAX_MOVES];
    root_nmove = gen_all_move(root_moves);

    // ================== 【優化：角落獎勵計算移至迴圈外】 ==================
    // 這整段邏輯 (std::vector, std::sort, 分配任務) 在 highest_weight 函式中
    // 只需要計算 "一次"，而不是 "每評估一步棋m" 都重算一次。
    
    // 存儲每個棋子到各角落的距離
    std::vector<std::tuple<int, int, int>> pieces_distances; // (棋子索引, 角落編號, 距離)
        
    if(nowTurn == USER){
        // 計算所有棋子到各角落的距離
        for(int i = 0; i < PIECES; i++){
            if(pos[i] != -1){
                int p_row = pos[i] / 6;
                int p_col = pos[i] % 6;
                
                // 計算這個棋子到四個角落的距離
                int dist_to_0 = p_row + p_col;
                int dist_to_5 = p_row + (5 - p_col);
                int dist_to_30 = (5 - p_row) + p_col;
                int dist_to_35 = (5 - p_row) + (5 - p_col);
                
                // 添加所有棋子-角落距離組合
                pieces_distances.push_back(std::make_tuple(i, 0, dist_to_0));
                pieces_distances.push_back(std::make_tuple(i, 1, dist_to_5));
                pieces_distances.push_back(std::make_tuple(i, 2, dist_to_30));
                pieces_distances.push_back(std::make_tuple(i, 3, dist_to_35));
            }
        }
    }
    else if(nowTurn == ENEMY){
        for(int i = PIECES; i < PIECES * 2; i++){
            if(pos[i] != -1){
                int p_row = pos[i] / 6;
                int p_col = pos[i] % 6;
                
                // 計算这个棋子到四個角落的距離
                int dist_to_0 = p_row + p_col;
                int dist_to_5 = p_row + (5 - p_col);
                int dist_to_30 = (5 - p_row) + p_col;
                int dist_to_35 = (5 - p_row) + (5 - p_col);
                
                // 添加所有棋子-角落距離組合
                pieces_distances.push_back(std::make_tuple(i, 0, dist_to_0));
                pieces_distances.push_back(std::make_tuple(i, 1, dist_to_5));
                pieces_distances.push_back(std::make_tuple(i, 2, dist_to_30));
                pieces_distances.push_back(std::make_tuple(i, 3, dist_to_35));
            }
        }
    }
        
    // 按距離排序所有棋子-角落組合
    std::sort(pieces_distances.begin(), pieces_distances.end(),
    [](const std::tuple<int, int, int>& a, const std::tuple<int, int, int>& b){
        return std::get<2>(a) < std::get<2>(b);
    });
    
    // 已分配的棋子和角落
    bool piece_assigned[PIECES * 2];
    bool corner_assigned[4];
    int assigned_corner_for_piece[PIECES * 2]; // 關鍵：[棋子編號] -> 該去的角落

    memset(piece_assigned, false, sizeof(piece_assigned));
    memset(corner_assigned, false, sizeof(corner_assigned));
    memset(assigned_corner_for_piece, -1, sizeof(assigned_corner_for_piece));
    
    // 按距離分配棋子到角落
    for(const auto& tuple : pieces_distances) {
        int p_idx = std::get<0>(tuple);
        int corner = std::get<1>(tuple);
        
        if(!piece_assigned[p_idx] && !corner_assigned[corner]) {
            piece_assigned[p_idx] = true;
            corner_assigned[corner] = true;
            assigned_corner_for_piece[p_idx] = corner; // 把任務記下來
        }
        
        if(corner_assigned[0] && corner_assigned[1] && corner_assigned[2] && corner_assigned[3]) {
            break;
        }
    }
    // ================== 【優化：結束】 ==================


    for(int m = 0; m < root_nmove; m++){
        int move_index = m;
        int piece = root_moves[m] >> 4;
        int direction = root_moves[m] & 0xf;
        int src = pos[piece];
        int dst = src + dir_val[direction];  //the position after move

        if(pos[piece] == 0 && direction == 1 && nowTurn == USER && board[0] == BLUE){  // if check_win_move() = true, won't move and return directly
            WEIGHT[move_index] = 1;
        }
        else if(pos[piece] == 5 && direction == 2 && nowTurn == USER && board[5] == BLUE){
            WEIGHT[move_index] = 1;
        }
        else if(pos[piece] == 30 && direction == 1 && nowTurn == ENEMY && board[30] == -BLUE){
            WEIGHT[move_index] = 1;
        }
        else if(pos[piece] == 35 && direction == 2 && nowTurn == ENEMY && board[35] == -BLUE){
            WEIGHT[move_index] = 1;
        }
        else if(pos[piece] == 4 && direction == 2 && nowTurn == USER && color[piece] == BLUE){
            if(board[5] == 0 && board[11] >= 0){
                WEIGHT[move_index] = 1;
            }
        }
        else if(pos[piece] == 1 && direction == 1 && nowTurn == USER && color[piece] == BLUE){
            if(board[0] == 0 && board[6] >= 0){
                WEIGHT[move_index] = 1;
            }
        }
        else if(pos[piece] == 34 && direction == 2 && nowTurn == ENEMY && color[piece] == -BLUE){
            if(board[35] == 0 && board[29] <= 0){
                WEIGHT[move_index] = 1;
            }
        }
        else if(pos[piece] == 31 && direction == 1 && nowTurn == ENEMY && color[piece] == -BLUE){
            if(board[30] == 0 && board[24] <= 0){
                WEIGHT[move_index] = 1;
            }
        }
        else{
            int tmp_color[PIECES * 2];      //去除上帝視角
            for(int i = 0; i < PIECES * 2; i++) tmp_color[i] = color[i];
            if(nowTurn == USER) for(int i = PIECES; i < PIECES * 2; i++) color[i] = -UNKNOWN;
            else for(int i = 0; i < PIECES; i++) color[i] = UNKNOWN;

            do_move(root_moves[m]);
            nowTurn ^= 1;

            WEIGHT[move_index] = compute_board_weight(d);

            nowTurn ^= 1;
            undo();

            for(int i = 0; i < PIECES * 2; i++) color[i] = tmp_color[i];
        }

        //調參數時間
        int row = dst / 6;
        int col = dst % 6;
        
        // 計算目標位置到各角落的距離
        int d0 = row + col;                     // 到 (0,0) 的距離
        int d5 = row + (5 - col);               // 到 (0,5) 的距離
        int d30 = (5 - row) + col;              // 到 (5,0) 的距離
        int d35 = (5 - row) + (5 - col);        // 到 (5,5) 的距離
        
        // 【!!!】 原本在這裡的 std::vector, std::sort, for... 都被移到上面了
        
        // 角落獎勵係數
        float corner_bonus = 1.0;
        
        // 檢查當前棋子是否被分配到某個角落
        // 這裡 "使用" 在迴圈外算好的 assigned_corner_for_piece
        if(assigned_corner_for_piece[piece] != -1) {
            int assigned_corner = assigned_corner_for_piece[piece];
            int current_dist;

            // 計算當前位置到分配角落的距離
            int p_row = src / 6;
            int p_col = src % 6;
            
            if(assigned_corner == 0) {
                current_dist = p_row + p_col;
                // 檢查移動是否更接近分配的角落
                if(d0 < current_dist) {
                    corner_bonus = 1.01;
                }
            } else if(assigned_corner == 1) {
                current_dist = p_row + (5 - p_col);
                if(d5 < current_dist) {
                    corner_bonus = 1.01;
                }
            } else if(assigned_corner == 2) {
                current_dist = (5 - p_row) + p_col;
                if(d30 < current_dist) {
                    corner_bonus = 1.01;
                }
            } else if(assigned_corner == 3) {
                current_dist = (5 - p_row) + (5 - col);
                if(d35 < current_dist) {
                    corner_bonus = 1.01;
                }
            }
        }

        WEIGHT[move_index] *= corner_bonus;

        if(piece_nums[2] <= 1 && board[dst] == 0){
            WEIGHT[move_index] *= 1.01;
        }

        //printf("Move = %d | piece: %c, direction: %d, WEIGHT[]: %f\n\n", move_index, print_piece[piece], direction, WEIGHT[move_index]);
    }

    // 【!!! 保留你新的結尾邏輯 !!!】
    // 共用統計（max/min/argmax）與 RNG
    float max_weight = -std::numeric_limits<float>::infinity();
    float min_weight =  std::numeric_limits<float>::infinity();
    int   best_idx   = -1;

    for (int i = 0; i < root_nmove; ++i) {
        const float wi = WEIGHT[i];
        if (!(wi == wi)) continue; // 跳過 NaN
        if (wi > max_weight) { max_weight = wi; best_idx = i; }
        if (wi < min_weight) { min_weight = wi; }
    }
    if (best_idx < 0) {
        best_idx   = 0;   // 全 NaN 時的保底
        max_weight = 0.0f;
        min_weight = 0.0f;
    }

    int chosen_idx = best_idx; // 預設為 argmax

#if SELECTION_MODE == 2
    // softmax 機率抽樣
    const double temperature = 1.0; // 可視需求調整
    const double T = std::max(1e-9, temperature); // 防 0/負溫度
    std::vector<double> probs(root_nmove, 0.0);
    double sumProb = 0.0;
    for (int i = 0; i < root_nmove; i++) {
        double wi = static_cast<double>(WEIGHT[i]);
        if (!(wi == wi)) { // NaN -> 當 0
            probs[i] = 0.0;
            continue;
        }
        double v = std::exp((wi - static_cast<double>(max_weight)) / T);
        if (!std::isfinite(v)) v = 0.0;
        probs[i] = v;
        sumProb += v;
    }
    if (sumProb > 0.0 && std::isfinite(sumProb)) {
        double u = next_u01();
        double target = u * sumProb;
        double acc = 0.0;
        for (int i = 0; i < root_nmove; i++) {
            acc += probs[i];
            if (target < acc) { chosen_idx = i; break; } // 改成嚴格比較
        }
        if (chosen_idx < 0) chosen_idx = best_idx;
    }
#elif SELECTION_MODE == 1
    // 線性權重抽樣（負數平移，Σw=0 回退 argmax）
    const double shift = (min_weight < 0.0f) ? -static_cast<double>(min_weight) : 0.0;
    std::vector<double> w(root_nmove, 0.0);
    double sumW = 0.0;
    for (int i = 0; i < root_nmove; i++) {
        double wi = static_cast<double>(WEIGHT[i]);
        if (!(wi == wi)) { // NaN -> 當 0
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
            if (target < acc) { chosen_idx = i; break; }
        }
        if (chosen_idx < 0) chosen_idx = best_idx;
    }
#else
    // 直接使用 argmax（已在 best_idx 計算）
#endif

    // 萬一前面沒選到（極少見的邊界），保底 argmax
    if (chosen_idx < 0 || chosen_idx >= root_nmove) chosen_idx = best_idx;
    return root_moves[chosen_idx];
}

DATA data;

int main() {
    std::random_device rd;
    std::mt19937 gen(std::chrono::system_clock::now().time_since_epoch().count());
    std::uniform_int_distribution<> dist(0, 7);
    
    int num_games;
    std::cout << "請輸入要進行的遊戲場數: ";
    std::cin >> num_games;
    
    data.init_data();
    data.read_data_file(500000);
    GameStats stats;
    stats.total_games = num_games;
    
    if (num_games > 1) {
        std::cout << "\n開始進行多場遊戲模擬...\n";
    }
    
    for(int game_num = 1; game_num <= num_games; game_num++) {
        GST game;
        MCTS mcts(5000);
        ISMCTS ismcts(5000);
        bool my_turn = true;
        
        // 重置所有遊戲狀態
        game.init_board();
        mcts.reset();
        ismcts.reset();
        
        if(num_games == 1) {
            std::cout << "\n===== 遊戲開始 =====\n\n";
            game.print_board();
        } else {
            print_progress_bar(game_num - 1, num_games);
        }
        
        while(!game.is_over()) {
            if(my_turn) {
                if(num_games == 1) std::cout << "Player 1 (ISMCTS) 思考中...\n";
                stats.ismcts_total_steps++;
                auto start = std::chrono::steady_clock::now();
                int move = ismcts.findBestMove(game,data);
                auto end = std::chrono::steady_clock::now();
                stats.ismcts_total_times += std::chrono::duration<double, std::milli>(end - start).count();
                if (move == -1) break;
                game.do_move(move);
            } else {
                if(num_games == 1) std::cout << "Player 2 (MCTS) 思考中...\n";
                int move = mcts.findBestMove(game);
                if (move == -1) break;
                game.do_move(move);
            }
            
            if(num_games == 1) {
                game.print_board();
                std::cout << "當前回合數: " << game.n_plies << std::endl;
            }
            
            my_turn = !my_turn;
        }
        
        // Update statistics
        int winner = game.get_winner();
        if(winner == -2) {
            stats.draws++;
        } else if(winner == USER) {
            stats.ismcts_wins++; // 總勝場+1
            if(game.is_escape) {
                stats.ismcts_escape++; // 藍子逃脫+1
            } else if(game.piece_nums[0] == 0) {
                stats.ismcts_enemy_red++; // 我方紅子被吃光+1
            } else if(game.piece_nums[3] == 0) {
                stats.ismcts_enemy_blue++; // 敵方藍子被吃光+1
            }
        } else if(winner == ENEMY) {
            stats.mcts_wins++; // 總勝場+1
            if(game.is_escape) {
                stats.mcts_escape++; // 藍子逃脫+1
            } else if(game.piece_nums[2] == 0) {
                stats.mcts_enemy_red++; // 我方紅子被吃光+1
            } else if(game.piece_nums[1] == 0) {
                stats.mcts_enemy_blue++; // 敵方藍子被吃光+1
            }
        }

        // Only show detailed output for single game
        if(num_games == 1) {
            if(winner == -2) {
                printf("遊戲結束！達到200回合，判定為平局！\n");
            } else {
                printf("遊戲結束！%s 獲勝！\n", winner ? "Player 2 (MCTS)" : "Player 1 (ISMCTS)");
                if(game.is_escape) {
                    printf("勝利方式：藍色棋子成功逃脫！\n");
                } else if(game.piece_nums[0] == 0) {
                    printf("勝利方式：Player 1 的紅色棋子全部被吃光！\n");
                } else if(game.piece_nums[2] == 0) {
                    printf("勝利方式：Player 2 的紅色棋子全部被吃光！\n");
                } else if(game.piece_nums[1] == 0) {
                    printf("勝利方式：Player 1 的藍色棋子全部被吃光！\n");
                } else if(game.piece_nums[3] == 0) {
                    printf("勝利方式：Player 2 的藍色棋子全部被吃光！\n");
                }
            }
        }
    }
    
    // Complete the progress bar and print final stats
    if(num_games > 1) {
        print_progress_bar(num_games, num_games);
        std::cout << "\n\n";
        print_game_stats(stats);
    }
    
    return 0;
}