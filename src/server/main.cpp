/**
 * @file main.cpp
 * @brief Main Entry Point for the AI Program.
 * * Implements the main loop for reading commands from stdin (Server),
 * * parsing them, and delegating tasks to the MyAI class.
 * @author Original Project Team (Inherited Code)
 * @author Chen You-Kai (Optimization & Docs)
 */

#include "MyAI.h"

// =============================
// Main Application Entry
// =============================

/**
 * @brief Main Loop: Continuously reads and processes server commands.
 * * Supported commands: MOV?, /exit, SET?, WON, LST, DRW, etc.
 * @return int Exit status (0 for success).
 */
int main() {
	// Seed random number generator
	srand(time(NULL));

	char read[1024], write[1024], output[2048], *token;
	const char *data[20];
	bool isFailed;

	// Create the AI agent instance
	MyAI myai;

	do {
		// Read command from stdin (Standard Input)
		if (fgets(read, 1024, stdin) == NULL) {
			fprintf(stderr, "Failed to read from stdin\n");
			break;
		}

		// Remove trailing newline character
		read[strlen(read) - 1] = '\0';

		// Parse command string into tokens
		int i = 0;

		// Check delimiter (comma or space)
		if (strchr(read, ',') != NULL) {
			// Comma-separated format
			token = strtok(read, ",");
			while (token != NULL) {
				data[i++] = token;
				token = strtok(NULL, ",");
			}
		} else {
			// Space-separated format
			token = strtok(read, " ");
			while (token != NULL) {
				data[i++] = token;
				token = strtok(NULL, " ");
			}
		}

		bool won = false;
		bool lost = false;
		bool draw = false;

		// Clear response buffer
		write[0] = '\0';

		// =============================
		// Command Dispatching
		// =============================
		if (strstr(data[0], "MOV?") != nullptr) {
			// Server requests a move
			myai.Get(data, write);
		} else if (!strcmp(data[0], "/exit")) {
			// Server requests termination
			myai.Exit(data, write);
			break;
		} else if (strstr(data[0], "WON") != nullptr) {
			// Game Won
			won = true;
		} else if (strstr(data[0], "LST") != nullptr) {
			// Game Lost
			lost = true;
		} else if (strstr(data[0], "DRW") != nullptr) {
			// Game Draw
			draw = true;
		} else if (strstr(data[0], "OK") != nullptr) {
			// Acknowledge (No action needed)
		} else if (strstr(data[0], "SET?") != nullptr) {
			// Setup phase: Choose Red pieces
			myai.Set(write);
		}

		// Format and send response to stdout (Standard Output)
		snprintf(output, 50, "%s\n", write);

		fprintf(stdout, "%s", output);
		fprintf(stderr, "%s", output);	// Log to stderr as well

		// Ensure output is sent immediately
		fflush(stdout);
		fflush(stderr);

	} while (true);

	return 0;
}