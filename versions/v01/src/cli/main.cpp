#include <iostream>
#include <string>
#include "commands.h"

/**
 * Entry point for the CLI application.
 * Delegates all command handling to run_cli() in commands.cpp.
 */
int main(int argc, char** argv) {
    try {
        return run_cli(argc, argv);
    } catch (const std::exception& ex) {
        std::cerr << "[FATAL] Uncaught exception: " << ex.what() << "\n";
        return 1;
    } catch (...) {
        std::cerr << "[FATAL] Unknown exception\n";
        return 1;
    }
}