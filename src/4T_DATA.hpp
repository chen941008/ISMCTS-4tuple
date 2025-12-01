#ifndef DATA_HPP
#define DATA_HPP

#include "4T_header.h"

class GST;

// =============================
// DATA 類別：4-tuple 特徵資料管理
// =============================
class DATA {
public:
    // LUTw_E/LUTv_E: Enemy 4-tuple 贏的次數/去過的次數
    // LUTw_U/LUTv_U: User 4-tuple 贏的次數/去過的次數
    unsigned long long LUTw_E[TUPLE_NUM * FEATURE_NUM + 1] = {0}, LUTv_E[TUPLE_NUM * FEATURE_NUM + 1] = {0};
    unsigned long long LUTw_U[TUPLE_NUM * FEATURE_NUM + 1] = {0}, LUTv_U[TUPLE_NUM * FEATURE_NUM + 1] = {0};
    float LUTwr_U[TUPLE_NUM * FEATURE_NUM + 1] = {0.0}, LUTwr_E[TUPLE_NUM * FEATURE_NUM + 1] = {0.0};

    // R1: 敵方紅棋剩一顆的情況 
    // B1: 我方藍棋剩一顆的情況
    unsigned long long LUTw_E_R1[TUPLE_NUM * FEATURE_NUM + 1] = {0}, LUTv_E_R1[TUPLE_NUM * FEATURE_NUM + 1] = {0};
    unsigned long long LUTw_U_R1[TUPLE_NUM * FEATURE_NUM + 1] = {0}, LUTv_U_R1[TUPLE_NUM * FEATURE_NUM + 1] = {0};
    unsigned long long LUTw_E_B1[TUPLE_NUM * FEATURE_NUM + 1] = {0}, LUTv_E_B1[TUPLE_NUM * FEATURE_NUM + 1] = {0};
    unsigned long long LUTw_U_B1[TUPLE_NUM * FEATURE_NUM + 1] = {0}, LUTv_U_B1[TUPLE_NUM * FEATURE_NUM + 1] = {0};
    float LUTwr_U_B1[TUPLE_NUM * FEATURE_NUM + 1] = {0.0}, LUTwr_E_B1[TUPLE_NUM * FEATURE_NUM + 1] = {0.0};
    float LUTwr_U_R1[TUPLE_NUM * FEATURE_NUM + 1] = {0.0}, LUTwr_E_R1[TUPLE_NUM * FEATURE_NUM + 1] = {0.0};

    int trans[POS_NUM + 1] = {0};      // 棋盤4-tuple pattern位置編碼轉換表

    // =============================
    // 主要方法
    // =============================
    void init_data(); // 初始化所有 LUT
    int LUT_idx(int location, int feature){
        // 由位置與特徵編碼取得 LUT 索引
        return (location - 1) * FEATURE_NUM + feature;
    }
    void read_data_file(int num); // 讀取資料檔案
    void write_data_file_run(int run); // 寫入指定回合資料檔案

    // R1、B1 function：不同資料集的讀寫
    void read_data_file_R1(int num);
    void write_data_file_run_R1(int run);
    void read_data_file_B1(int num);
    void write_data_file_run_B1(int run);
};

#endif