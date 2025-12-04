/**
 * @file 4T_DATA_impl.cpp
 * @brief Implementation of the DATA class for N-Tuple network weights.
 * * Handles initialization, CSV parsing, and file I/O for weight tables (LUTs).
 * @author Original Project Team (Inherited Code)
 * @author Chen You-Kai (Optimization & Docs)
 */

#include "4T_DATA.hpp"
#include "4T_header.h"

// ==========================================
// Initialization
// ==========================================

/**
 * @brief Initializes all Look-Up Tables (LUTs) and the N-Tuple translation table.
 * * Sets default weights to 1, visit counts to 2, and win rates to 0.5.
 * * Pre-computes the position-to-feature mapping (trans array).
 */
void DATA::init_data() {
	for (int i = 0; i < TUPLE_NUM * FEATURE_NUM; i++) {
		// Initial weight 1/2 = 0.5
		LUTw_E[i] = 1;	   // Initial Enemy Weight
		LUTv_E[i] = 2;	   // Initial Enemy Visits
		LUTwr_E[i] = 0.5;  // Initial Enemy Win Rate
		LUTw_U[i] = 1;	   // Initial User Weight
		LUTv_U[i] = 2;	   // Initial User Visits
		LUTwr_U[i] = 0.5;  // Initial User Win Rate

		// Initialize specialized datasets (R1/B1)
		LUTw_E_R1[i] = 1;
		LUTv_E_R1[i] = 2;
		LUTwr_E_R1[i] = 0.5;
		LUTw_U_R1[i] = 1;
		LUTv_U_R1[i] = 2;
		LUTwr_U_R1[i] = 0.5;

		LUTw_E_B1[i] = 1;
		LUTv_E_B1[i] = 2;
		LUTwr_E_B1[i] = 0.5;
		LUTw_U_B1[i] = 1;
		LUTv_U_B1[i] = 2;
		LUTwr_U_B1[i] = 0.5;
	}

	int loca_num = 0;
	for (int i = 0; i < ROW * COL; i++) {  // 0~35
		int loc;
		// Encode all tuples on the board and store them in 'trans' for future lookup
		if (i % 6 <= 2) {  // 1x4 Pattern
			loca_num++;
			loc = i * 36 * 36 * 36 + (i + 1) * 36 * 36 + (i + 2) * 36 + (i + 3);
			trans[loc] = loca_num;
		}
		if (i < 18) {  // 4x1 Pattern
			loca_num++;
			loc = i * 36 * 36 * 36 + (i + 6) * 36 * 36 + (i + 12) * 36 + (i + 18);
			trans[loc] = loca_num;
		}
		if (i % 6 <= 4 && i < 30) {	 // 2x2 Pattern
			loca_num++;
			loc = i * 36 * 36 * 36 + (i + 1) * 36 * 36 + (i + 6) * 36 + (i + 7);
			trans[loc] = loca_num;
		}
	}
}

// ==========================================
// Helper Functions
// ==========================================

/**
 * @brief Helper: Splits a comma-separated string into a vector.
 * @param s The input CSV string.
 * @return std::vector<std::string> The list of tokens.
 */
std::vector<std::string> _csv(std::string s) {
	std::vector<std::string> arr;
	std::istringstream delim(s);
	std::string token;
	int c = 0;
	while (getline(delim, token, ',')) {
		arr.push_back(token);
		c++;
	}
	return arr;
}

// ==========================================
// File I/O (Standard Game)
// ==========================================

/**
 * @brief Loads weight data from CSV files and updates LUTs.
 */
void DATA::read_data_file(int num) {  // Reads data from CSV and writes to array
	std::string E_filename = "./data/Edata_" + std::to_string(num) + ".csv";
	std::ifstream iEdata(E_filename, std::ios::in);

	if (!iEdata) {
		printf("Add new Edata.csv\n");
		std::ofstream Edata;
		Edata.open("Edata.csv", std::ios::out | std::ios::trunc);
	} else {
		std::string line;
		bool first_line = 1;			 // Flag for header row
		while (getline(iEdata, line)) {	 // Read row by row
			if (first_line) {
				first_line = 0;
				continue;
				;
			}
			std::vector<std::string> a = _csv(line);
			LUTw_E[LUT_idx(std::stoll(a[0]), std::stoll(a[1]))] = std::stoll(a[2]);
			LUTv_E[LUT_idx(std::stoll(a[0]), std::stoll(a[1]))] = std::stoll(a[3]);
			LUTwr_E[LUT_idx(std::stoll(a[0]), std::stoll(a[1]))] = std::stof(a[4]);
		}
	}

	std::string U_filename = "./data/Udata_" + std::to_string(num) + ".csv";
	std::ifstream iUdata(U_filename, std::ios::in);

	if (!iUdata) {
		printf("Add new Udata.csv\n");
		std::ofstream Udata;
		Udata.open("Udata.csv", std::ios::out | std::ios::trunc);
	} else {
		std::string line;
		bool first_line = 1;			 // Flag for header row
		while (getline(iUdata, line)) {	 // Read row by row
			if (first_line) {
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

/**
 * @brief Saves current weights to CSV files for a specific run.
 */
void DATA::write_data_file_run(int run) {  // Open file & write file
	std::ofstream Edata, Udata;

	std::string E_filename = "data/Edata_" + std::to_string(run) + ".csv";
	std::string U_filename = "data/Udata_" + std::to_string(run) + ".csv";

	Edata.open(E_filename, std::ios::out | std::ios::trunc);
	Udata.open(U_filename, std::ios::out | std::ios::trunc);

	Edata << "location" << "," << "feature" << "," << "LUTw" << "," << "LUTv" << ","
		  << "4-tuple win rate" << std::endl;
	Udata << "location" << "," << "feature" << "," << "LUTw" << "," << "LUTv" << ","
		  << "4-tuple win rate" << std::endl;

	for (int i = 1; i <= TUPLE_NUM; i++) {
		for (int j = 0; j < FEATURE_NUM; j++) {
			Edata << i << "," << j << "," << LUTw_E[LUT_idx(i, j)] << "," << LUTv_E[LUT_idx(i, j)]
				  << "," << (float)LUTw_E[LUT_idx(i, j)] / (float)LUTv_E[LUT_idx(i, j)]
				  << std::endl;
		}
	}

	for (int i = 1; i <= TUPLE_NUM; i++) {
		for (int j = 0; j < FEATURE_NUM; j++) {
			Udata << i << "," << j << "," << LUTw_U[LUT_idx(i, j)] << "," << LUTv_U[LUT_idx(i, j)]
				  << "," << (float)LUTw_U[LUT_idx(i, j)] / (float)LUTv_U[LUT_idx(i, j)]
				  << std::endl;
		}
	}

	Edata.close();
	Udata.close();
}

// ==========================================
// File I/O (Specialized Scenarios: R1/B1)
// ==========================================

/**
 * @brief Loads weight data for R1 scenario (Enemy has 1 Red piece left).
 */
void DATA::read_data_file_R1(int num) {
	std::string E_filename = "./data R1/Edata_" + std::to_string(num) + ".csv";
	std::ifstream iEdata(E_filename, std::ios::in);

	if (!iEdata) {
		printf("Add new R1 Edata.csv\n");
		std::ofstream Edata;
		Edata.open("Edata.csv", std::ios::out | std::ios::trunc);
	} else {
		std::string line;
		bool first_line = 1;
		while (getline(iEdata, line)) {
			if (first_line) {
				first_line = 0;
				continue;
				;
			}
			std::vector<std::string> a = _csv(line);
			LUTw_E_R1[LUT_idx(std::stoll(a[0]), std::stoll(a[1]))] = std::stoll(a[2]);
			LUTv_E_R1[LUT_idx(std::stoll(a[0]), std::stoll(a[1]))] = std::stoll(a[3]);
			LUTwr_E_R1[LUT_idx(std::stoll(a[0]), std::stoll(a[1]))] = std::stof(a[4]);
		}
	}

	std::string U_filename = "./data R1/Udata_" + std::to_string(num) + ".csv";
	std::ifstream iUdata(U_filename, std::ios::in);

	if (!iUdata) {
		printf("Add new R1 Udata.csv\n");
		std::ofstream Udata;
		Udata.open("Udata.csv", std::ios::out | std::ios::trunc);
	} else {
		std::string line;
		bool first_line = 1;
		while (getline(iUdata, line)) {
			if (first_line) {
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

/**
 * @brief Saves R1 scenario weights to CSV files.
 */
void DATA::write_data_file_run_R1(int run) {
	std::ofstream Edata, Udata;

	std::string E_filename = "data R1/Edata_" + std::to_string(run) + ".csv";
	std::string U_filename = "data R1/Udata_" + std::to_string(run) + ".csv";

	Edata.open(E_filename, std::ios::out | std::ios::trunc);
	Udata.open(U_filename, std::ios::out | std::ios::trunc);

	Edata << "location" << "," << "feature" << "," << "LUTw" << "," << "LUTv" << ","
		  << "4-tuple win rate" << std::endl;
	Udata << "location" << "," << "feature" << "," << "LUTw" << "," << "LUTv" << ","
		  << "4-tuple win rate" << std::endl;

	for (int i = 1; i <= TUPLE_NUM; i++) {
		for (int j = 0; j < FEATURE_NUM; j++) {
			Edata << i << "," << j << "," << LUTw_E_R1[LUT_idx(i, j)] << ","
				  << LUTv_E_R1[LUT_idx(i, j)] << ","
				  << (float)LUTw_E_R1[LUT_idx(i, j)] / (float)LUTv_E_R1[LUT_idx(i, j)] << std::endl;
		}
	}

	for (int i = 1; i <= TUPLE_NUM; i++) {
		for (int j = 0; j < FEATURE_NUM; j++) {
			Udata << i << "," << j << "," << LUTw_U_R1[LUT_idx(i, j)] << ","
				  << LUTv_U_R1[LUT_idx(i, j)] << ","
				  << (float)LUTw_U_R1[LUT_idx(i, j)] / (float)LUTv_U_R1[LUT_idx(i, j)] << std::endl;
		}
	}

	Edata.close();
	Udata.close();
}

/**
 * @brief Loads weight data for B1 scenario (User has 1 Blue piece left).
 */
void DATA::read_data_file_B1(int num) {
	std::string E_filename = "./data B1/Edata_" + std::to_string(num) + ".csv";
	std::ifstream iEdata(E_filename, std::ios::in);

	if (!iEdata) {
		printf("Add new B1 Edata.csv\n");
		std::ofstream Edata;
		Edata.open("Edata.csv", std::ios::out | std::ios::trunc);
	} else {
		std::string line;
		bool first_line = 1;
		while (getline(iEdata, line)) {
			if (first_line) {
				first_line = 0;
				continue;
				;
			}
			std::vector<std::string> a = _csv(line);
			LUTw_E_B1[LUT_idx(std::stoll(a[0]), std::stoll(a[1]))] = std::stoll(a[2]);
			LUTv_E_B1[LUT_idx(std::stoll(a[0]), std::stoll(a[1]))] = std::stoll(a[3]);
			LUTwr_E_B1[LUT_idx(std::stoll(a[0]), std::stoll(a[1]))] = std::stof(a[4]);
		}
	}

	std::string U_filename = "./data B1/Udata_" + std::to_string(num) + ".csv";
	std::ifstream iUdata(U_filename, std::ios::in);

	if (!iUdata) {
		printf("Add new B1 Udata.csv\n");
		std::ofstream Udata;
		Udata.open("Udata.csv", std::ios::out | std::ios::trunc);
	} else {
		std::string line;
		bool first_line = 1;
		while (getline(iUdata, line)) {
			if (first_line) {
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

/**
 * @brief Saves B1 scenario weights to CSV files.
 */
void DATA::write_data_file_run_B1(int run) {
	std::ofstream Edata, Udata;

	std::string E_filename = "data B1/Edata_" + std::to_string(run) + ".csv";
	std::string U_filename = "data B1/Udata_" + std::to_string(run) + ".csv";

	Edata.open(E_filename, std::ios::out | std::ios::trunc);
	Udata.open(U_filename, std::ios::out | std::ios::trunc);

	Edata << "location" << "," << "feature" << "," << "LUTw" << "," << "LUTv" << ","
		  << "4-tuple win rate" << std::endl;
	Udata << "location" << "," << "feature" << "," << "LUTw" << "," << "LUTv" << ","
		  << "4-tuple win rate" << std::endl;

	for (int i = 1; i <= TUPLE_NUM; i++) {
		for (int j = 0; j < FEATURE_NUM; j++) {
			Edata << i << "," << j << "," << LUTw_E_B1[LUT_idx(i, j)] << ","
				  << LUTv_E_B1[LUT_idx(i, j)] << ","
				  << (float)LUTw_E_B1[LUT_idx(i, j)] / (float)LUTv_E_B1[LUT_idx(i, j)] << std::endl;
		}
	}

	for (int i = 1; i <= TUPLE_NUM; i++) {
		for (int j = 0; j < FEATURE_NUM; j++) {
			Udata << i << "," << j << "," << LUTw_U_B1[LUT_idx(i, j)] << ","
				  << LUTv_U_B1[LUT_idx(i, j)] << ","
				  << (float)LUTw_U_B1[LUT_idx(i, j)] / (float)LUTv_U_B1[LUT_idx(i, j)] << std::endl;
		}
	}

	Edata.close();
	Udata.close();
}