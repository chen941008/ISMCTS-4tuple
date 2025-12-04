#include "MyAI.h"

// =============================
// 主程式入口：伺服器指令循環
// =============================
int main() {
	srand(time(NULL));
	char read[1024], write[1024], output[2048], *token;
	const char *data[20];
	bool isFailed;
	MyAI myai;	// 建立 AI 物件
	do {
		// 讀取指令
		if (fgets(read, 1024, stdin) == NULL) {
			fprintf(stderr, "Failed to read from stdin\n");
			break;
		}

		read[strlen(read) - 1] = '\0';
		// 解析指令資料
		int i = 0;

		// 判斷分隔符（逗號或空格）
		if (strchr(read, ',') != NULL) {
			// 以逗號分隔
			token = strtok(read, ",");
			while (token != NULL) {
				data[i++] = token;
				token = strtok(NULL, ",");
			}
		} else {
			// 以空格分隔
			token = strtok(read, " ");
			while (token != NULL) {
				data[i++] = token;
				token = strtok(NULL, " ");
			}
		}
		bool won = false;
		bool lost = false;
		bool draw = false;
		// 清空回應字串
		write[0] = '\0';

		// =============================
		// 指令分流
		// =============================
		if (strstr(data[0], "MOV?") != nullptr) {
			myai.Get(data, write);	// 取得移動
		} else if (!strcmp(data[0], "/exit")) {
			myai.Exit(data, write);	 // 離開
			break;
		} else if (strstr(data[0], "WON") != nullptr) {
			won = true;
		} else if (strstr(data[0], "LST") != nullptr) {
			lost = true;
		} else if (strstr(data[0], "DRW") != nullptr) {
			draw = true;
		} else if (strstr(data[0], "OK") != nullptr) {
		} else if (strstr(data[0], "SET?") != nullptr) {
			myai.Set(write);  // 設定紅棋
		}

		snprintf(output, 50, "%s\n", write);

		fprintf(stdout, "%s", output);
		fprintf(stderr, "%s", output);

		fflush(stdout);
		fflush(stderr);

	} while (true);

	return 0;
}
