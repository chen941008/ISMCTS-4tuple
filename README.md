# 幽靈棋 AI — 4-tuple + ISMCTS 合併版

## 🧑‍💻 專案作者與維護者

本專案基於前人研究成果，後續由現任維護者優化整合。

### **原始作者團隊（Original Project Team）**

以下為本專案最初版本的開發者：

- **楊淳亘**
- **蘇亭伃**
- **張可欣**
- **周子馨**

（負責原始 4-tuple 架構、部分 AI 程式邏輯與資料格式、ISMCTS 整合與優化）

### **現任維護者（Current Maintainer）**

- **Chen You-Kai**  
  ‣ ISMCTS 整合與優化  
  ‣ 4-tuple + ISMCTS Hybrid Merge  
  ‣ 程式重構、效能提升  
  ‣ 文件（README）撰寫與維護

---

# 📘 概述

本專案整合 **4-tuple 神經特徵評估** 與 **ISMCTS（Information Set Monte Carlo Tree Search）**，提供：

- 可直接編譯、可連接 server 的 AI 執行檔
- 多種選擇策略（softmax / 線性權重 / argmax）
- 本地測試程式（一般對局 / 殘局測試）

---

## 🚀 快速開始

本程式提供 3 種 AI「動作選擇策略」：

| SELECTION_MODE | 策略         | 說明           |
| -------------- | ------------ | -------------- |
| **2（預設）**  | Softmax 抽樣 | 最具探索性     |
| **1**          | 線性權重抽樣 | 權衡探索與利用 |
| **0**          | Argmax       | 每次都選最高分 |

編譯時透過 `-DSELECTION_MODE=X` 指定策略，例如：

```bash
cd src/server
g++ -o Tomorin_softmax main.cpp MyAI.cpp ../4T_GST_impl.cpp ../4T_DATA_impl.cpp ../ismcts.cpp ../node.cpp -std=c++14 -O2 -DSELECTION_MODE=2
```

---

## 📁 專案架構

```
src/
├── server/
│   ├── data/
│   ├── main.cpp
│   └── MyAI.cpp
│
├── 4T_DATA_impl.cpp
├── 4T_GST_impl.cpp
├── pcg_xxx.hpp
│
├── ismcts.cpp
├── node.cpp
│
├── 4T_header.h
│
├── gst.cpp
├── gst-endgame.cpp
└── mcts.cpp
```

---

## 🧱 編譯方式

### 📌 Server 版本（連線到平台）

### 1. Softmax（預設）

```bash
g++ -o Tomorin_softmax main.cpp MyAI.cpp ../4T_GST_impl.cpp ../4T_DATA_impl.cpp ../ismcts.cpp ../node.cpp -std=c++14 -O2 -DSELECTION_MODE=2
```

### 2. 線性權重

```bash
g++ -o Tomorin_linear main.cpp MyAI.cpp ../4T_GST_impl.cpp ../4T_DATA_impl.cpp ../ismcts.cpp ../node.cpp -std=c++14 -O2 -DSELECTION_MODE=1
```

### 3. Argmax

```bash
g++ -o Tomorin_argmax main.cpp MyAI.cpp ../4T_GST_impl.cpp ../4T_DATA_impl.cpp ../ismcts.cpp ../node.cpp -std=c++14 -O2 -DSELECTION_MODE=0
```

---

## 🔧 AI 模式切換

| 模式             | 修改位置                                       | 說明             |
| ---------------- | ---------------------------------------------- | ---------------- |
| **純 4-tuple**   | `best_move = game.highest_weight(data);`       | 無 MCTS          |
| **純 ISMCTS**    | 移除 `move = simState.highest_weight(d);`      | 不依賴 4-tuple   |
| **合併（預設）** | `best_move = ismcts.findBestMove(game, data);` | 4-tuple + ISMCTS |

---

## 🧪 本地測試（gst / gst-endgame）

### 必須在 `src/server/` 中編譯才能讀取 data/

### gst（大量對局）

Softmax：

```bash
g++ -std=c++14 -O2 ../gst.cpp ../ismcts.cpp ../mcts.cpp ../node.cpp ../4T_DATA_impl.cpp -o gst_softmax -DSELECTION_MODE=2
./gst_softmax
```

---

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

---

## 🔌 Server 連線

見：`./picture/server_connect_AI_path.png`

---

## 📚 論文

[Information Set Monte Carlo Tree Search](https://eprints.whiterose.ac.uk/id/eprint/75048/1/CowlingPowleyWhitehouse2012.pdf)
