#include "sandbox.h"
#include <unistd.h>
#include <sys/wait.h>
#include <iostream>
#include <cstring>

/**
 * Store allowed paths.
 */
void Sandbox::set_allowed_paths(const std::vector<std::string>& paths) {
    allowed_paths_ = paths;
}

/**
 * Check if an executable path is allowed.
 * MVP rule:
 *   - cmd must start with one of the allowed directory prefixes.
 */
bool Sandbox::is_allowed(const std::string& cmd) const {
    for (const std::string& p : allowed_paths_) {
        if (cmd.rfind(p, 0) == 0) {
            return true;
        }
    }
    return false;
}

/**
 * Execute a command inside basic sandbox rules.
 * Uses fork + execvp.
 */
int Sandbox::exec(const std::vector<std::string>& argv) const {
    if (argv.empty()) {
        std::cerr << "[Sandbox] Error: empty argv\n";
        return -1;
    }

    std::string cmd = argv[0];
    if (!is_allowed(cmd)) {
        std::cerr << "[Sandbox] Denied: " << cmd
                  << " is not in allow-listed directories.\n";
        return -1;
    }

    pid_t pid = fork();
    if (pid < 0) {
        std::cerr << "[Sandbox] fork() failed: " << strerror(errno) << "\n";
        return -1;
    }

    if (pid == 0) {
        // Convert argv to execvp format
        std::vector<char*> c_argv;
        c_argv.reserve(argv.size() + 1);
        for (const std::string& s : argv) {
            c_argv.push_back(const_cast<char*>(s.c_str()));
        }
        c_argv.push_back(nullptr);

        execvp(c_argv[0], c_argv.data());

        // exec only returns on failure
        std::cerr << "[Sandbox] execvp() failed: " << strerror(errno) << "\n";
        _exit(127);
    }

    int status = 0;
    waitpid(pid, &status, 0);

    if (WIFEXITED(status)) {
        return WEXITSTATUS(status);
    }
    return -1;
}