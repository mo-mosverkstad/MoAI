#pragma once
#include <string>
#include <vector>

/**
 * Sandbox executes commands in a controlled environment.
 * MVP implementation:
 *   - No root privileges required
 *   - Only checks that requested command path is inside an allow-listed dir
 *   - Future versions may add Landlock or seccomp restrictions
 */
class Sandbox {
public:
    Sandbox() = default;

    /**
     * Specify allowed filesystem paths.
     * Only executable paths inside these directories are permitted.
     */
    void set_allowed_paths(const std::vector<std::string>& paths);

    /**
     * Execute command argv[0] argv[1] ... in a subprocess.
     * Returns the child exit code.
     */
    int exec(const std::vector<std::string>& argv) const;

private:
    bool is_allowed(const std::string& cmd) const;

private:
    std::vector<std::string> allowed_paths_;
};