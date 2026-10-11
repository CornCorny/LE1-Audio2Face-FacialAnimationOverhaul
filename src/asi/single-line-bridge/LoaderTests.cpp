#include <Windows.h>
#include <cstdio>
#include <filesystem>
#include <string>

// Exercise the real DLL's public loader contract in a separate process.
// No game process is opened. Attach must reject this test EXE before hooks.
int wmain(int argc, wchar_t** argv) {
    if (argc != 2) return 2;
    auto module = LoadLibraryW(argv[1]);
    if (!module) { std::printf("FAIL: LoadLibrary error %lu\n", GetLastError()); return 1; }
    const char* required[] = {"SpiSupportDecl", "SpiShouldPreload", "SpiShouldSpawnThread", "SpiOnAttach", "SpiOnDetach"};
    for (auto name : required) {
        if (!GetProcAddress(module, name)) { std::printf("FAIL: missing required export %s\n", name); return 1; }
    }
    using Support = void(*)(wchar_t**, wchar_t**, wchar_t**, int*, int*);
    using Toggle = bool(*)();
    using Callback = bool(*)(void*);
    wchar_t *name=nullptr, *author=nullptr, *version=nullptr;
    int games=0, spi=0;
    reinterpret_cast<Support>(GetProcAddress(module,"SpiSupportDecl"))(&name,&author,&version,&games,&spi);
    if (!name || !author || !version || games != 2 || spi < 1 || spi > 3 ||
        reinterpret_cast<Toggle>(GetProcAddress(module,"SpiShouldPreload"))() ||
        reinterpret_cast<Toggle>(GetProcAddress(module,"SpiShouldSpawnThread"))()) {
        std::puts("FAIL: wrong metadata, game mask, SPI version or scheduling"); return 1;
    }
    wchar_t exe[MAX_PATH]{}; GetModuleFileNameW(nullptr, exe, MAX_PATH);
    auto logDir = std::filesystem::path(exe).parent_path() / L"ASI";
    std::filesystem::create_directories(logDir);
    auto attach = reinterpret_cast<Callback>(GetProcAddress(module,"SpiOnAttach"));
    if (!attach(nullptr)) { std::puts("FAIL: attach rejected callback contract"); return 1; }
    // The plugin keeps its log open. Explicit sharing permits reading a live
    // writer; MSVC's default ifstream share mode can reject that open.
    auto logPath = logDir / L"MEFSingleLineBridge.log";
    auto input = CreateFileW(logPath.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (input == INVALID_HANDLE_VALUE) { std::printf("FAIL: log open error %lu\n", GetLastError()); return 1; }
    std::string log(GetFileSize(input, nullptr), '\0');
    DWORD read = 0;
    bool readOk = ReadFile(input, log.data(), static_cast<DWORD>(log.size()), &read, nullptr) != 0;
    CloseHandle(input);
    if (!readOk || read != log.size()) { std::puts("FAIL: log read"); return 1; }
    if (log.find("SPI attach entered") == std::string::npos ||
        log.find("REFUSED: unsupported executable; no hooks installed.") == std::string::npos ||
        log.find("READY:") != std::string::npos) {
        std::puts("FAIL: startup logging or executable guard"); return 1;
    }
    if (!reinterpret_cast<Callback>(GetProcAddress(module,"SpiOnDetach"))(nullptr)) {
        std::puts("FAIL: detach callback"); return 1;
    }
    std::puts("PASS: all five loader exports, LE1/SPI metadata, postload/sequential mode, attach log and unsupported-host rejection. No game hook was installed.");
    return 0;
}
