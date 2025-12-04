#define _CRT_RAND_S

#include "gst-endgame.hpp"

#include "4T_DATA.hpp"
#include "ismcts.hpp"
#include "mcts.hpp"

// =============================
// 靜態變數：棋子、方向、初始位置、pattern offset
// =============================
static std::map<char, int> piece_index = {{'A', 0},	 {'B', 1},	{'C', 2},  {'D', 3},  {'E', 4},
										  {'F', 5},	 {'G', 6},	{'H', 7},  {'a', 8},  {'b', 9},
										  {'c', 10}, {'d', 11}, {'e', 12}, {'f', 13}, {'g', 14},
										  {'h', 15}};  // 棋子字元對應編號
static std::map<char, int> dir_index = {
	{'N', 0}, {'W', 1}, {'E', 2}, {'S', 3}};  // 方向字元對應編號
static std::map<int, char> print_piece = {{0, 'A'},	 {1, 'B'},	{2, 'C'},  {3, 'D'},  {4, 'E'},
										  {5, 'F'},	 {6, 'G'},	{7, 'H'},  {8, 'a'},  {9, 'b'},
										  {10, 'c'}, {11, 'd'}, {12, 'e'}, {13, 'f'}, {14, 'g'},
										  {15, 'h'}};  // 棋子編號對應字元
static const int init_pos[2][PIECES] = {{25, 26, 27, 28, 31, 32, 33, 34},
										{10, 9, 8, 7, 4, 3, 2, 1}};	 // 初始位置
static const int dir_val[4] = {-COL, -1, 1, COL};					 // 方向偏移量

static const int offset_1x4[4] = {0, 1, 2, 3};	  // 橫向 1x4 pattern
static const int offset_2x2[4] = {0, 1, 6, 7};	  // 2x2 pattern
static const int offset_4x1[4] = {0, 6, 12, 18};  // 縱向 4x1 pattern

// 設置顏色
void SetColor(int color = 7) {
#ifdef _WIN32
	HANDLE hConsole;
	hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
	SetConsoleTextAttribute(hConsole, color);
#else
	switch (color) {
		case 4:
			printf("\033[31m");
			break;	// RED
		case 9:
			printf("\033[34m");
			break;	// BLUE
		default:
			printf("\033[0m");
			break;	// Reset to default
	}
#endif
}

// =============================
// GST::init_board
// 初始化棋盤、分配紅棋
// =============================
void GST::init_board() {
	auto now = std::chrono::system_clock::now();
	auto now_as_duration = now.time_since_epoch();
	auto now_as_microseconds =
		std::chrono::duration_cast<std::chrono::microseconds>(now_as_duration).count();
	pcg32 rng(now_as_microseconds);
	/*
		A  B  C  D  E  F  G  H  a  b  c  d  e  f  g  h
		0  1  2  3  4  5  6  7  8  9 10 11 12 13 14 15  <-piece index, used in pos and color
	   25 26 27 28 31 32 33 34 10  9  8  7  4  3  2  1  <-position on board
	*/

	memset(board, 0, sizeof(board));
	memset(pos, -1, sizeof(pos));
	memset(revealed, false, sizeof(revealed));
	for (int i = 0; i < ROW * COL; i++) piece_board[i] = -1;
	for (int i = 0; i < 4; i++) piece_nums[i] = 0;
	for (int i = 0; i < PIECES; i++) {	// set color
		color[i] = BLUE;
		color[i + 8] = -BLUE;
	}
	nowTurn = USER;
	winner = -1;
	n_plies = 0;

	printf("請輸入自訂盤面（6x6，使用 A-H, a-h 表示棋子，0 表示空位）：\n");
	for (int row = 0; row < ROW; row++) {
		for (int col = 0; col < COL; col++) {
			char ch;
			scanf(" %c", &ch);
			int index = row * COL + col;
			if (ch == '0') continue;
			if (piece_index.find(ch) != piece_index.end()) {
				int p = piece_index[ch];
				if (pos[p] != -1) {
					printf("錯誤：重複輸入 %c，請重新輸入\n", ch);
					exit(1);
				}
				pos[p] = index;
				piece_board[index] = p;
				board[index] = (p < PIECES) ? 1 : -1;
			} else {
				printf("錯誤：%c 是無效的代號，請重新輸入\n", ch);
				exit(1);
			}
		}
	}
	for (int i = 0; i < PIECES * 2; i++) {
		if (pos[i] == -1) {
			revealed[i] = true;
		}
	}
	// 設置紅色棋子
	char red[4] = {'A', 'B', 'D', 'F'};	  // 用戶
	char red2[4] = {'a', 'b', 'd', 'f'};  // 敵人
	printf("預設紅色幽靈: A B D F (玩家1) 和 a b d f (玩家2)\n");

	for (int i = 0; i < 4; i++) {
		color[piece_index[red[i]]] = RED;
		color[piece_index[red2[i]]] = -RED;
	}

	for (int i = 0; i < PIECES; i++) {
		revealed[i] = true;
	}

	// // USER 紅色幽靈設定
	// char red[4];
	// bool used_red[PIECES] = {};
	// printf("請設定 USER 的紅色幽靈（A-H 中選四個）：\n");
	// for (int i = 0; i < 4; i++) {
	//     scanf(" %c", &red[i]);
	//     if (red[i] < 'A' || red[i] > 'H') {
	//         printf("錯誤：%c 不是有效的紅棋\n", red[i]);
	//         exit(1);
	//     }
	//     int idx = piece_index[red[i]];
	//     if (used_red[idx]) {
	//         printf("錯誤：重複設定紅棋 %c\n", red[i]);
	//         exit(1);
	//     }
	//     used_red[idx] = true;
	//     color[idx] = RED;

	//     // 標記玩家的棋子從開始就被揭露
	//     revealed[idx] = true;
	// }

	// // ENEMY 紅色幽靈設定
	// char red2[4];
	// bool used_red2[PIECES] = {};
	// printf("請設定 ENEMY 的紅色幽靈（a-h 中選四個）：\n");
	// for (int i = 0; i < 4; i++) {
	//     scanf(" %c", &red2[i]);
	//     if (red2[i] < 'a' || red2[i] > 'h') {
	//         printf("錯誤：%c 不是有效的紅棋\n", red2[i]);
	//         exit(1);
	//     }
	//     int idx = piece_index[red2[i]];
	//     if (used_red2[idx - 8]) {
	//         printf("錯誤：重複設定紅棋 %c\n", red2[i]);
	//         exit(1);
	//     }
	//     used_red2[idx - 8] = true;
	//     color[idx] = -RED;
	// }

	// 重算各方棋子數
	for (int i = 0; i < PIECES * 2; i++) {
		if (pos[i] == -1) continue;
		if (color[i] == RED)
			piece_nums[0]++;
		else if (color[i] == BLUE)
			piece_nums[1]++;
		else if (color[i] == -RED)
			piece_nums[2]++;
		else if (color[i] == -BLUE)
			piece_nums[3]++;
	}

	return;
}

// =============================
// GST::print_board
// 印出棋盤、剩餘棋子、被吃棋子
// =============================
void GST::print_board() {
	for (int i = 0; i < ROW; i++) {
		for (int j = 0; j < COL; j++) {
			if (piece_board[i * ROW + j] != -1)
				printf("%4c", print_piece[piece_board[i * ROW + j]]);
			else if (i == 0 && j == 0)
				printf("%4c", '<');
			else if (i == 0 && j == COL - 1)
				printf("%4c", '>');
			else
				printf("%4c", '-');
		}
		printf("\n");
	}
	printf("\n");
	printf("User remaining ghosts: ");
	for (int i = 0; i < PIECES; i++) {
		if (pos[i] != -1) printf("%c: %s ", print_piece[i], color[i] == RED ? "red" : "blue");
	}
	printf("\n");
	printf("Eaten enemy ghosts: ");
	for (int i = 8; i < PIECES * 2; i++) {
		if (pos[i] == -1) printf("%c: %s ", print_piece[i], color[i] == -RED ? "red" : "blue");
	}
	printf("\n");
}

// =============================
// GST::gen_move
// 產生指定棋子的所有合法移動
// =============================
int GST::gen_move(int* move_arr, int piece, int location, int& count) {
	int row = location / ROW;
	int col = location % COL;

	if (nowTurn == USER) {
		if (row != 0 && board[location - 6] <= 0) move_arr[count++] = piece << 4;			 // 上
		if (row != ROW - 1 && board[location + 6] <= 0) move_arr[count++] = piece << 4 | 3;	 // 下
		if (col != 0 && board[location - 1] <= 0) move_arr[count++] = piece << 4 | 1;		 // 左
		if (col != COL - 1 && board[location + 1] <= 0) move_arr[count++] = piece << 4 | 2;	 // 右
		if (color[piece] == BLUE) {															 // 逃脫
			if (location == 0) move_arr[count++] = piece << 4 | 1;
			if (location == 5) move_arr[count++] = piece << 4 | 2;
		}
	} else {
		if (row != 0 && board[location - 6] >= 0) move_arr[count++] = piece << 4;
		if (row != ROW - 1 && board[location + 6] >= 0) move_arr[count++] = piece << 4 | 3;
		if (col != 0 && board[location - 1] >= 0) move_arr[count++] = piece << 4 | 1;
		if (col != COL - 1 && board[location + 1] >= 0) move_arr[count++] = piece << 4 | 2;
		if (color[piece] == -BLUE) {
			if (location == 30) move_arr[count++] = piece << 4 | 1;
			if (location == 35) move_arr[count++] = piece << 4 | 2;
		}
	}
	return count;
}

// =============================
// GST::gen_all_move
// 產生所有合法移動
// =============================
int GST::gen_all_move(int* move_arr) {
	int count = 0;
	int offset = nowTurn == ENEMY ? PIECES : 0;
	int* nowTurn_pos = pos + offset;

	for (int i = 0; i < PIECES; i++) {
		if (pos[i + offset] != -1) {
			gen_move(move_arr, i + offset, nowTurn_pos[i], count);
		}
	}
	return count;
}

// =============================
// check_win_move
// 判斷移動是否可直接獲勝
// =============================
bool check_win_move(int location, int dir) {
	if (location == 0 || location == 30)
		return dir == 1 ? true : false;
	else if (location == 5 || location == 35)
		return dir == 2 ? true : false;
	return false;
}

// =============================
// GST::do_move
// 執行移動，更新棋盤、吃棋、勝負判斷
// =============================
void GST::do_move(int move) {
	int piece = move >> 4;
	int direction = move & 0xf;

	if (abs(color[piece]) == BLUE) {
		if (check_win_move(pos[piece], direction)) {
			winner = nowTurn;
			n_plies++;
			nowTurn ^= 1;
			is_escape = true;
			return;
		}
	}
	if (n_plies == MAX_PLIES) {
		fprintf(stderr, "cannot do anymore moves\n");
		exit(1);
	}

	int dst = pos[piece] + dir_val[direction];
	// dst: the chess's location after move / pos: the location of chess / dir_val: up down left
	// right

	if (board[dst] < 0) {			 // Enemy's color
		pos[piece_board[dst]] = -1;	 // chess is eaten
		move |= piece_board[dst] << 8;
		revealed[piece_board[dst]] = true;
		if (color[piece_board[dst]] == -RED)
			piece_nums[2] -= 1;	 // check the remain color of red & blue chess
		else if (color[piece_board[dst]] == -BLUE)
			piece_nums[3] -= 1;
		else if (color[piece_board[dst]] == -UNKNOWN) {
		}  // 先什麼都不做
		else {
			fprintf(stderr, "piece: %d, direction: %d\n", piece, direction);
			fprintf(stderr, "pos[piece]: %d, dir_val[direction]: %d\n", pos[piece],
					dir_val[direction]);
			fprintf(stderr, "color[piece_board[dst]]: %d, piece_board[dst]: %d\n",
					color[piece_board[dst]], piece_board[dst]);
			fprintf(stderr, "do_move error, eaten color wrong!\n");
			exit(1);
		}
	} else if (board[dst] > 0) {  // User's color
		pos[piece_board[dst]] = -1;
		move |= piece_board[dst] << 8;
		revealed[piece_board[dst]] = true;
		if (color[piece_board[dst]] == RED)
			piece_nums[0] -= 1;
		else if (color[piece_board[dst]] == BLUE)
			piece_nums[1] -= 1;
		else if (color[piece_board[dst]] == UNKNOWN) {
		}  // 先什麼都不做
		else {
			fprintf(stderr, "piece: %d, direction: %d\n", piece, direction);
			fprintf(stderr, "pos[piece]: %d, dir_val[direction]: %d\n", pos[piece],
					dir_val[direction]);
			fprintf(stderr, "color[piece_board[dst]]: %d, piece_board[dst]: %d\n",
					color[piece_board[dst]], piece_board[dst]);
			fprintf(stderr, "do_move error, eaten color wrong!\n");
			exit(1);
		}
	} else {
		move |= 0x1000;
	}

	board[pos[piece]] = 0;		   // set 0 at the location which stay before => space: color = 0
	piece_board[pos[piece]] = -1;  // set 0 at the location which stay before => space: no chess
	board[dst] = color[piece];	   // color the chess color at the location after move
	piece_board[dst] = piece;	   // set chess number at the location after move
	pos[piece] = dst;			   // the location of chess now
	history[n_plies++] = move;
	nowTurn ^= 1;  // change player
}

// =============================
// GST::undo
// 回復到上一步
// =============================
void GST::undo() {	// return to last move(use to return status of random move)
	if (winner != -1) winner = -1;

	if (n_plies == 0) {
		fprintf(stderr, "no history\n");
		exit(1);
	}
	nowTurn ^= 1;  // change player

	int move = history[--n_plies];
	int check_eaten = move >> 12;
	int eaten_piece = (move & 0xfff) >> 8;
	int piece = (move & 0xff) >> 4;
	int direction = move & 0xf;
	int src = pos[piece] - dir_val[direction];	// location of last move

	if (is_escape) {
		is_escape = false;
		return;
	}

	if (check_eaten != 0x1) {
		board[pos[piece]] = color[eaten_piece];
		piece_board[pos[piece]] = eaten_piece;
		pos[eaten_piece] = pos[piece];
		if (nowTurn == USER) {
			if (color[eaten_piece] == -RED)
				piece_nums[2] += 1;
			else if (color[eaten_piece] == -BLUE)
				piece_nums[3] += 1;
			else if (color[eaten_piece] == -UNKNOWN) {
			}  // 先什麼都不做
			else {
				fprintf(stderr, "undo error, eaten color wrong!");
				exit(1);
			}
		} else {
			if (color[eaten_piece] == RED)
				piece_nums[0] += 1;
			else if (color[eaten_piece] == BLUE)
				piece_nums[1] += 1;
			else if (color[eaten_piece] == UNKNOWN) {
			}  // 先什麼都不做
			else {
				fprintf(stderr, "undo error, eaten color wrong!");
				exit(1);
			}
		}
	} else {
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
bool GST::is_over() {  // game end or not => the number of remain chess color
	if (n_plies >= 200) {
		winner = -2;  // -2表示平局
		return true;
	}
	if (winner != -1)
		return true;
	else {
		if (piece_nums[0] == 0 || piece_nums[3] == 0) {
			winner = USER;
			return true;
		} else if (piece_nums[1] == 0 || piece_nums[2] == 0) {
			winner = ENEMY;
			return true;
		}
	}
	return false;
}

// =============================
// GST::is_valid_pattern
// 檢查給定的 pattern 偏移量是否有效
// =============================
bool GST::is_valid_pattern(int base_pos, const int* offset) {
	int base_row = base_pos / COL;
	int base_col = base_pos % COL;

	if (offset == offset_1x4) {
		if (base_col > 2) return false;
	} else if (offset == offset_2x2) {
		if (base_col > 4 || base_row > 4) return false;
	} else if (offset == offset_4x1) {
		if (base_row > 2) return false;
	}

	return true;
}

// =============================
// GST::get_loc
// 取得給定位置和偏移量的唯一編碼
// =============================
int GST::get_loc(int base_pos, const int* offset) {
	int position[4];
	for (int i = 0; i < 4; i++) {
		position[i] = base_pos + offset[i];
	}
	return (position[0] * 36 * 36 * 36 + position[1] * 36 * 36 + position[2] * 36 + position[3]);
}

// =============================
// GST::get_feature_unknown
// 獲取給定位置和偏移量的特徵編碼
// =============================
int GST::get_feature_unknown(int base_pos, const int* offset) {	 // the piece color
	int features[4];
	for (int i = 0; i < 4; i++) {
		int pos = base_pos + offset[i];
		if (nowTurn == USER) {
			features[i] = (board[pos] < 0) ? 3 : board[pos];
		} else {
			features[i] = (board[pos] > 0) ? 3 : -board[pos];
		}
	}
	return (features[0] * 64 + features[1] * 16 + features[2] * 4 + features[3]);
}

// =============================
// GST::get_weight
// 獲取給定位置和偏移量的權重值
// =============================
float GST::get_weight(int base_pos, const int* offset, DATA& d) {
	int feature = get_feature_unknown(base_pos, offset);
	int LUTidx = d.LUT_idx(d.trans[get_loc(base_pos, offset)], feature);
	float weight = 0;
	if (nowTurn == USER) {
		if (piece_nums[2] == 1) {  // E R = 1
			weight = (float)(d.LUTw_U_R1[LUTidx]) / (float)(d.LUTv_U_R1[LUTidx]);
			// printf("U R1 using\n");
		} else if (piece_nums[1] == 1) {  // U B = 1
			weight = (float)(d.LUTw_U_B1[LUTidx]) / (float)(d.LUTv_U_B1[LUTidx]);
		} else {
			weight = (float)(d.LUTw_U[LUTidx]) / (float)(d.LUTv_U[LUTidx]);
		}
	} else {
		if (piece_nums[0] == 1) {  // U R = 1
			weight = (float)(d.LUTw_E_R1[LUTidx]) / (float)(d.LUTv_E_R1[LUTidx]);
			// printf("E R1 using\n");
		} else if (piece_nums[3] == 1) {  // E B = 1
			weight = (float)(d.LUTw_E_B1[LUTidx]) / (float)(d.LUTv_E_B1[LUTidx]);
		} else {
			weight = (float)(d.LUTw_E[LUTidx]) / (float)(d.LUTv_E[LUTidx]);
		}
	}

	// printf("location = %d, feature = %d, LUTidx = %d, weight = %f\n", d.trans[get_loc(base_pos,
	// offset)], feature, LUTidx, weight);

	return weight;
}

// =============================
// GST::compute_board_weight
// 計算整個棋盤的權重值
// =============================
float GST::compute_board_weight(DATA& d) {
	float total_weight = 0;

	for (int pos = 0; pos < ROW * COL; pos++) {
		int row = pos / COL;
		int col = pos % COL;

		if (is_valid_pattern(pos, offset_1x4)) {
			total_weight += get_weight(pos, offset_1x4, d);
		}
		if (is_valid_pattern(pos, offset_4x1)) {
			total_weight += get_weight(pos, offset_4x1, d);
		}
		if (is_valid_pattern(pos, offset_2x2)) {
			total_weight += get_weight(pos, offset_2x2, d);
		}
	}

	return total_weight / (float)TUPLE_NUM;
}

// =============================
// GST::highest_weight
// 找出權重最高的移動
// =============================
int GST::highest_weight(DATA& d) {
	float WEIGHT[MAX_MOVES] = {0};
	int root_nmove;
	int root_moves[MAX_MOVES];
	root_nmove = gen_all_move(root_moves);

	for (int m = 0; m < root_nmove; m++) {
		int move_index = m;
		int piece = root_moves[m] >> 4;
		int direction = root_moves[m] & 0xf;
		int src = pos[piece];
		int dst = src + dir_val[direction];	 // the position after move

		if (pos[piece] == 0 && direction == 1 && nowTurn == USER &&
			board[0] == BLUE) {	 // if check_win_move() = true, won't move and return directly
			WEIGHT[move_index] = 1;
		} else if (pos[piece] == 5 && direction == 2 && nowTurn == USER && board[5] == BLUE) {
			WEIGHT[move_index] = 1;
		} else if (pos[piece] == 30 && direction == 1 && nowTurn == ENEMY && board[30] == -BLUE) {
			WEIGHT[move_index] = 1;
		} else if (pos[piece] == 35 && direction == 2 && nowTurn == ENEMY && board[35] == -BLUE) {
			WEIGHT[move_index] = 1;
		} else if (pos[piece] == 4 && direction == 2 && nowTurn == USER && color[piece] == BLUE) {
			if (board[5] == 0 && board[11] >= 0) {
				WEIGHT[move_index] = 1;
			}
		} else if (pos[piece] == 1 && direction == 1 && nowTurn == USER && color[piece] == BLUE) {
			if (board[0] == 0 && board[6] >= 0) {
				WEIGHT[move_index] = 1;
			}
		} else if (pos[piece] == 34 && direction == 2 && nowTurn == ENEMY &&
				   color[piece] == -BLUE) {
			if (board[35] == 0 && board[29] <= 0) {
				WEIGHT[move_index] = 1;
			}
		} else if (pos[piece] == 31 && direction == 1 && nowTurn == ENEMY &&
				   color[piece] == -BLUE) {
			if (board[30] == 0 && board[24] <= 0) {
				WEIGHT[move_index] = 1;
			}
		} else {
			int tmp_color[PIECES * 2];	// 去除上帝視角
			for (int i = 0; i < PIECES * 2; i++) tmp_color[i] = color[i];
			if (nowTurn == USER)
				for (int i = PIECES; i < PIECES * 2; i++) color[i] = -UNKNOWN;
			else
				for (int i = 0; i < PIECES; i++) color[i] = UNKNOWN;

			do_move(root_moves[m]);
			nowTurn ^= 1;

			WEIGHT[move_index] = compute_board_weight(d);

			nowTurn ^= 1;
			undo();

			for (int i = 0; i < PIECES * 2; i++) color[i] = tmp_color[i];
		}

		// 調參數時間
		int row = dst / 6;
		int col = dst % 6;

		// 計算目標位置到各角落的距離
		int d0 = row + col;				  // 到 (0,0) 的距離
		int d5 = row + (5 - col);		  // 到 (0,5) 的距離
		int d30 = (5 - row) + col;		  // 到 (5,0) 的距離
		int d35 = (5 - row) + (5 - col);  // 到 (5,5) 的距離

		// 存儲每個棋子到各角落的距離
		std::vector<std::tuple<int, int, int>> pieces_distances;  // (棋子索引, 角落編號, 距離)

		if (nowTurn == USER) {
			// 計算所有棋子到各角落的距離
			for (int i = 0; i < PIECES; i++) {
				if (pos[i] != -1) {
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
		} else if (nowTurn == ENEMY) {
			for (int i = PIECES; i < PIECES * 2; i++) {
				if (pos[i] != -1) {
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

		// 按距離排序所有棋子-角落組合
		std::sort(pieces_distances.begin(), pieces_distances.end(),
				  [](const std::tuple<int, int, int>& a, const std::tuple<int, int, int>& b) {
					  return std::get<2>(a) < std::get<2>(b);
				  });

		// 已分配的棋子和角落
		bool piece_assigned[PIECES * 2];
		bool corner_assigned[4];
		int assigned_corner_for_piece[PIECES * 2];

		memset(piece_assigned, false, sizeof(piece_assigned));
		memset(corner_assigned, false, sizeof(corner_assigned));
		memset(assigned_corner_for_piece, -1, sizeof(assigned_corner_for_piece));

		// 按距離分配棋子到角落
		for (const auto& tuple : pieces_distances) {
			int p_idx = std::get<0>(tuple);
			int corner = std::get<1>(tuple);

			// 如果這個棋子和角落都還沒被分配，則進行分配
			if (!piece_assigned[p_idx] && !corner_assigned[corner]) {
				piece_assigned[p_idx] = true;
				corner_assigned[corner] = true;
				assigned_corner_for_piece[p_idx] = corner;
			}

			// 如果所有角落都已分配，停止循環
			if (corner_assigned[0] && corner_assigned[1] && corner_assigned[2] &&
				corner_assigned[3]) {
				break;
			}
		}

		// 角落獎勵係數
		float corner_bonus = 1.0;

		// 檢查當前棋子是否被分配到某個角落
		if (assigned_corner_for_piece[piece] != -1) {
			int assigned_corner = assigned_corner_for_piece[piece];
			int current_dist;

			// 計算當前位置到分配角落的距離
			int p_row = src / 6;
			int p_col = src % 6;

			if (assigned_corner == 0) {
				current_dist = p_row + p_col;
				// 檢查移動是否更接近分配的角落
				if (d0 < current_dist) {
					corner_bonus = 1.01;
				}
			} else if (assigned_corner == 1) {
				current_dist = p_row + (5 - p_col);
				if (d5 < current_dist) {
					corner_bonus = 1.01;
				}
			} else if (assigned_corner == 2) {
				current_dist = (5 - p_row) + p_col;
				if (d30 < current_dist) {
					corner_bonus = 1.01;
				}
			} else if (assigned_corner == 3) {
				current_dist = (5 - p_row) + (5 - p_col);
				if (d35 < current_dist) {
					corner_bonus = 1.01;
				}
			}
		}

		WEIGHT[move_index] *= corner_bonus;

		if (piece_nums[2] <= 1 && board[dst] == 0) {
			WEIGHT[move_index] *= 1.01;
		}

		// printf("Move = %d | piece: %c, direction: %d, WEIGHT[]: %f\n\n", move_index,
		// print_piece[piece], direction, WEIGHT[move_index]);
	}

	float max_weight = -1;
	int do_idx = -1, same_idx = 0, SAME[MAX_MOVES] = {0};

	// 找到最大權重的走步
	for (int i = 0; i < root_nmove; i++) {
		if (WEIGHT[i] == max_weight) {
			SAME[same_idx++] = i;
		} else if (WEIGHT[i] > max_weight) {
			max_weight = WEIGHT[i];
			do_idx = i;
			SAME[0] = i;
			same_idx = 1;
		}
	}

	auto now = std::chrono::system_clock::now();
	auto now_as_duration = now.time_since_epoch();
	auto now_as_microseconds =
		std::chrono::duration_cast<std::chrono::microseconds>(now_as_duration).count();
	pcg32 rng(now_as_microseconds);
	if (same_idx > 1) {
		int x = rng(same_idx - 1);
		do_idx = SAME[x];
	}

	return root_moves[do_idx];
}

DATA data;

int main() {
	// 為Mac初始化隨機數生成
	std::random_device rd;
	std::mt19937 gen(std::chrono::system_clock::now().time_since_epoch().count());
	std::uniform_int_distribution<> dist(0, 7);

	GST game;

	MCTS mcts(10000);
	ISMCTS ismcts(10000);

	bool my_turn = true;

	data.init_data();
	data.read_data_file(500000);

	// 初始化棋盤
	game.init_board();
	game.print_board();

	std::cout << "\n===== 遊戲開始 =====\n\n";

	// 主遊戲循環
	while (!game.is_over()) {
		if (my_turn) {
			std::cout << "Player 1 (ISMCTS) 思考中...\n";
			int move = ismcts.findBestMove(game, data);
			if (move == -1) {
				std::cout << "ISMCTS 無法找到有效移動，玩家1可能已經輸了！\n";
				break;
			}

			// 直接執行移動
			game.do_move(move);
		} else {
			std::cout << "Player 2 (MCTS) 思考中...\n";
			int move = mcts.findBestMove(game);
			if (move == -1) {
				std::cout << "MCTS 無法找到有效移動，玩家2可能已經輸了！\n";
				break;
			}

			// 執行移動
			game.do_move(move);
		}

		// 顯示當前棋盤狀態
		game.print_board();

		// 顯示當前回合數
		std::cout << "當前回合數: " << game.n_plies << std::endl;

		// 切換玩家
		my_turn = !my_turn;
	}

	// 宣布勝者
	int winner = game.get_winner();
	if (winner == -2) {
		printf("遊戲結束！達到20回合，判定為平局！\n");
	} else {
		printf("遊戲結束！%s 獲勝！\n", winner ? "Player 2 (MCTS)" : "Player 1 (ISMCTS)");
		if (game.is_escape) {
			printf("勝利方式：藍色棋子成功逃脫！\n");
		} else if (game.piece_nums[0] == 0) {
			printf("勝利方式：Player 1 的紅色棋子全部被吃光！\n");
		} else if (game.piece_nums[2] == 0) {
			printf("勝利方式：Player 2 的紅色棋子全部被吃光！\n");
		} else if (game.piece_nums[1] == 0) {
			printf("勝利方式：Player 1 的藍色棋子全部被吃光！\n");
		} else if (game.piece_nums[3] == 0) {
			printf("勝利方式：Player 2 的藍色棋子全部被吃光！\n");
		}
	}

	return 0;
}