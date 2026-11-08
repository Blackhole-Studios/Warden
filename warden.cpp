#include <cerrno>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <csignal>
#include <cstdlib>
#include <string>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

namespace fs = std::filesystem;

namespace {
constexpr char kRepositoryUrl[] = "https://github.com/Blackhole-Studios/Warden.git";
constexpr char kRoot[] = "/opt/warden";
constexpr char kSource[] = "/opt/warden/source";
constexpr char kCode[] = "/opt/warden/code";
constexpr char kExecutable[] = "/usr/local/bin/warden";
constexpr char kOldExecutable[] = "/usr/local/bin/warden.old";
constexpr char kStagedExecutable[] = "/usr/local/bin/warden.new";
constexpr char kBootstrapSource[] = "/opt/warden/source/warden-bootstrap.cpp";
constexpr char kBootstrapExecutable[] = "/usr/local/bin/warden-bootstrap";
constexpr char kStagedBootstrap[] = "/usr/local/bin/warden-bootstrap.new";
constexpr char kEtc[] = "/etc/warden";
constexpr char kData[] = "/var/lib/warden";
constexpr char kService[] = "/etc/systemd/system/warden.service";
constexpr char kCoreSource[] = "/opt/warden/code/core/main.cpp";
constexpr char kCoreDirectory[] = "/opt/warden/code/core";
constexpr char kCoreBinary[] = "/opt/warden/bin/core";
constexpr char kCoreHash[] = "/var/lib/warden/core.source-hash";
constexpr char kBootstrapHash[] = "/var/lib/warden/bootstrap.source-hash";
// Keep these values synchronized with code/core/stop_codes.hpp.
constexpr int kCoreUpdate = 10;
constexpr int kCoreRestart = 11;
constexpr int kCoreSafeExit = 12;
volatile std::sig_atomic_t g_stop_requested = 0;

void request_stop(int) { g_stop_requested = 1; }

std::string files_hash(const std::vector<std::string>& paths) {
    uint64_t hash = 14695981039346656037ULL;
    for (const auto& path : paths) {
        std::ifstream file(path, std::ios::binary);
        if (!file) return {};
        char byte;
        while (file.get(byte)) {
            hash ^= static_cast<unsigned char>(byte);
            hash *= 1099511628211ULL;
        }
        hash ^= 255;
        hash *= 1099511628211ULL;
    }
    std::ostringstream value;
    value << std::hex << hash;
    return value.str();
}

bool marker_matches(const char* marker_path, const std::string& value) {
    std::ifstream marker(marker_path);
    std::string recorded;
    return !value.empty() && std::getline(marker, recorded) && recorded == value;
}

bool save_marker(const char* marker_path, const std::string& value) {
    const std::string temporary = std::string(marker_path) + ".new";
    { std::ofstream marker(temporary, std::ios::trunc); marker << value << '\n';
      if (!marker) { std::cerr << "Error: cannot write build marker " << marker_path << ".\n"; return false; } }
    std::error_code ec;
    fs::rename(temporary, marker_path, ec);
    if (ec) { std::cerr << "Error: cannot install build marker: " << ec.message() << '\n'; return false; }
    return true;
}

int run(const std::string& program, const std::vector<std::string>& arguments) {
    std::vector<char*> argv;
    argv.reserve(arguments.size() + 2);
    argv.push_back(const_cast<char*>(program.c_str()));
    for (const auto& arg : arguments) argv.push_back(const_cast<char*>(arg.c_str()));
    argv.push_back(nullptr);

    const pid_t child = fork();
    if (child < 0) {
        std::cerr << "Error: could not start " << program << ": " << std::strerror(errno) << '\n';
        return -1;
    }
    if (child == 0) {
        execvp(program.c_str(), argv.data());
        std::cerr << "Error: could not execute " << program << ": " << std::strerror(errno) << '\n';
        _exit(errno == ENOENT ? 127 : 126);
    }
    int status = 0;
    while (waitpid(child, &status, 0) < 0) {
        if (errno == EINTR) continue;
        std::cerr << "Error: could not wait for " << program << ": " << std::strerror(errno) << '\n';
        return -1;
    }
    if (WIFEXITED(status)) return WEXITSTATUS(status);
    return -1;
}

bool git_output(const std::vector<std::string>& args, std::string& output) {
    int fds[2];
    if (pipe(fds) != 0) return false;
    std::vector<char*> argv;
    argv.push_back(const_cast<char*>("git"));
    for (const auto& arg : args) argv.push_back(const_cast<char*>(arg.c_str()));
    argv.push_back(nullptr);
    const pid_t child = fork();
    if (child < 0) { close(fds[0]); close(fds[1]); return false; }
    if (child == 0) {
        close(fds[0]);
        if (dup2(fds[1], STDOUT_FILENO) < 0) _exit(126);
        close(fds[1]);
        execvp("git", argv.data());
        _exit(errno == ENOENT ? 127 : 126);
    }
    close(fds[1]);
    char buffer[512];
    ssize_t count;
    while ((count = read(fds[0], buffer, sizeof(buffer))) > 0) output.append(buffer, count);
    close(fds[0]);
    int status = 0;
    while (waitpid(child, &status, 0) < 0) if (errno != EINTR) return false;
    while (!output.empty() && (output.back() == '\n' || output.back() == '\r')) output.pop_back();
    return WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

bool valid_checkout(const std::string& directory = kSource) {
    std::string tree, remote;
    return git_output({"-C", directory, "rev-parse", "--is-inside-work-tree"}, tree) && tree == "true" &&
           git_output({"-C", directory, "remote", "get-url", "origin"}, remote) &&
           (remote == kRepositoryUrl || remote == "https://github.com/Blackhole-Studios/Warden" ||
            remote == "git@github.com:Blackhole-Studios/Warden.git" ||
            remote == "git@github.com:Blackhole-Studios/Warden" ||
            remote == "ssh://git@github.com/Blackhole-Studios/Warden.git" ||
            remote == "ssh://git@github.com/Blackhole-Studios/Warden");
}

bool require_root() {
    if (geteuid() == 0) return true;
    std::cerr << "Error: this command changes system directories. Run it with sudo.\n";
    return false;
}

bool ensure_directories() {
    std::error_code ec;
    for (const char* path : {kRoot, kEtc, kData, "/var/lib/warden/models",
                             "/var/lib/warden/corpus", "/var/lib/warden/user-data",
                             "/var/lib/warden/logs", "/opt/warden/bin"}) {
        fs::create_directories(path, ec);
        if (ec) {
            std::cerr << "Error: cannot create " << path << ": " << ec.message() << '\n';
            return false;
        }
    }
    return true;
}

bool build_core() {
    std::error_code ec;
    if (!fs::is_regular_file(kCoreSource, ec) || ec) {
        std::cerr << "Error: Core source is missing at " << kCoreSource << ".\n";
        return false;
    }
    const std::string source_hash = files_hash({
        kCoreSource, "/opt/warden/code/core/core.cpp", "/opt/warden/code/core/core.hpp",
        "/opt/warden/code/core/stop_codes.hpp",
        "/opt/warden/code/core/input/input_manager.cpp", "/opt/warden/code/core/input/input_manager.hpp",
        "/opt/warden/code/core/context/context_manager.cpp", "/opt/warden/code/core/context/context_manager.hpp",
        "/opt/warden/code/core/routing/routing_manager.cpp", "/opt/warden/code/core/routing/routing_manager.hpp",
        "/opt/warden/code/core/response/response_manager.cpp", "/opt/warden/code/core/response/response_manager.hpp",
        "/opt/warden/code/core/output/output_manager.cpp", "/opt/warden/code/core/output/output_manager.hpp"});
    if (source_hash.empty()) { std::cerr << "Error: cannot fingerprint Core sources.\n"; return false; }
    if (access(kCoreBinary, X_OK) == 0 && marker_matches(kCoreHash, source_hash)) {
        std::cout << "Core is unchanged; skipping build.\n";
        return true;
    }
    const std::string staged = std::string(kCoreBinary) + ".new";
    fs::remove(staged, ec);
    if (ec) { std::cerr << "Error: cannot clear staged Core binary: " << ec.message() << '\n'; return false; }
    std::cout << "Building Core...\n";
    if (run("g++", {"-std=c++17", "-O2", "-pthread", "-I", kCoreDirectory,
                     kCoreSource, "/opt/warden/code/core/core.cpp",
                     "/opt/warden/code/core/input/input_manager.cpp",
                     "/opt/warden/code/core/context/context_manager.cpp",
                     "/opt/warden/code/core/routing/routing_manager.cpp",
                     "/opt/warden/code/core/response/response_manager.cpp",
                     "/opt/warden/code/core/output/output_manager.cpp", "-o", staged}) != 0) {
        std::cerr << "Error: Core build failed. The previous Core binary, if any, was preserved.\n";
        fs::remove(staged, ec);
        return false;
    }
    fs::permissions(staged, fs::perms::owner_all | fs::perms::group_read |
                    fs::perms::group_exec | fs::perms::others_read | fs::perms::others_exec,
                    fs::perm_options::replace, ec);
    if (ec) { std::cerr << "Error: cannot set Core permissions: " << ec.message() << '\n'; fs::remove(staged); return false; }
    fs::rename(staged, kCoreBinary, ec);
    if (ec) { std::cerr << "Error: cannot install Core binary: " << ec.message() << '\n'; fs::remove(staged); return false; }
    return save_marker(kCoreHash, source_hash);
}

bool install_bootstrap() {
    std::error_code ec;
    if (!fs::is_regular_file(kBootstrapSource, ec) || ec) {
        std::cerr << "Error: bootstrap source is missing at " << kBootstrapSource << ".\n";
        return false;
    }
    const std::string source_hash = files_hash({kBootstrapSource});
    if (source_hash.empty()) { std::cerr << "Error: cannot fingerprint bootstrap source.\n"; return false; }
    if (access(kBootstrapExecutable, X_OK) == 0 && marker_matches(kBootstrapHash, source_hash)) {
        std::cout << "Bootstrap is unchanged; skipping build.\n";
        return true;
    }
    const std::string staged = std::string(kBootstrapExecutable) + ".new";
    fs::remove(staged, ec);
    if (ec) { std::cerr << "Error: cannot clear staged bootstrap: " << ec.message() << '\n'; return false; }
    if (run("g++", {"-std=c++17", "-O2", kBootstrapSource, "-o", staged}) != 0) {
        std::cerr << "Error: bootstrap build failed.\n";
        fs::remove(staged, ec);
        return false;
    }
    fs::permissions(staged, fs::perms::owner_all | fs::perms::group_read |
                    fs::perms::group_exec | fs::perms::others_read | fs::perms::others_exec,
                    fs::perm_options::replace, ec);
    if (ec) { std::cerr << "Error: cannot set bootstrap permissions: " << ec.message() << '\n'; fs::remove(staged); return false; }
    fs::rename(staged, kBootstrapExecutable, ec);
    if (ec) { std::cerr << "Error: cannot install bootstrap: " << ec.message() << '\n'; fs::remove(staged); return false; }
    return save_marker(kBootstrapHash, source_hash);
}

bool launch_bootstrap(const std::string& mode, int argc, char** argv) {
    if (access(kBootstrapExecutable, X_OK) != 0) {
        std::cerr << "Error: Warden bootstrap is missing. Run 'sudo warden init' first.\n";
        return false;
    }
    const pid_t parent_pid = mode == "check" ? 0 : getpid();
    std::vector<std::string> values{kBootstrapExecutable, "--parent", std::to_string(parent_pid),
                                    "--mode", mode};
    for (int i = 1; i < argc; ++i) values.emplace_back(argv[i]);
    std::vector<char*> args;
    for (auto& value : values) args.push_back(value.data());
    args.push_back(nullptr);
    if (mode == "check") {
        execv(kBootstrapExecutable, args.data());
        std::cerr << "Error: cannot execute bootstrap: " << std::strerror(errno) << '\n';
        return false;
    }

    const pid_t child = fork();
    if (child < 0) {
        std::cerr << "Error: cannot start bootstrap: " << std::strerror(errno) << '\n';
        return false;
    }
    if (child == 0) {
        setsid();
        execv(kBootstrapExecutable, args.data());
        std::cerr << "Error: cannot execute bootstrap: " << std::strerror(errno) << '\n';
        _exit(127);
    }
    std::cout << "Warden is handing control to the bootstrap and will exit now.\n";
    return true;
}

bool deploy_code() {
    const fs::path source_code = fs::path(kSource) / "code";
    std::error_code ec;
    if (!fs::is_directory(source_code, ec) || ec) {
        std::cerr << "Error: source checkout has no code/ directory.\n";
        return false;
    }
    const fs::path staging = fs::path(kRoot) / ".code-staging";
    const fs::path backup = fs::path(kRoot) / ".code-previous";
    fs::remove_all(staging, ec);
    if (ec) { std::cerr << "Error: cannot clear staging directory: " << ec.message() << '\n'; return false; }
    fs::copy(source_code, staging, fs::copy_options::recursive | fs::copy_options::copy_symlinks, ec);
    if (ec) { std::cerr << "Error: cannot stage application code: " << ec.message() << '\n'; return false; }

    const bool had_code = fs::exists(kCode, ec);
    if (ec) { std::cerr << "Error: cannot inspect deployed code: " << ec.message() << '\n'; return false; }
    fs::remove_all(backup, ec);
    if (ec) { std::cerr << "Error: cannot clear previous code: " << ec.message() << '\n'; return false; }
    if (had_code) {
        fs::rename(kCode, backup, ec);
        if (ec) { std::cerr << "Error: cannot move current code aside: " << ec.message() << '\n'; return false; }
    }
    fs::rename(staging, kCode, ec);
    if (ec) {
        std::cerr << "Error: cannot deploy staged code: " << ec.message() << '\n';
        if (had_code) { std::error_code restore; fs::rename(backup, kCode, restore); }
        return false;
    }
    fs::remove_all(backup, ec);
    return true;
}

bool install_executable() {
    std::error_code ec;
    const std::string staged = kStagedExecutable;
    fs::remove(staged, ec);
    if (ec) { std::cerr << "Error: cannot clear staged Warden executable: " << ec.message() << '\n'; return false; }
    if (run("g++", {"-std=c++17", "-O2", fs::path(kSource).append("warden.cpp").string(), "-o", staged}) != 0) {
        std::cerr << "Error: compiling warden.cpp failed. The current executable was preserved.\n";
        fs::remove(staged, ec);
        return false;
    }
    fs::permissions(staged, fs::perms::owner_all | fs::perms::group_read |
                    fs::perms::group_exec | fs::perms::others_read | fs::perms::others_exec,
                    fs::perm_options::replace, ec);
    if (ec) { std::cerr << "Error: cannot set Warden permissions: " << ec.message() << '\n'; fs::remove(staged); return false; }
    fs::rename(staged, kExecutable, ec);
    if (ec) { std::cerr << "Error: cannot install " << kExecutable << ": " << ec.message() << '\n'; fs::remove(staged); return false; }
    return true;
}

int install() {
    if (!require_root()) return 1;
    const fs::path stage = fs::path("/opt") / (".warden-install-" + std::to_string(getpid()));
    const fs::path staged_source = stage / "source";
    std::error_code ec;
    fs::remove_all(stage, ec);
    if (ec) { std::cerr << "Error: cannot clear install staging directory: " << ec.message() << '\n'; return 1; }
    fs::create_directories(stage, ec);
    if (ec) { std::cerr << "Error: cannot create install staging directory: " << ec.message() << '\n'; return 1; }

    std::cout << "Fetching a clean Warden source checkout...\n";
    if (run("git", {"clone", kRepositoryUrl, staged_source.string()}) != 0) {
        fs::remove_all(stage, ec);
        std::cerr << "Error: could not fetch Warden source; existing installation was left untouched.\n";
        return 1;
    }
    for (const char* relative : {
             "warden.cpp", "warden-bootstrap.cpp", "code/core/main.cpp", "code/core/core.cpp",
             "code/core/core.hpp", "code/core/stop_codes.hpp",
             "code/core/input/input_manager.cpp", "code/core/input/input_manager.hpp",
             "code/core/context/context_manager.cpp", "code/core/context/context_manager.hpp",
             "code/core/routing/routing_manager.cpp", "code/core/routing/routing_manager.hpp",
             "code/core/response/response_manager.cpp", "code/core/response/response_manager.hpp",
             "code/core/output/output_manager.cpp", "code/core/output/output_manager.hpp"}) {
        if (!fs::is_regular_file(staged_source / relative, ec) || ec) {
            fs::remove_all(stage, ec);
            std::cerr << "Error: fetched source is incomplete (missing " << relative
                      << "); existing installation was left untouched.\n";
            return 1;
        }
    }

    fs::remove_all(kRoot, ec);
    if (ec) { fs::remove_all(stage); std::cerr << "Error: cannot remove old " << kRoot << ": " << ec.message() << '\n'; return 1; }
    fs::create_directories(kRoot, ec);
    if (ec) { fs::remove_all(stage); std::cerr << "Error: cannot recreate " << kRoot << ": " << ec.message() << '\n'; return 1; }
    fs::rename(staged_source, kSource, ec);
    if (ec) { fs::remove_all(stage); std::cerr << "Error: cannot install source checkout: " << ec.message() << '\n'; return 1; }
    fs::remove_all(stage, ec);

    if (!ensure_directories() || !deploy_code() || !install_bootstrap() ||
        !build_core() || !install_executable()) return 1;
    std::cout << "Warden and Core installed from a clean source checkout. No service unit is configured yet.\n";
    return 0;
}

int update(bool restart_service = true) {
    if (!require_root()) return 1;
    if (!valid_checkout()) { std::cerr << "Error: no valid Warden checkout at " << kSource << ". Run 'warden install' first.\n"; return 1; }
    std::string old_revision;
    if (!git_output({"-C", kSource, "rev-parse", "HEAD"}, old_revision)) {
        std::cerr << "Error: cannot read current source revision.\n";
        return 1;
    }
    std::cout << "Updating Warden source...\n";
    if (run("git", {"-C", kSource, "pull", "--ff-only"}) != 0) {
        std::cerr << "Error: update failed. Local changes may prevent a fast-forward update.\n";
        return 1;
    }
    std::string new_revision;
    if (!git_output({"-C", kSource, "rev-parse", "HEAD"}, new_revision)) {
        std::cerr << "Error: cannot read updated source revision.\n";
        return 1;
    }
    int code_diff = 0;
    if (old_revision != new_revision)
        code_diff = run("git", {"-C", kSource, "diff", "--quiet", old_revision,
                                 new_revision, "--", "code/"});
    if (code_diff < 0 || code_diff > 1) {
        std::cerr << "Error: cannot compare application source revisions.\n";
        return 1;
    }
    const bool code_changed = code_diff == 1 || !fs::is_directory(kCode);
    if (!install_bootstrap()) return 1;
    if (code_changed && !deploy_code()) return 1;
    if (!build_core()) return 1;
    std::cout << (code_changed ? "Application code deployed.\n" : "Application code is unchanged; deployment skipped.\n");
    if (restart_service && code_changed && fs::exists(kService)) {
        if (run("systemctl", {"restart", "warden.service"}) != 0 ||
            run("systemctl", {"is-active", "--quiet", "warden.service"}) != 0) {
            std::cerr << "Error: Warden service did not restart successfully.\n";
            return 1;
        }
    } else if (restart_service && code_changed) {
        std::cout << "Service restart skipped: no service unit is installed.\n";
    }
    std::cout << "Warden update completed.\n";
    return 0;
}

int init() {
    if (!require_root()) return 1;
    if (!ensure_directories()) return 1;
    std::error_code ec;
    if (!fs::is_regular_file(kCoreSource, ec) || ec) {
        if (!valid_checkout() || !deploy_code()) {
            std::cerr << "Error: application code is missing. Run 'warden install' first.\n";
            return 1;
        }
    }
    if (!build_core()) return 1;
    if (!install_bootstrap()) return 1;
    std::cout << "Warden is initialized and Core is ready.\n";
    return 0;
}

int start_core() {
    if (geteuid() != 0 && access(kCoreBinary, X_OK) != 0) {
        std::cerr << "Error: Core is not initialized. Run 'sudo warden init'.\n";
        return 1;
    }
    if (access(kCoreBinary, X_OK) != 0) {
        std::cerr << "Error: Core is not initialized. Run 'warden init'.\n";
        return 1;
    }

    struct sigaction action{};
    action.sa_handler = request_stop;
    sigemptyset(&action.sa_mask);
    sigaction(SIGINT, &action, nullptr);
    sigaction(SIGTERM, &action, nullptr);
    g_stop_requested = 0;

    while (!g_stop_requested) {
        const pid_t child = fork();
        if (child < 0) { std::cerr << "Error: could not start Core: " << std::strerror(errno) << '\n'; return 1; }
        if (child == 0) {
            struct sigaction defaults{};
            defaults.sa_handler = SIG_DFL;
            sigemptyset(&defaults.sa_mask);
            sigaction(SIGINT, &defaults, nullptr);
            sigaction(SIGTERM, &defaults, nullptr);
            execl(kCoreBinary, kCoreBinary, static_cast<char*>(nullptr));
            std::cerr << "Error: could not execute Core: " << std::strerror(errno) << '\n';
            _exit(127);
        }

        int status = 0;
        bool interrupted = false;
        if (g_stop_requested) {
            interrupted = true;
            kill(child, SIGTERM);
        }
        while (waitpid(child, &status, 0) < 0) {
            if (errno == EINTR) {
                if (g_stop_requested) {
                    interrupted = true;
                    kill(child, SIGTERM);
                }
                continue;
            }
            std::cerr << "Error: could not wait for Core: " << std::strerror(errno) << '\n';
            return 1;
        }
        if (interrupted || g_stop_requested) return 0;
        if (!WIFEXITED(status)) {
            std::cerr << "Error: Core terminated abnormally.\n";
            return 1;
        }
        const int code = WEXITSTATUS(status);
        if (code == kCoreSafeExit || code == 0) return 0;
        if (code == kCoreRestart) {
            std::cout << "Core requested restart. Restarting...\n";
            continue;
        }
        if (code == kCoreUpdate) {
            std::cout << "Core requested update.\n";
            if (update(false) != 0) return 1;
            continue;
        }
        std::cerr << "Error: Core exited with unrecognized stop code " << code << ".\n";
        return code > 0 ? code : 1;
    }
    return 0;
}

int status() {
    std::error_code ec;
    std::cout << "Source checkout: " << (valid_checkout() ? "installed" : "missing or invalid") << '\n';
    std::cout << "Application code: " << (fs::is_directory(kCode, ec) ? "deployed" : "missing") << '\n';
    std::cout << "Executable: " << (fs::exists(kExecutable, ec) ? "installed" : "missing") << '\n';
    if (fs::exists(kService)) {
        const int active = run("systemctl", {"is-active", "--quiet", "warden.service"});
        std::cout << "Service: " << (active == 0 ? "active" : "inactive") << '\n';
    } else std::cout << "Service: not installed (Core/service unit is not available yet)\n";
    return 0;
}

int uninstall(bool complete) {
    if (!require_root()) return 1;
    if (complete) {
        std::cerr << "Complete removal deletes Warden configuration and all data.\n"
                     "Type REMOVE WARDEN DATA to continue: ";
        std::string confirmation;
        std::getline(std::cin, confirmation);
        if (confirmation != "REMOVE WARDEN DATA") { std::cout << "Cancelled.\n"; return 1; }
    }
    if (fs::exists(kService)) {
        run("systemctl", {"disable", "--now", "warden.service"});
        std::error_code ec; fs::remove(kService, ec);
        if (ec) { std::cerr << "Error: cannot remove service unit: " << ec.message() << '\n'; return 1; }
        run("systemctl", {"daemon-reload"});
    }
    std::error_code ec;
    for (const char* path : {kExecutable, kOldExecutable, kStagedExecutable,
                             kBootstrapExecutable, kStagedBootstrap, kCoreBinary,
                             "/opt/warden/bin/core.new", kCode, kSource,
                             "/opt/warden/.code-staging", "/opt/warden/.code-previous"}) {
        fs::remove_all(path, ec);
        if (ec) { std::cerr << "Error: cannot remove " << path << ": " << ec.message() << '\n'; return 1; }
    }
    // Remove only empty install directories; leave unrelated files in place.
    for (const char* path : {"/opt/warden/bin", kRoot}) {
        ec.clear();
        fs::remove(path, ec);
    }
    if (complete) {
        fs::remove_all(kEtc, ec);
        if (ec) { std::cerr << "Error: cannot remove " << kEtc << ": " << ec.message() << '\n'; return 1; }
        fs::remove_all(kData, ec);
        if (ec) { std::cerr << "Error: cannot remove " << kData << ": " << ec.message() << '\n'; return 1; }
    }
    std::cout << (complete ? "Warden and its data were removed.\n" : "Warden removed; configuration and data were preserved.\n");
    return 0;
}

void help() {
    std::cout << "Warden system manager\n"
                 "Usage: warden <command> [options]\n\n"
                 "Commands:\n"
                 "  init                    Prepare directories and build Core\n"
                 "  start                   Run and supervise Core\n"
                 "  install                 Install Warden\n"
                 "  update [--self]         Update application, or bootstrap a Warden update\n"
                 "  uninstall [--complete]  Remove Warden; --complete also removes config/data\n"
                 "  status                  Show installation and service status\n"
                 "  version                 Show bootstrap version\n"
                 "  help                    Show this help\n";
}
}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) { help(); return 0; }
    const std::string command = argv[1];
    const char* resumed_value = std::getenv("WARDEN_BOOTSTRAP_RESUMED");
    const bool resumed = resumed_value != nullptr;
    const std::string bootstrap_result = std::getenv("WARDEN_BOOTSTRAP_RESULT")
                                             ? std::getenv("WARDEN_BOOTSTRAP_RESULT") : "";
    unsetenv("WARDEN_BOOTSTRAP_RESUMED");
    unsetenv("WARDEN_BOOTSTRAP_RESULT");
    if (!resumed && command == "start" && argc == 2) {
        return launch_bootstrap("check", argc, argv) ? 0 : 1;
    }
    if (resumed && command == "update" && argc == 3 && std::string(argv[2]) == "--self") {
        if (bootstrap_result == "error") return 1;
        std::cout << (bootstrap_result == "updated" ? "Warden self-update completed.\n"
                                                      : "Warden is already current.\n");
        return 0;
    }
    if (command == "init" && argc == 2) return init();
    if (command == "start" && argc == 2) return start_core();
    if (command == "install" && argc == 2) return install();
    if (command == "update" && argc == 2) return update();
    if (command == "update" && argc == 3 && std::string(argv[2]) == "--self") {
        if (!require_root()) return 1;
        return launch_bootstrap("self", argc, argv) ? 0 : 1;
    }
    if (command == "status" && argc == 2) return status();
    if (command == "version" && argc == 2) { std::cout << "Warden bootstrap (development)\n"; return 0; }
    if (command == "help" && argc == 2) { help(); return 0; }
    if (command == "uninstall" && argc == 2) return uninstall(false);
    if (command == "uninstall" && argc == 3 && std::string(argv[2]) == "--complete") return uninstall(true);
    std::cerr << "Error: unknown command or invalid arguments. Run 'warden help'.\n";
    return 2;
}
