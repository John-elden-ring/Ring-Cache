#pragma once

#include "ringcache/network_client.hpp"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <csignal>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

#include <chrono>
#include <filesystem>
#include <string>
#include <thread>
#include <vector>

namespace ringcache::test {

/// @brief RAII wrapper for managing a spawned child process across Windows and POSIX.
struct ChildProcess {
#ifdef _WIN32
    HANDLE hProcess{nullptr};
    HANDLE hThread{nullptr};
    DWORD pid{0};
#else
    pid_t pid{-1};
#endif

    bool start(const std::string& commandLine) {
#ifdef _WIN32
        STARTUPINFOA si{};
        PROCESS_INFORMATION pi{};
        si.cb = sizeof(si);

        std::vector<char> cmdBuf(commandLine.begin(), commandLine.end());
        cmdBuf.push_back('\0');

        BOOL ok = CreateProcessA(
            nullptr,
            cmdBuf.data(),
            nullptr,
            nullptr,
            FALSE,
            CREATE_NO_WINDOW,
            nullptr,
            nullptr,
            &si,
            &pi
        );
        if (!ok) {
            return false;
        }
        hProcess = pi.hProcess;
        hThread = pi.hThread;
        pid = pi.dwProcessId;
        return true;
#else
        pid_t p = fork();
        if (p < 0) {
            return false;
        }
        if (p == 0) {
            // Put child process in its own process group
            setpgid(0, 0);
            // Prefix with exec so /bin/sh is replaced by the node process
            std::string execCmd = "exec " + commandLine;
            execl("/bin/sh", "sh", "-c", execCmd.c_str(), (char*)nullptr);
            _exit(1);
        }
        pid = p;
        return true;
#endif
    }

    void stop() {
#ifdef _WIN32
        if (hProcess) {
            TerminateProcess(hProcess, 0);
            WaitForSingleObject(hProcess, 2000);
            CloseHandle(hProcess);
            hProcess = nullptr;
        }
        if (hThread) {
            CloseHandle(hThread);
            hThread = nullptr;
        }
#else
        if (pid > 0) {
            // Signal the entire process group and the process directly
            kill(-pid, SIGTERM);
            kill(pid, SIGTERM);

            // Bounded wait using WNOHANG to avoid indefinite hangs
            auto start = std::chrono::steady_clock::now();
            bool exited = false;
            while (std::chrono::steady_clock::now() - start < std::chrono::milliseconds(1500)) {
                int status = 0;
                pid_t res = waitpid(pid, &status, WNOHANG);
                if (res == pid || res == -1) {
                    exited = true;
                    break;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(25));
            }

            // Force kill if process did not exit within timeout
            if (!exited) {
                kill(-pid, SIGKILL);
                kill(pid, SIGKILL);
                int status = 0;
                waitpid(pid, &status, 0);
            }
            pid = -1;
        }
#endif
    }

    ~ChildProcess() {
        stop();
    }
};

/// @brief Locates the ringcache_node binary relative to the current working directory or test directory.
inline std::string findNodeBinary() {
    const std::vector<std::filesystem::path> candidates = {
        "ringcache_node.exe",
        "ringcache_node",
        "../ringcache_node.exe",
        "../ringcache_node",
        "build/ringcache_node.exe",
        "build/ringcache_node",
        "./build/ringcache_node.exe",
        "./build/ringcache_node",
        "bin/ringcache_node.exe",
        "bin/ringcache_node",
        "../bin/ringcache_node.exe",
        "../bin/ringcache_node"
    };

    for (const auto& path : candidates) {
        if (std::filesystem::exists(path)) {
            return std::filesystem::absolute(path).string();
        }
    }
    return "";
}

/// @brief Probes a node's TCP port until it answers PING or exceeds the timeout.
inline bool waitForNodeReady(uint16_t port, std::chrono::milliseconds timeout = std::chrono::milliseconds(3000)) {
    auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        NetworkClient client("127.0.0.1", port, std::chrono::milliseconds(100));
        if (client.ping()) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    return false;
}

} // namespace ringcache::test
