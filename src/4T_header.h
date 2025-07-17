// =============================
// 4T_header.h：全域常數、型別、標頭檔
// =============================
#define ROW 6           // 棋盤行數
#define COL 6           // 棋盤列數
#define PIECES 8        // 每方棋子數
#define MAX_PLIES 1000  // 最大步數
#define MAX_MOVES 32    // 每回合最大合法移動數

#define RED 1           // 紅色棋子
#define BLUE 2          // 藍色棋子
#define UNKNOWN 3       // 未知顏色

#define USER 0          // 玩家
#define ENEMY 1         // 對手

#define POS_NUM 1537019 // 4-tuple pattern 位置編碼總數
#define FEATURE_NUM 256 // 4-tuple pattern 特徵總數
#define TUPLE_NUM 61    // 4-tuple pattern 總數

#define MAX_HAND 200    // 最大移動數

// =============================
// 標準函式庫與平台相關標頭
// =============================
#include <stdlib.h>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <map>
#include <iostream>
#include <fstream>
#include <streambuf>
#include <sstream>
#include <chrono>
#include <random>
#include <time.h>
#include <algorithm>
#include <iomanip>
#include <limits>
#include <cassert>
#include <memory>
#include <cmath>
#include <unordered_map>

#ifdef _WIN32
    #include <windows.h>
    #include <direct.h>
#elif defined(__APPLE__)
    #include <mach/mach_time.h>
    #include <unistd.h>
    #include <sys/stat.h>
#endif

// =============================
// 專案主要標頭檔
// =============================
#include "pcg_random.hpp"   // PCG 隨機數
#include "4T_DATA.hpp"      // 4-tuple 資料管理
#include "4T_GST.hpp"       // 棋盤狀態