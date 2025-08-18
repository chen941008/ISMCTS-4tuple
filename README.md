## 概述

幽靈棋 4-tuple + ISMCTS 合併版，可正常編譯連接伺服器的執行檔，同時提供本地測試程式

## 資料夾架構與介紹：
```
src/
├── server/                    # 在這裡編譯
│   ├── data/                  # 4-tuple 原程式訓練的資料結果（已刪除訓練程式，只留下使用資料的必要部分）
│   ├── main.cpp               # 對接 server 跟 MyAI 的程式
│   └── MyAI.cpp               # AI 主程式
│
├── 4T_DATA_impl.cpp           # 4-tuple 讀取 /src/server/data 資料的程式
├── 4T_GST_impl.cpp            # 4-tuple 實作
├── pcg_xxx.hpp                # 4-tuple 額外需要的 header
│
├── ismcts.cpp                 # ISMCTS 實作
├── node.cpp                   # ISMCTS 使用的 node 結構
│
├── 4T_header.h                # 整理所有必要的 header 檔
│
├── gst-endgame.cpp            # 測試殘局表現：本地指定殘局盤面與 mcts 對打
├── gst.cpp                    # 本地與 mcts 對打指定次數場，觀察平均勝率
└── mcts.cpp                   # 本地測試的對手程式
```


## 編譯方式

## 連接 server 用的 exe 檔

記得要根據需求修改 `MyAI::Generate_move(char* move)` 的 best_move，或是 `ISMCTS::simulation(GST &state,DATA &d)` 的 move

4-tuple:

best_move 修改為 `int best_move = game.highest_weight(data);` 就是單純的 4-tuple

ismcts:

move 不使用 `move = simState.highest_weight(d);` 就能切斷與 4-tuple 的連結，變成單純的 ISMCTS

merge:

best_move 修改為 `int best_move = ismcts.findBestMove(game, data);` 就能兩種都用到

編譯指令如下 (名稱可修改) :

> g++ -o Tomorin_merge main.cpp MyAI.cpp ../4T_GST_impl.cpp ../4T_DATA_impl.cpp ../ismcts.cpp ../node.cpp -std=c++14 -O2


## 本地測試 (gst、gst-endgame)

要在 /src/server 編譯才讀得到 DATA 資料，不然會報錯

> g++ -std=c++14 -O2 ../gst.cpp ../ismcts.cpp ../mcts.cpp ../node.cpp ../4T_DATA_impl.cpp -o gst
>
> ./gst

> g++ -std=c++14 -O2 ../gst-endgame.cpp ../ismcts.cpp ../mcts.cpp ../node.cpp ../4T_DATA_impl.cpp -o gst_endgame
>
> ./gst_endgame

## 盤面相關

### 本程式使用的移動方向:
N: north
E: east
W: west
S: south

### 棋子代碼:
```
 0   h   g   f   e   0
 0   d   c   b   a   0
 0   0   0   0   0   0
 0   0   0   0   0   0
 0   A   B   C   D   0
 0   E   F   G   H   0
```
### 盤面位置:
```
 0   1   2   3   4   5
 6   7   8   9  10  11
12  13  14  15  16  17
18  19  20  21  22  23
24  25  26  27  28  29
30  31  32  33  34  35
```
棋子不在盤面上時使用 -1 表示

this program(x,y) = (pos/row, pos%col)

GPW used(x,y) = (pos%row, pos/col)

### server 接收的移動方向代號:
0: up
1: left
2: right
3: down
```
   0
 1 P 2
   3
```

### 電腦視角:
顏色代號: |1|:red |2|:blue
```
 0  -2  -2  -2  -1   0
 0  -1  -1  -1  -2   0
 0   0   0   0   0   0
 0   0   0   0   0   0
 0   1   1   1   2   0
 0   2   2   2   1   0
```
### 初始棋子位置:
```
-1  15  14  13  12  -1
-1  11  10   9   8  -1
-1  -1  -1  -1  -1  -1
-1  -1  -1  -1  -1  -1
-1   0   1   2   3  -1
-1   4   5   6   7  -1
```
### 使用者視角:
```
 <   h   g   f   e   >
 -   d   c   b   a   - 
 -   -   -   -   -   -
 -   -   -   -   -   -
 -   A   B   C   D   -
 -   E   F   G   H   -
```

### 編譯完成後的程式與server連接
[連接方式](./picture/server_connect_AI_path.png)

### 論文
[Information Set Monte Carlo Tree Search](https://eprints.whiterose.ac.uk/id/eprint/75048/1/CowlingPowleyWhitehouse2012.pdf)