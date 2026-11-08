#include <cerrno>
#include <csignal>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <sys/types.h>
#include <sys/wait.h>
#include <thread>
#include <chrono>
#include <cstdlib>
#include <unistd.h>
#include <vector>

namespace fs = std::filesystem;

namespace {
constexpr char kSource[] = "/opt/warden/source";
constexpr char kWarden[] = "/usr/local/bin/warden";
constexpr char kOldWarden[] = "/usr/local/bin/warden.old";
constexpr char kStagedWarden[] = "/usr/local/bin/warden.new";
constexpr char kInstalledRevision[] = "/var/lib/warden/warden.commit";

int run(const std::string& program, const std::vector<std::string>& arguments) {
    std::vector<char*> argv;
    argv.push_back(const_cast<char*>(program.c_str()));
    for (const auto& arg : arguments) argv.push_back(const_cast<char*>(arg.c_str()));
    argv.push_back(nullptr);
    const pid_t child = fork();
    if (child < 0) return -1;
    if (child == 0) {
        execvp(program.c_str(), argv.data());
        _exit(errno == ENOENT ? 127 : 126);
    }
    int status = 0;
    while (waitpid(child, &status, 0) < 0) if (errno != EINTR) return -1;
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

bool git_value(const std::vector<std::string>& args, std::string& value) {
    int fds[2];
    if (pipe(fds) != 0) return false;
    std::vector<char*> argv{const_cast<char*>("git")};
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
    while ((count = read(fds[0], buffer, sizeof(buffer))) > 0) value.append(buffer, count);
    close(fds[0]);
    int status = 0;
    while (waitpid(child, &status, 0) < 0) if (errno != EINTR) return false;
    while (!value.empty() && (value.back() == '\n' || value.back() == '\r')) value.pop_back();
    return WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

bool is_ancestor(const std::string& older, const std::string& newer) {
    const int result = run("git", {"-C", kSource, "merge-base", "--is-ancestor", older, newer});
    return result == 0;
}

void wait_for_process(pid_t pid) {
    while (kill(pid, 0) == 0 || errno == EPERM) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}

void set_resume_result(const std::string& result) {
    setenv("WARDEN_BOOTSTRAP_RESUMED", "1", 1);
    setenv("WARDEN_BOOTSTRAP_RESULT", result.c_str(), 1);
}

[[noreturn]] void resume_warden(const std::vector<std::string>& command,
                                const std::string& result) {
    set_resume_result(result);
    std::vector<char*> argv;
    argv.push_back(const_cast<char*>(kWarden));
    for (const auto& arg : command) argv.push_back(const_cast<char*>(arg.c_str()));
    argv.push_back(nullptr);
    execv(kWarden, argv.data());
    std::cerr << "Bootstrap error: cannot relaunch Warden: " << std::strerror(errno) << '\n';
    _exit(127);
}

bool compile_warden() {
    std::error_code ec;
    fs::remove(kStagedWarden, ec);
    if (ec) { std::cerr << "Bootstrap error: cannot clear staged executable: " << ec.message() << '\n'; return false; }
    if (run("g++", {"-std=c++17", "-O2", "/opt/warden/source/warden.cpp", "-o", kStagedWarden}) != 0) {
        std::cerr << "Bootstrap error: compiling the updated warden.cpp failed.\n";
        fs::remove(kStagedWarden, ec);
        return false;
    }
    fs::permissions(kStagedWarden, fs::perms::owner_all | fs::perms::group_read |
                    fs::perms::group_exec | fs::perms::others_read | fs::perms::others_exec,
                    fs::perm_options::replace, ec);
    if (ec) { std::cerr << "Bootstrap error: cannot set executable permissions: " << ec.message() << '\n'; return false; }
    return true;
}

int bootstrap(int argc, char** argv) {
    if (argc < 5 || std::string(argv[1]) != "--parent" || std::string(argv[3]) != "--mode") {
        std::cerr << "Usage: warden-bootstrap --parent PID --mode check|self [warden arguments...]\n";
        return 2;
    }
    const pid_t parent = static_cast<pid_t>(std::stol(argv[2]));
    const std::string mode = argv[4];
    if (mode != "check" && mode != "self") return 2;
    std::vector<std::string> command;
    for (int i = 5; i < argc; ++i) command.emplace_back(argv[i]);
    if (parent > 0) wait_for_process(parent);

    std::error_code ec;
    fs::remove(kOldWarden, ec);  // Retire the prior executable on the next bootstrap run.
    if (ec) std::cerr << "Bootstrap warning: cannot remove old Warden executable: " << ec.message() << '\n';

    if (run("git", {"-C", kSource, "pull", "--ff-only"}) != 0) {
        std::cerr << "Bootstrap: source update failed; keeping the installed Warden.\n";
        resume_warden(command, "error");
    }

    std::string current_revision, current_warden_blob, installed_revision, installed_warden_blob;
    if (!git_value({"-C", kSource, "rev-parse", "HEAD"}, current_revision)) {
        std::cerr << "Bootstrap: cannot read the source revision.\n";
        resume_warden(command, "error");
    }
    if (!git_value({"-C", kSource, "rev-parse", "HEAD:warden.cpp"}, current_warden_blob)) {
        std::cerr << "Bootstrap: cannot fingerprint warden.cpp.\n";
        resume_warden(command, "error");
    }
    std::ifstream revision_file(kInstalledRevision);
    std::getline(revision_file, installed_revision);
    std::getline(revision_file, installed_warden_blob);

    bool newer = installed_revision.empty();
    if (!newer) {
        const bool forward = installed_revision == current_revision || is_ancestor(installed_revision, current_revision);
        if (forward) {
            if (installed_warden_blob.empty() &&
                !git_value({"-C", kSource, "rev-parse", installed_revision + ":warden.cpp"}, installed_warden_blob)) {
                std::cerr << "Bootstrap: cannot fingerprint the installed Warden revision.\n";
                resume_warden(command, "error");
            }
            newer = installed_warden_blob != current_warden_blob;
        }
    }
    if (!newer) {
        std::cout << "Bootstrap: installed Warden is current; making no executable changes.\n";
        resume_warden(command, "current");
    }

    std::cout << "Bootstrap: newer warden.cpp found; compiling the new Warden.\n";
    if (!compile_warden()) resume_warden(command, "error");
    fs::remove(kOldWarden, ec);
    ec.clear();
    fs::rename(kWarden, kOldWarden, ec);
    if (ec) {
        std::cerr << "Bootstrap error: cannot preserve the current executable: " << ec.message() << '\n';
        resume_warden(command, "error");
    }
    fs::rename(kStagedWarden, kWarden, ec);
    if (ec) {
        std::error_code restore;
        fs::rename(kOldWarden, kWarden, restore);
        std::cerr << "Bootstrap error: cannot install the new executable: " << ec.message() << '\n';
        resume_warden(command, "error");
    }

    if (run(kWarden, {"init"}) != 0) {
        std::error_code rollback;
        fs::remove(kWarden, rollback);
        fs::rename(kOldWarden, kWarden, rollback);
        std::cerr << "Bootstrap error: new Warden initialization failed; restored the old executable.\n";
        resume_warden(command, "error");
    }
    std::ofstream marker(kInstalledRevision, std::ios::trunc);
    marker << current_revision << '\n' << current_warden_blob << '\n';
    if (!marker) {
        std::cerr << "Bootstrap error: cannot record installed revision.\n";
        resume_warden(command, "error");
    }
    std::cout << "Bootstrap: updated Warden installed and initialized.\n";
    resume_warden(command, "updated");
}
}  // namespace

int main(int argc, char** argv) {
    return bootstrap(argc, argv);
}
