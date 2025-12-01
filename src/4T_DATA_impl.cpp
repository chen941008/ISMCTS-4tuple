#include <string>
#include "4T_header.h"

// =============================
// DATA::init_data
// 初始化所有 LUT 及 4-tuple pattern 位置轉換表
// =============================
void DATA::init_data(){
    for(int i = 0; i < TUPLE_NUM * FEATURE_NUM; i++){
        //初始權重1/2 = 0.5
        LUTw_E[i] = 1;      // 敵方權重初始值
        LUTv_E[i] = 2;      // 敵方次數初始值
        LUTwr_E[i] = 0.5;   // 敵方權重初始值
        LUTw_U[i] = 1;      // 我方權重初始值
        LUTv_U[i] = 2;      // 我方次數初始值
        LUTwr_U[i] = 0.5;   // 我方權重初始值

        LUTw_E_R1[i] = 1;   // R1 資料集初始值
        LUTv_E_R1[i] = 2;
        LUTwr_E_R1[i] = 0.5;   // R1 資料集初始值
        LUTw_U_R1[i] = 1;
        LUTv_U_R1[i] = 2;
        LUTwr_U_R1[i] = 0.5;   // R1 資料集初始值
        LUTw_E_B1[i] = 1;   // B1 資料集初始值
        LUTv_E_B1[i] = 2;
        LUTwr_E_B1[i] = 0.5;   // B1 資料集初始值
        LUTw_U_B1[i] = 1;
        LUTv_U_B1[i] = 2;
        LUTwr_U_B1[i] = 0.5;   // B1 資料集初始值
    }

    int loca_num = 0;
    for(int i = 0; i < ROW * COL; i++){     // 0~35
        int loc;
        //把盤面上的所有tuple做編碼，一個一個存到trans裡方便未來查找跟轉換
        if(i % 6 <= 2){     //1x4
            loca_num++;
            loc = i * 36 * 36 * 36 + 
                  (i + 1) * 36 * 36 + 
                  (i + 2) * 36 + 
                  (i + 3);
            trans[loc] = loca_num;
        }
        if(i < 18){         //4x1
            loca_num++;
            loc = i * 36 * 36 * 36 + 
                  (i + 6) * 36 * 36 + 
                  (i + 12) * 36 + 
                  (i + 18);
            trans[loc] = loca_num;
        }
        if(i % 6 <= 4 && i < 30){       //2x2
            loca_num++;
            loc = i * 36 * 36 * 36 + 
                  (i + 1) * 36 * 36 + 
                  (i + 6) * 36 + 
                  (i + 7);
            trans[loc] = loca_num;
        }
    }
}

// =============================
// _csv
// 字串分割成 vector（以逗號分隔）
// =============================
std::vector<std::string> _csv(std::string s){
    std::vector<std::string> arr;
    std::istringstream delim(s);
    std::string token;
    int c = 0;
    while (getline(delim, token, ',')){
        arr.push_back(token);
        c++;                                            
    }
    return arr;
}

// =============================
// 讀取資料檔案，更新 LUT
// =============================
void DATA::read_data_file(int num){       //reada the data in CSV and write to 2-tuple array    
    // std::ifstream iEdata("Edata.csv", std::ios::in);

    std::string E_filename = "./data/Edata_" + std::to_string(num) + ".csv";
    std::ifstream iEdata(E_filename, std::ios::in);

    if (!iEdata){
        printf("Add new Edata.csv\n");
        std::ofstream Edata;
        Edata.open("Edata.csv", std::ios::out | std::ios::trunc);
    }   
    else{
        std::string line;
        bool first_line = 1;                     //first line or not
        while (getline(iEdata, line)){           //getline(iEdata, line) => read the data in CSV by row
            if(first_line){
                first_line = 0;
                continue;;
            }
            std::vector<std::string> a = _csv(line);
            LUTw_E[LUT_idx(std::stoll(a[0]), std::stoll(a[1]))] = std::stoll(a[2]);
            LUTv_E[LUT_idx(std::stoll(a[0]), std::stoll(a[1]))] = std::stoll(a[3]);
            LUTwr_E[LUT_idx(std::stoll(a[0]), std::stoll(a[1]))] = std::stof(a[4]);
        }
    }    

    //std::ifstream iUdata("Udata.csv", std::ios::in);

    std::string U_filename = "./data/Udata_" + std::to_string(num) + ".csv";
    std::ifstream iUdata(U_filename, std::ios::in);

    if (!iUdata){
        printf("Add new Udata.csv\n");
        std::ofstream Udata;
        Udata.open("Udata.csv", std::ios::out | std::ios::trunc);
    }   
    else{
        std::string line;
        bool first_line = 1;                          //first line or not
        while (getline(iUdata, line)){           //getline(iUdata, line) => read the data in CSV by row
            if(first_line){
                first_line = 0;
                continue;
            }
            std::vector<std::string> a = _csv(line);
            LUTw_U[LUT_idx(std::stoll(a[0]), std::stoll(a[1]))] = std::stoll(a[2]);
            LUTv_U[LUT_idx(std::stoll(a[0]), std::stoll(a[1]))] = std::stoll(a[3]);
            LUTwr_U[LUT_idx(std::stoll(a[0]), std::stoll(a[1]))] = std::stof(a[4]);
        }
    }
}

void DATA::read_data_file_R1(int num){       //reada the data in CSV and write to 2-tuple array    
    // std::ifstream iEdata("Edata.csv", std::ios::in);

    std::string E_filename = "./data R1/Edata_" + std::to_string(num) + ".csv";
    std::ifstream iEdata(E_filename, std::ios::in);

    if (!iEdata){
        printf("Add new R1 Edata.csv\n");
        std::ofstream Edata;
        Edata.open("Edata.csv", std::ios::out | std::ios::trunc);
    }   
    else{
        std::string line;
        bool first_line = 1;                     //first line or not
        while (getline(iEdata, line)){           //getline(iEdata, line) => read the data in CSV by row
            if(first_line){
                first_line = 0;
                continue;;
            }
            std::vector<std::string> a = _csv(line);
            LUTw_E_R1[LUT_idx(std::stoll(a[0]), std::stoll(a[1]))] = std::stoll(a[2]);
            LUTv_E_R1[LUT_idx(std::stoll(a[0]), std::stoll(a[1]))] = std::stoll(a[3]);
            LUTwr_E_R1[LUT_idx(std::stoll(a[0]), std::stoll(a[1]))] = std::stof(a[4]);
        }
    }    

    //std::ifstream iUdata("Udata.csv", std::ios::in);

    std::string U_filename = "./data R1/Udata_" + std::to_string(num) + ".csv";
    std::ifstream iUdata(U_filename, std::ios::in);

    if (!iUdata){
        printf("Add new R1 Udata.csv\n");
        std::ofstream Udata;
        Udata.open("Udata.csv", std::ios::out | std::ios::trunc);
    }   
    else{
        std::string line;
        bool first_line = 1;                          //first line or not
        while (getline(iUdata, line)){           //getline(iUdata, line) => read the data in CSV by row
            if(first_line){
                first_line = 0;
                continue;
            }
            std::vector<std::string> a = _csv(line);
            LUTw_U_R1[LUT_idx(std::stoll(a[0]), std::stoll(a[1]))] = std::stoll(a[2]);
            LUTv_U_R1[LUT_idx(std::stoll(a[0]), std::stoll(a[1]))] = std::stoll(a[3]);
            LUTwr_U_R1[LUT_idx(std::stoll(a[0]), std::stoll(a[1]))] = std::stof(a[4]);
        }
    }
}

void DATA::read_data_file_B1(int num){       //reada the data in CSV and write to 2-tuple array    
    // std::ifstream iEdata("Edata.csv", std::ios::in);

    std::string E_filename = "./data B1/Edata_" + std::to_string(num) + ".csv";
    std::ifstream iEdata(E_filename, std::ios::in);

    if (!iEdata){
        printf("Add new B1 Edata.csv\n");
        std::ofstream Edata;
        Edata.open("Edata.csv", std::ios::out | std::ios::trunc);
    }   
    else{
        std::string line;
        bool first_line = 1;                     //first line or not
        while (getline(iEdata, line)){           //getline(iEdata, line) => read the data in CSV by row
            if(first_line){
                first_line = 0;
                continue;;
            }
            std::vector<std::string> a = _csv(line);
            LUTw_E_B1[LUT_idx(std::stoll(a[0]), std::stoll(a[1]))] = std::stoll(a[2]);
            LUTv_E_B1[LUT_idx(std::stoll(a[0]), std::stoll(a[1]))] = std::stoll(a[3]);
            LUTwr_E_B1[LUT_idx(std::stoll(a[0]), std::stoll(a[1]))] = std::stof(a[4]);
        }
    }    

    //std::ifstream iUdata("Udata.csv", std::ios::in);

    std::string U_filename = "./data B1/Udata_" + std::to_string(num) + ".csv";
    std::ifstream iUdata(U_filename, std::ios::in);

    if (!iUdata){
        printf("Add new B1 Udata.csv\n");
        std::ofstream Udata;
        Udata.open("Udata.csv", std::ios::out | std::ios::trunc);
    }   
    else{
        std::string line;
        bool first_line = 1;                          //first line or not
        while (getline(iUdata, line)){           //getline(iUdata, line) => read the data in CSV by row
            if(first_line){
                first_line = 0;
                continue;
            }
            std::vector<std::string> a = _csv(line);
            LUTw_U_B1[LUT_idx(std::stoll(a[0]), std::stoll(a[1]))] = std::stoll(a[2]);
            LUTv_U_B1[LUT_idx(std::stoll(a[0]), std::stoll(a[1]))] = std::stoll(a[3]);
            LUTwr_U_B1[LUT_idx(std::stoll(a[0]), std::stoll(a[1]))] = std::stof(a[4]);
        }
    }
}

void DATA::write_data_file_run(int run){          //open file & write file
    // if (mkdir("data", 0777) != 0) {
    //     // new file
    // }

    std::ofstream Edata, Udata;

    std::string E_filename = "data/Edata_" + std::to_string(run) + ".csv";
    std::string U_filename = "data/Udata_" + std::to_string(run) + ".csv";
    
    Edata.open(E_filename, std::ios::out | std::ios::trunc);
    Udata.open(U_filename, std::ios::out | std::ios::trunc);
    
    Edata << "location" << "," << "feature" << "," << "LUTw" << "," << "LUTv" << "," << "4-tuple win rate" << std::endl;
    Udata << "location" << "," << "feature" << "," << "LUTw" << "," << "LUTv" << "," << "4-tuple win rate" << std::endl;
        
    for(int i = 1; i <= TUPLE_NUM; i++){
        for(int j = 0; j < FEATURE_NUM; j++){
            Edata << i << "," << j << "," << LUTw_E[LUT_idx(i, j)] << "," << LUTv_E[LUT_idx(i, j)] << "," << (float)LUTw_E[LUT_idx(i, j)] / (float)LUTv_E[LUT_idx(i, j)] << std::endl;
        }
    }

    for(int i = 1; i <= TUPLE_NUM; i++){
        for(int j = 0; j < FEATURE_NUM; j++){
            Udata << i << "," << j << "," << LUTw_U[LUT_idx(i, j)] << "," << LUTv_U[LUT_idx(i, j)] << "," << (float)LUTw_U[LUT_idx(i, j)] / (float)LUTv_U[LUT_idx(i, j)] << std::endl;
        }
    }
    
    
    Edata.close();
    Udata.close();
}

void DATA::write_data_file_run_R1(int run){          //open file & write file
    // if (mkdir("data R1", 0777) != 0) {
    //     // new file
    // }

    std::ofstream Edata, Udata;

    std::string E_filename = "data R1/Edata_" + std::to_string(run) + ".csv";
    std::string U_filename = "data R1/Udata_" + std::to_string(run) + ".csv";
    
    Edata.open(E_filename, std::ios::out | std::ios::trunc);
    Udata.open(U_filename, std::ios::out | std::ios::trunc);
    
    Edata << "location" << "," << "feature" << "," << "LUTw" << "," << "LUTv" << "," << "4-tuple win rate" << std::endl;
    Udata << "location" << "," << "feature" << "," << "LUTw" << "," << "LUTv" << "," << "4-tuple win rate" << std::endl;
        
    for(int i = 1; i <= TUPLE_NUM; i++){
        for(int j = 0; j < FEATURE_NUM; j++){
            Edata << i << "," << j << "," << LUTw_E_R1[LUT_idx(i, j)] << "," << LUTv_E_R1[LUT_idx(i, j)] << "," << (float)LUTw_E_R1[LUT_idx(i, j)] / (float)LUTv_E_R1[LUT_idx(i, j)] << std::endl;
        }
    }

    for(int i = 1; i <= TUPLE_NUM; i++){
        for(int j = 0; j < FEATURE_NUM; j++){
            Udata << i << "," << j << "," << LUTw_U_R1[LUT_idx(i, j)] << "," << LUTv_U_R1[LUT_idx(i, j)] << "," << (float)LUTw_U_R1[LUT_idx(i, j)] / (float)LUTv_U_R1[LUT_idx(i, j)] << std::endl;
        }
    }
    
    
    Edata.close();
    Udata.close();
}

void DATA::write_data_file_run_B1(int run){          //open file & write file
    // if (mkdir("data B1", 0777) != 0) {
    //     // new file
    // }

    std::ofstream Edata, Udata;

    std::string E_filename = "data B1/Edata_" + std::to_string(run) + ".csv";
    std::string U_filename = "data B1/Udata_" + std::to_string(run) + ".csv";
    
    Edata.open(E_filename, std::ios::out | std::ios::trunc);
    Udata.open(U_filename, std::ios::out | std::ios::trunc);
    
    Edata << "location" << "," << "feature" << "," << "LUTw" << "," << "LUTv" << "," << "4-tuple win rate" << std::endl;
    Udata << "location" << "," << "feature" << "," << "LUTw" << "," << "LUTv" << "," << "4-tuple win rate" << std::endl;
        
    for(int i = 1; i <= TUPLE_NUM; i++){
        for(int j = 0; j < FEATURE_NUM; j++){
            Edata << i << "," << j << "," << LUTw_E_B1[LUT_idx(i, j)] << "," << LUTv_E_B1[LUT_idx(i, j)] << "," << (float)LUTw_E_B1[LUT_idx(i, j)] / (float)LUTv_E_B1[LUT_idx(i, j)] << std::endl;
        }
    }

    for(int i = 1; i <= TUPLE_NUM; i++){
        for(int j = 0; j < FEATURE_NUM; j++){
            Udata << i << "," << j << "," << LUTw_U_B1[LUT_idx(i, j)] << "," << LUTv_U_B1[LUT_idx(i, j)] << "," << (float)LUTw_U_B1[LUT_idx(i, j)] / (float)LUTv_U_B1[LUT_idx(i, j)] << std::endl;
        }
    }
    
    
    Edata.close();
    Udata.close();
}