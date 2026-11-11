#include "common.hpp"
#include <optional>
#include <fstream>
#include <sstream>
#include <algorithm>
#include "json.hpp"

namespace uu {
static fs::path base;
static Config options;
static std::mutex log_lock, device_lock;
static std::vector<Device> known;

std::wstring wide(std::string_view s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), int(s.size()), nullptr, 0);
    if (!n) throw std::runtime_error("invalid UTF-8");
    std::wstring out(n, 0);
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), int(s.size()), out.data(), n);
    return out;
}
std::string utf8(std::wstring_view s) {
    if (s.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, s.data(), int(s.size()), nullptr, 0, nullptr, nullptr);
    std::string out(n, 0);
    WideCharToMultiByte(CP_UTF8, 0, s.data(), int(s.size()), out.data(), n, nullptr, nullptr);
    return out;
}
fs::path module_folder(HMODULE module) {
    std::wstring path(32768, L'\0');
    auto size=GetModuleFileNameW(module, path.data(), DWORD(path.size()));
    if (!size || size>=path.size()) throw std::runtime_error("module path");
    path.resize(size);
    return fs::path(path).parent_path();
}
void set_root(fs::path p) { base = std::move(p); }
const fs::path& root() { return base; }
void log(std::string_view text) {
    std::lock_guard lock(log_lock);
    try {
        fs::create_directories(base / L"private");
        auto name = L"patch-" + std::to_wstring(GetCurrentProcessId()) + L".log";
        std::ofstream file(base / L"private" / name, std::ios::app);
        file << GetTickCount64() << " " << text << '\n';
    } catch (...) { }
}
static std::wstring quote(std::wstring_view s) {
    std::wstring out = L"\"";
    size_t slashes = 0;
    for (wchar_t c : s) {
        if (c == L'\\') { ++slashes; continue; }
        if (c == L'\"') out.append(slashes * 2 + 1, L'\\');
        else out.append(slashes, L'\\');
        slashes = 0;
        out += c;
    }
    out.append(slashes * 2, L'\\');
    return out + L'\"';
}
static std::wstring command(const fs::path& exe, const std::vector<std::wstring>& args) {
    std::wstring cmd = quote(exe.wstring());
    for (const auto& arg : args) { cmd += L' '; cmd += quote(arg); }
    return cmd;
}
ProcessResult run(const fs::path& exe, const std::vector<std::wstring>& args, unsigned timeout, size_t limit) {
    ProcessResult result;
    HANDLE read{}, write{};
    SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
    if (!CreatePipe(&read, &write, &sa, 0)) return result;
    SetHandleInformation(read, HANDLE_FLAG_INHERIT, 0);
    HANDLE input = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, OPEN_EXISTING, 0, nullptr);
    STARTUPINFOW si{sizeof(si)};
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = write; si.hStdError = write; si.hStdInput = input;
    PROCESS_INFORMATION pi{};
    auto cmd = command(exe, args);
    BOOL ok = CreateProcessW(exe.c_str(), cmd.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
    CloseHandle(write); CloseHandle(input);
    if (!ok) { CloseHandle(read); return result; }
    CloseHandle(pi.hThread);
    auto deadline = GetTickCount64() + timeout;
    bool finished = false;
    for (;;) {
        DWORD available = 0;
        if (!PeekNamedPipe(read, nullptr, 0, nullptr, &available, nullptr)) break;
        if (available) {
            char buf[8192]; DWORD got = 0;
            if (!ReadFile(read, buf, std::min<DWORD>(available, sizeof(buf)), &got, nullptr)) break;
            if (result.output.size() + got > limit) { TerminateProcess(pi.hProcess, 1); break; }
            result.output.append(buf, got);
            continue;
        }
        if (finished) break;
        finished = WaitForSingleObject(pi.hProcess, 10) == WAIT_OBJECT_0;
        if (GetTickCount64() > deadline) { TerminateProcess(pi.hProcess, 1); break; }
    }
    WaitForSingleObject(pi.hProcess, 1000);
    DWORD code = 1;
    GetExitCodeProcess(pi.hProcess, &code);
    result.code = int(code);
    CloseHandle(read); CloseHandle(pi.hProcess);
    return result;
}
HANDLE spawn(const fs::path& exe, const std::vector<std::wstring>& args) {
    SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
    HANDLE null = CreateFileW(L"NUL", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, OPEN_EXISTING, 0, nullptr);
    STARTUPINFOW si{sizeof(si)};
    si.dwFlags = STARTF_USESTDHANDLES; si.hStdInput = si.hStdOutput = si.hStdError = null;
    PROCESS_INFORMATION pi{};
    auto cmd = command(exe, args);
    BOOL ok = CreateProcessW(exe.c_str(), cmd.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
    CloseHandle(null);
    if (!ok) return nullptr;
    CloseHandle(pi.hThread);
    return pi.hProcess;
}
void read_config() {
    std::ifstream file(base / L"config.local.json");
    if (!file) throw std::runtime_error("config.local.json missing");
    auto j = nlohmann::json::parse(file);
    options.adb = wide(j.at("adb").get<std::string>());
    options.scrcpy = wide(j.at("scrcpy").get<std::string>());
    options.fps = std::clamp(j.value("fps", 30), 5, 60);
    options.max_size = std::clamp(j.value("max_size", 1920), 320, 3840);
    options.bit_rate = std::clamp(j.value("bit_rate", 8000000), 1000000, 40000000);
    if (!fs::is_regular_file(options.adb) || !fs::is_regular_file(options.scrcpy / L"scrcpy-server"))
        throw std::runtime_error("ADB or scrcpy path unavailable");
    std::ifstream mapping(base / L"private" / L"devices.json");
    if (mapping) {
        auto m = nlohmann::json::parse(mapping);
        std::lock_guard lock(device_lock);
        for (auto& d : m) known.push_back({d.at("index"), d.at("serial"), d.at("name"), false});
    }
}
const Config& config() { return options; }
void refresh_devices() {
    auto result = run(options.adb, {L"devices", L"-l"});
    if (result.code) { log("device discovery failed"); return; }
    std::vector<Device> discovered;
    auto previous=devices();
    bool changed=false;
    std::istringstream lines(result.output);
    std::string line;
    while (std::getline(lines, line)) {
        std::istringstream parts(line);
        std::string serial, state, item, model = "Android";
        parts >> serial >> state;
        if (state != "device") continue;
        while (parts >> item) if (item.starts_with("model:")) model = item.substr(6);
        auto prior=std::find_if(previous.begin(),previous.end(),[&](auto& d){return d.serial==serial;});
        int width=prior==previous.end()?0:prior->width,height=prior==previous.end()?0:prior->height;
        if(width<1 || height<1) {
            auto size=run(options.adb,{L"-s",wide(serial),L"shell",L"wm",L"size"},3000);
            auto at=size.output.rfind("size:");
            if(size.code==0 && at!=std::string::npos) {
                std::istringstream dimensions(size.output.substr(at+5));char separator{};
                if(!(dimensions>>width>>separator>>height) || separator!='x' || width<1 || height<1 || width>16384 || height>16384)width=height=0;
            }
        }
        discovered.push_back({0,serial,model,true,width,height});
    }
    std::lock_guard lock(device_lock);
    for (auto& d : known) d.online = false;
    for(auto& found:discovered) {
        auto& serial=found.serial;
        auto it = std::find_if(known.begin(), known.end(), [&](auto& d) { return d.serial == serial; });
        if (it != known.end()) { it->online=true;it->width=found.width;it->height=found.height;continue; }
        int index = 10000;
        for (auto& d : known) index = std::max(index, d.index + 1);
        if (index >= 20000) { log("device limit reached"); continue; }
        known.push_back({index, serial, "ADB " + std::to_string(index - 9999) + " - " + found.name, true,found.width,found.height});
        changed=true;
        log("discovered virtual instance " + std::to_string(index));
    }
    if(!changed)return;
    nlohmann::json saved = nlohmann::json::array();
    for (auto& d : known) saved.push_back({{"index",d.index},{"serial",d.serial},{"name",d.name}});
    fs::create_directories(base / L"private");
    // The local mapping contains device identifiers and is excluded from source control.
    std::ofstream out(base / L"private" / L"devices.json"); out << saved.dump(2);
}
std::vector<Device> devices() { std::lock_guard lock(device_lock); return known; }
std::optional<Device> device(int index) {
    std::lock_guard lock(device_lock);
    for (auto& d : known) if (d.index == index) return d;
    return {};
}
uint32_t be32(const uint8_t* p) { return uint32_t(p[0])<<24 | uint32_t(p[1])<<16 | uint32_t(p[2])<<8 | p[3]; }
uint64_t be64(const uint8_t* p) { return uint64_t(be32(p))<<32 | be32(p+4); }
void put16(uint8_t* p, uint16_t v) { p[0] = uint8_t(v>>8); p[1] = uint8_t(v); }
void put32(uint8_t* p, uint32_t v) { for (int i=3; i>=0; --i) {p[i]=uint8_t(v);v>>=8;} }
void put64(uint8_t* p, uint64_t v) { put32(p,uint32_t(v>>32)); put32(p+4,uint32_t(v)); }
}
