/**
 * @file 4T_header.h
 * @brief Global constants, macros, and standard library includes.
 * * Defines the fundamental parameters for the 4T game, N-Tuple network,
 * * and platform-specific configurations.
 * @author Original Project Team (Inherited Code)
 * @author Chen You-Kai (Optimization & Docs)
 */

#ifndef FOUR_T_HEADER_H
#define FOUR_T_HEADER_H

// =============================
// Global Constants
// =============================

/// @name Board & Game Rules
/// @{
#define ROW 6			///< Number of rows on the board
#define COL 6			///< Number of columns on the board
#define PIECES 8		///< Number of pieces per player (Standard 4T has 8)
#define MAX_PLIES 1000	///< Maximum game length (plies/half-moves) to prevent infinite loops
#define MAX_MOVES 32	///< Maximum legal moves possible in a single turn
#define MAX_HAND 200	///< Maximum buffer size for move generation
/// @}

/// @name Piece & Player Definitions
/// @{
#define RED 1	   ///< Piece ID/Color: Red
#define BLUE 2	   ///< Piece ID/Color: Blue
#define UNKNOWN 3  ///< Piece ID/Color: Unknown (Fog of War)

#define USER 0	 ///< Player ID: The AI Agent (Me)
#define ENEMY 1	 ///< Player ID: The Opponent
/// @}

/// @name N-Tuple Network Constants
/// @{
#define POS_NUM 1537019	 ///< Total number of position encodings for 4-tuple patterns
#define FEATURE_NUM 256	 ///< Total feature size per pattern
#define TUPLE_NUM 61	 ///< Total number of defined 4-tuple patterns
/// @}

// =============================
// Standard Libraries & Platform Support
// =============================

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <algorithm>
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <memory>
#include <random>
#include <sstream>
#include <streambuf>
#include <string>
#include <unordered_map>
#include <vector>

// Platform-specific includes for directory handling and timing
#ifdef _WIN32
#include <direct.h>
#include <windows.h>
#elif defined(__APPLE__)
#include <mach/mach_time.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

// =============================
// Third-party Libraries
// =============================
#include "pcg_random.hpp"  ///< PCG Random Number Generator (Faster/Better than std::rand)

#endif	// FOUR_T_HEADER_H