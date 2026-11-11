#pragma once
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>
#include <memory>
#include <mutex>
#include <atomic>
#include <functional>
#include <condition_variable>
#include <thread>
#include <chrono>
#include <cstdint>
#include <optional>

namespace uu {
namespace fs = std::filesystem;
std::wstring wide(std::string_view value);
std::string utf8(std::wstring_view value);
fs::path module_folder(HMODULE module = nullptr);
fs::path patch_folder(const fs::path& module_directory);
void set_root(fs::path root);
const fs::path& root();
void log(std::string_view message);
struct ProcessResult { int code = -1; std::string output; };
ProcessResult run(const fs::path& exe, const std::vector<std::wstring>& args,
                  unsigned timeout_ms = 10000, size_t limit = 8 * 1024 * 1024);
HANDLE spawn(const fs::path& exe, const std::vector<std::wstring>& args);
struct Config {
    fs::path adb;
    fs::path scrcpy;
    std::vector<std::string> connect;
    int fps = 30;
    int max_size = 1920;
    int bit_rate = 8000000;
};
const Config& config();
void read_config();
struct Device { int index; std::string serial, name; bool online = false; int width=0,height=0; };
void refresh_devices();
std::vector<Device> devices();
std::optional<Device> device(int index);
uint32_t be32(const uint8_t* p);
uint64_t be64(const uint8_t* p);
void put16(uint8_t* p, uint16_t v);
void put32(uint8_t* p, uint32_t v);
void put64(uint8_t* p, uint64_t v);
}
