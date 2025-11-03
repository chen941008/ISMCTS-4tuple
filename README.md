## 概述

幽靈棋 4-tuple + ISMCTS 合併版，可正常編譯連接伺服器的執行檔，同時提供本地測試程式

### 快速使用指南

本程式支援三種 AI 選擇策略：

1. **Softmax 機率抽樣** (`SELECTION_MODE=2`, 預設) - 適合需要探索性的對局
2. **線性權重抽樣** (`SELECTION_MODE=1`) - 平衡探索與利用
3. **Argmax 直接選擇** (`SELECTION_MODE=0`) - 總是選擇最佳動作，適合追求最高勝率

編譯時添加 `-DSELECTION_MODE=X` 參數來選擇模式，例如：
```bash
# 在 src/server/ 目錄下
g++ -o Tomorin_softmax main.cpp MyAI.cpp ../4T_GST_impl.cpp ../4T_DATA_impl.cpp ../ismcts.cpp ../node.cpp -std=c++14 -O2 -DSELECTION_MODE=2
```

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

### 選擇策略模式

程式支援三種選擇策略，可在編譯時通過 `-DSELECTION_MODE=X` 指定：

- **SELECTION_MODE=2** (預設): **Softmax 機率抽樣** - 使用溫度參數的 softmax 分佈進行機率性選擇
- **SELECTION_MODE=1**: **線性權重抽樣** - 按權重比例進行機率性選擇（負權重會自動平移）
- **SELECTION_MODE=0**: **Argmax 直接選擇** - 直接選擇權重最高的動作

### 編譯不同模式的執行檔

**Softmax 模式（預設）:**
```bash
g++ -o Tomorin_softmax main.cpp MyAI.cpp ../4T_GST_impl.cpp ../4T_DATA_impl.cpp ../ismcts.cpp ../node.cpp -std=c++14 -O2 -DSELECTION_MODE=2
```

**線性權重模式:**
```bash
g++ -o Tomorin_linear main.cpp MyAI.cpp ../4T_GST_impl.cpp ../4T_DATA_impl.cpp ../ismcts.cpp ../node.cpp -std=c++14 -O2 -DSELECTION_MODE=1
```

**Argmax 模式:**
```bash
g++ -o Tomorin_argmax main.cpp MyAI.cpp ../4T_GST_impl.cpp ../4T_DATA_impl.cpp ../ismcts.cpp ../node.cpp -std=c++14 -O2 -DSELECTION_MODE=0
```

### AI 組合模式

4-tuple:

best_move 修改為 `int best_move = game.highest_weight(data);` 就是單純的 4-tuple

ismcts:

move 不使用 `move = simState.highest_weight(d);` 就能切斷與 4-tuple 的連結，變成單純的 ISMCTS

merge:

best_move 修改為 `int best_move = ismcts.findBestMove(game, data);` 就能兩種都用到

### 通用編譯指令（使用預設 softmax 模式）

> g++ -o Tomorin_merge main.cpp MyAI.cpp ../4T_GST_impl.cpp ../4T_DATA_impl.cpp ../ismcts.cpp ../node.cpp -std=c++14 -O2


## 本地測試 (gst、gst-endgame)

要在 /src/server 編譯才讀得到 DATA 資料，不然會報錯

### 編譯不同選擇模式的本地測試

**Softmax 模式（預設）:**
```bash
g++ -std=c++14 -O2 ../gst.cpp ../ismcts.cpp ../mcts.cpp ../node.cpp ../4T_DATA_impl.cpp -o gst_softmax -DSELECTION_MODE=2
./gst_softmax
```

**線性權重模式:**
```bash
g++ -std=c++14 -O2 ../gst.cpp ../ismcts.cpp ../mcts.cpp ../node.cpp ../4T_DATA_impl.cpp -o gst_linear -DSELECTION_MODE=1
./gst_linear
```

**Argmax 模式:**
```bash
g++ -std=c++14 -O2 ../gst.cpp ../ismcts.cpp ../mcts.cpp ../node.cpp ../4T_DATA_impl.cpp -o gst_argmax -DSELECTION_MODE=0
./gst_argmax
```

### 殘局測試編譯

**Softmax 模式:**
```bash
g++ -std=c++14 -O2 ../gst-endgame.cpp ../ismcts.cpp ../mcts.cpp ../node.cpp ../4T_DATA_impl.cpp -o gst_endgame_softmax -DSELECTION_MODE=2
./gst_endgame_softmax
```

**線性權重模式:**
```bash
g++ -std=c++14 -O2 ../gst-endgame.cpp ../ismcts.cpp ../mcts.cpp ../node.cpp ../4T_DATA_impl.cpp -o gst_endgame_linear -DSELECTION_MODE=1
./gst_endgame_linear
```

**Argmax 模式:**
```bash
g++ -std=c++14 -O2 ../gst-endgame.cpp ../ismcts.cpp ../mcts.cpp ../node.cpp ../4T_DATA_impl.cpp -o gst_endgame_argmax -DSELECTION_MODE=0
./gst_endgame_argmax
```

### 通用編譯指令（使用預設 softmax 模式）

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