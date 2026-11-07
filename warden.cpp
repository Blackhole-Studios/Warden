#include <cerrno>
#include <cstring>
#include <filesystem>
#include <iostream>
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
constexpr char kEtc[] = "/etc/warden";
constexpr char kData[] = "/var/lib/warden";
constexpr char kService[] = "/etc/systemd/system/warden.service";

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

bool valid_checkout() {
    std::string tree, remote;
    return git_output({"-C", kSource, "rev-parse", "--is-inside-work-tree"}, tree) && tree == "true" &&
           git_output({"-C", kSource, "remote", "get-url", "origin"}, remote) &&
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
                             "/var/lib/warden/logs"}) {
        fs::create_directories(path, ec);
        if (ec) {
            std::cerr << "Error: cannot create " << path << ": " << ec.message() << '\n';
            return false;
        }
    }
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
    fs::copy_file("/proc/self/exe", kExecutable, fs::copy_options::overwrite_existing, ec);
    if (ec) { std::cerr << "Error: cannot install " << kExecutable << ": " << ec.message() << '\n'; return false; }
    fs::permissions(kExecutable, fs::perms::owner_all | fs::perms::group_read |
                    fs::perms::group_exec | fs::perms::others_read | fs::perms::others_exec,
                    fs::perm_options::replace, ec);
    if (ec) { std::cerr << "Error: cannot set executable permissions: " << ec.message() << '\n'; return false; }
    return true;
}

int install() {
    if (!require_root()) return 1;
    std::error_code ec;
    if (fs::exists(kSource, ec)) {
        if (!valid_checkout()) { std::cerr << "Error: " << kSource << " exists but is not a Warden checkout; left untouched.\n"; return 1; }
        std::cout << "Warden source is already present. Use 'warden update' to update it.\n";
        return 1;
    }
    if (!ensure_directories()) return 1;
    std::cout << "Cloning Warden into " << kSource << "...\n";
    if (run("git", {"clone", kRepositoryUrl, kSource}) != 0) return 1;
    if (!deploy_code() || !install_executable()) return 1;
    std::cout << "Warden files installed. No service was installed because this checkout has no runnable Core or service unit yet.\n";
    return 0;
}

int update() {
    if (!require_root()) return 1;
    if (!valid_checkout()) { std::cerr << "Error: no valid Warden checkout at " << kSource << ". Run 'warden install' first.\n"; return 1; }
    std::cout << "Updating Warden source...\n";
    if (run("git", {"-C", kSource, "pull", "--ff-only"}) != 0) {
        std::cerr << "Error: update failed. Local changes may prevent a fast-forward update.\n";
        return 1;
    }
    if (!deploy_code()) return 1;
    std::cout << "Application code deployed. Compilation skipped: no build command is configured.\n";
    if (fs::exists(kService)) {
        if (run("systemctl", {"restart", "warden.service"}) != 0 ||
            run("systemctl", {"is-active", "--quiet", "warden.service"}) != 0) {
            std::cerr << "Error: Warden service did not restart successfully.\n";
            return 1;
        }
    } else {
        std::cout << "Service restart skipped: no service unit is installed.\n";
    }
    std::cout << "Warden update completed.\n";
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
    for (const char* path : {kExecutable, kCode, kSource}) {
        fs::remove_all(path, ec);
        if (ec) { std::cerr << "Error: cannot remove " << path << ": " << ec.message() << '\n'; return 1; }
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
                 "  install                 Install Warden\n"
                 "  update                  Update source and deploy code\n"
                 "  uninstall [--complete]  Remove Warden; --complete also removes config/data\n"
                 "  status                  Show installation and service status\n"
                 "  version                 Show bootstrap version\n"
                 "  help                    Show this help\n";
}
}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) { help(); return 0; }
    const std::string command = argv[1];
    if (command == "install" && argc == 2) return install();
    if (command == "update" && argc == 2) return update();
    if (command == "status" && argc == 2) return status();
    if (command == "version" && argc == 2) { std::cout << "Warden bootstrap (development)\n"; return 0; }
    if (command == "help" && argc == 2) { help(); return 0; }
    if (command == "uninstall" && argc == 2) return uninstall(false);
    if (command == "uninstall" && argc == 3 && std::string(argv[2]) == "--complete") return uninstall(true);
    std::cerr << "Error: unknown command or invalid arguments. Run 'warden help'.\n";
    return 2;
}
