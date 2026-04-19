#pragma once
#include <string>
#include <vector>

/**
 * Dispatch CLI commands.
 * Supports:
 *   mysearch ingest <path>
 *   mysearch search "<query>"
 *   mysearch run <cmd> [args...]
 */
int run_cli(int argc, char** argv);