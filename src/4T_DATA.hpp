/**
 * @file 4T_DATA.hpp
 * @brief Definition of the DATA class for N-Tuple network weights.
 * * Manages Look-Up Tables (LUTs) for feature weights and handles file I/O.
 * @author Original Project Team (Inherited Code)
 * @author Chen You-Kai (Optimization & Docs)
 */

#ifndef DATA_HPP
#define DATA_HPP

#include "4T_header.h"

class GST;

/**
 * @class DATA
 * @brief Manages the 4-Tuple Network weights and feature data.
 * * Stores large arrays of weights (LUTs) trained for different game phases.
 * * Handles loading/saving weights from/to disk.
 */
class DATA {
   public:
	/// @name Basic Look-Up Tables (Standard Game)
	/// @{
	// LUTw: Win counts, LUTv: Visit counts, LUTwr: Win Rate (Weight)
	// _E: Enemy perspective, _U: User perspective
	unsigned long long LUTw_E[TUPLE_NUM * FEATURE_NUM + 1] = {0};
	unsigned long long LUTv_E[TUPLE_NUM * FEATURE_NUM + 1] = {0};
	unsigned long long LUTw_U[TUPLE_NUM * FEATURE_NUM + 1] = {0};
	unsigned long long LUTv_U[TUPLE_NUM * FEATURE_NUM + 1] = {0};

	float LUTwr_U[TUPLE_NUM * FEATURE_NUM + 1] = {0.0};
	float LUTwr_E[TUPLE_NUM * FEATURE_NUM + 1] = {0.0};
	/// @}

	/// @name Specialized Look-Up Tables (Endgame Scenarios)
	/// @{
	// R1: Scenario where Enemy has only 1 Red piece left
	// B1: Scenario where User has only 1 Blue piece left

	// Enemy weights for R1/B1 scenarios
	unsigned long long LUTw_E_R1[TUPLE_NUM * FEATURE_NUM + 1] = {0},
														   LUTv_E_R1[TUPLE_NUM * FEATURE_NUM + 1] =
															   {0};
	unsigned long long LUTw_E_B1[TUPLE_NUM * FEATURE_NUM + 1] = {0},
														   LUTv_E_B1[TUPLE_NUM * FEATURE_NUM + 1] =
															   {0};

	// User weights for R1/B1 scenarios
	unsigned long long LUTw_U_R1[TUPLE_NUM * FEATURE_NUM + 1] = {0},
														   LUTv_U_R1[TUPLE_NUM * FEATURE_NUM + 1] =
															   {0};
	unsigned long long LUTw_U_B1[TUPLE_NUM * FEATURE_NUM + 1] = {0},
														   LUTv_U_B1[TUPLE_NUM * FEATURE_NUM + 1] =
															   {0};

	// Pre-calculated win rates for specialized scenarios
	float LUTwr_U_B1[TUPLE_NUM * FEATURE_NUM + 1] = {0.0},
											   LUTwr_E_B1[TUPLE_NUM * FEATURE_NUM + 1] = {0.0};
	float LUTwr_U_R1[TUPLE_NUM * FEATURE_NUM + 1] = {0.0},
											   LUTwr_E_R1[TUPLE_NUM * FEATURE_NUM + 1] = {0.0};
	/// @}

	/// @name Feature Mapping
	/// @{
	int trans[POS_NUM + 1] = {0};  ///< Translation table: Position encoding -> N-Tuple Index
	/// @}

	// =============================
	// Core Methods
	// =============================

	/**
	 * @brief Initializes all data structures and LUTs to zero.
	 */
	void init_data();

	/**
	 * @brief Computes the index in the Linear LUT array.
	 * @param location The N-Tuple location index (0 ~ TUPLE_NUM-1).
	 * @param feature The feature pattern index (0 ~ FEATURE_NUM-1).
	 * @return int The flattened index for accessing LUT arrays.
	 */
	int LUT_idx(int location, int feature) { return (location - 1) * FEATURE_NUM + feature; }

	/**
	 * @brief Loads weight data from binary files.
	 * @param num The iteration number/ID of the weight file to load.
	 */
	void read_data_file(int num);

	/**
	 * @brief Saves current weights to binary files.
	 * @param run The iteration number/ID to tag the saved file.
	 */
	void write_data_file_run(int run);

	/// @name Specialized I/O Methods (R1/B1 Scenarios)
	/// @{
	void read_data_file_R1(int num);
	void write_data_file_run_R1(int run);
	void read_data_file_B1(int num);
	void write_data_file_run_B1(int run);
	/// @}
};

#endif	// DATA_HPP