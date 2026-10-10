#include "Platform.hpp"
#include <algorithm>
#include <cstddef>
#include <fstream>
#include <string>
#include <thread>

#if defined(_WIN32) || defined(_WIN64)
    #define OPTIGD_OS_WINDOWS 1
    #ifndef NOMINMAX
    #define NOMINMAX 1
    #endif
    #include <windows.h>
    #include <psapi.h>
    #pragma comment(lib, "psapi.lib")
#elif defined(__ANDROID__)
    #define OPTIGD_OS_ANDROID 1
    #include <unistd.h>
#elif defined(__linux__)
    #define OPTIGD_OS_LINUX 1
    #include <unistd.h>
#elif defined(__APPLE__)
    #define OPTIGD_OS_APPLE 1
    #include <TargetConditionals.h>
    #include <unistd.h>
    #include <mach/mach.h>
#else
    #define OPTIGD_OS_UNKNOWN 1
#endif

namespace optigd::platform {

PlatformOS Platform::getCurrentOS() {
#if defined(OPTIGD_OS_WINDOWS)
    return PlatformOS::Windows;
#elif defined(OPTIGD_OS_ANDROID)
    return PlatformOS::Android;
#elif defined(OPTIGD_OS_LINUX)
    return PlatformOS::Linux;
#elif defined(OPTIGD_OS_APPLE)
    #if TARGET_OS_IPHONE
        return PlatformOS::iOS;
    #else
        return PlatformOS::MacOS;
    #endif
#else
    return PlatformOS::Unknown;
#endif
}

std::string_view Platform::getOSName() {
#if defined(OPTIGD_OS_WINDOWS)
    return "Windows";
#elif defined(OPTIGD_OS_ANDROID)
    return "Android";
#elif defined(OPTIGD_OS_LINUX)
    return "Linux";
#elif defined(OPTIGD_OS_APPLE)
    return "Apple (macOS/iOS)";
#else
    return "Unknown OS";
#endif
}

SystemInfo Platform::getSystemInfo() {
    SystemInfo info;
    info.os = getCurrentOS();
    info.osName = std::string(getOSName());
    info.logicalCores = std::max(1u, std::thread::hardware_concurrency());

#if defined(__clang__)
    info.compiler = "Clang " __clang_version__;
#elif defined(__GNUC__)
    info.compiler = "GCC " + std::to_string(__GNUC__) + "." +
                    std::to_string(__GNUC_MINOR__) + "." +
                    std::to_string(__GNUC_PATCHLEVEL__);
#elif defined(_MSC_VER)
    info.compiler = "MSVC " + std::to_string(_MSC_VER);
#else
    info.compiler = "Unknown Compiler";
#endif

#if defined(OPTIGD_OS_LINUX) || defined(OPTIGD_OS_ANDROID)
    long pages = sysconf(_SC_PHYS_PAGES);
    long pageSize = sysconf(_SC_PAGE_SIZE);
    if (pages > 0 && pageSize > 0) {
        info.totalRamMb = (static_cast<uint64_t>(pages) * static_cast<uint64_t>(pageSize)) / (1024 * 1024);
    }
#elif defined(OPTIGD_OS_WINDOWS)
    MEMORYSTATUSEX memStatus;
    memStatus.dwLength = sizeof(memStatus);
    if (GlobalMemoryStatusEx(&memStatus)) {
        info.totalRamMb = memStatus.ullTotalPhys / (1024 * 1024);
    }
#else
    info.totalRamMb = 0;
#endif

    return info;
}

uint64_t Platform::getTimestampNs() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

uint64_t Platform::getTimestampUs() {
    return std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

size_t Platform::getCurrentProcessMemoryKb() {
#if defined(OPTIGD_OS_LINUX) || defined(OPTIGD_OS_ANDROID)
    std::ifstream statm("/proc/self/statm");
    if (statm.is_open()) {
        size_t size = 0, resident = 0;
        if (statm >> size >> resident) {
            long pageSizeKb = sysconf(_SC_PAGE_SIZE) / 1024;
            return resident * pageSizeKb;
        }
    }
#elif defined(OPTIGD_OS_WINDOWS)
    PROCESS_MEMORY_COUNTERS pmc;
    if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc))) {
        return pmc.WorkingSetSize / 1024;
    }
#endif
    return 0;
}

} // namespace optigd::platform
