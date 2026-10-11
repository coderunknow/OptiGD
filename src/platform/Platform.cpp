#include "Platform.hpp"
#include <algorithm>
#include <thread>
#include <fstream>
#include <sstream>

#if defined(OPTIGD_OS_LINUX) || defined(OPTIGD_OS_ANDROID) || defined(OPTIGD_OS_APPLE)
    #include <time.h>
    #if defined(_POSIX_TIMERS) && _POSIX_TIMERS > 0
        #define OPTIGD_HAVE_CLOCK_GETTIME 1
    #elif defined(__APPLE__)
        #include <mach/mach_time.h>
        #include <mach/thread_act.h>
        #define OPTIGD_HAVE_MACH_THREAD_TIME 1
    #endif
#endif

#if defined(_WIN32) || defined(_WIN64)
    #define OPTIGD_OS_WINDOWS 1
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #include <windows.h>
    #include <psapi.h>
    #ifdef _MSC_VER
        #pragma comment(lib, "psapi.lib")
    #endif
#elif defined(__ANDROID__)
    #define OPTIGD_OS_ANDROID 1
    #include <unistd.h>
#elif defined(__linux__)
    #define OPTIGD_OS_LINUX 1
    #include <unistd.h>
#elif defined(__APPLE__)
    #define OPTIGD_OS_APPLE 1
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

// ---------------------------------------------------------------------------
// Architecture
// ---------------------------------------------------------------------------

PlatformArch Platform::getArchitecture() {
#if defined(__x86_64__) || defined(_M_X64)
    return PlatformArch::X86_64;
#elif defined(__i386__) || defined(_M_IX86)
    return PlatformArch::X86;
#elif defined(__aarch64__) || defined(_M_ARM64)
    return PlatformArch::Arm64;
#elif defined(__arm__) || defined(_M_ARM)
    return PlatformArch::Arm32;
#else
    return PlatformArch::Unknown;
#endif
}

std::string_view Platform::getArchitectureName() {
    switch (getArchitecture()) {
        case PlatformArch::X86_64: return "x86_64";
        case PlatformArch::X86:    return "x86";
        case PlatformArch::Arm64:  return "arm64";
        case PlatformArch::Arm32:  return "arm32";
        default:                   return "unknown";
    }
}

// ---------------------------------------------------------------------------
// Thread CPU time
//
// `OptiGDManager` measures the hooked update() call with the wall clock, which
// is correct for "how long the game spent in update" but cannot distinguish a
// slow update from a preempted one. This is the only way to tell them apart, and
// it returns 0 rather than a guess when the platform cannot supply it.
// ---------------------------------------------------------------------------

bool Platform::supportsThreadCpuTime() {
#if defined(OPTIGD_OS_WINDOWS) || defined(OPTIGD_HAVE_CLOCK_GETTIME) || defined(OPTIGD_HAVE_MACH_THREAD_TIME)
    return true;
#else
    return false;
#endif
}

uint64_t Platform::getCurrentThreadCpuTimeUs() {
#if defined(OPTIGD_OS_WINDOWS)
    FILETIME creation{}, exitTime{}, kernel{}, user{};
    if (!GetThreadTimes(GetCurrentThread(), &creation, &exitTime, &kernel, &user)) {
        return 0;
    }
    auto toUs = [](const FILETIME& ft) -> uint64_t {
        ULARGE_INTEGER value{};
        value.LowPart = ft.dwLowDateTime;
        value.HighPart = ft.dwHighDateTime;
        return value.QuadPart / 10ULL; // 100 ns units -> microseconds
    };
    return toUs(kernel) + toUs(user);
#elif defined(OPTIGD_HAVE_CLOCK_GETTIME)
    struct timespec ts{};
    if (clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts) != 0) return 0;
    return static_cast<uint64_t>(ts.tv_sec) * 1000000ULL +
           static_cast<uint64_t>(ts.tv_nsec) / 1000ULL;
#elif defined(OPTIGD_HAVE_MACH_THREAD_TIME)
    thread_basic_info_data_t info{};
    mach_msg_type_number_t count = THREAD_BASIC_INFO_COUNT;
    kern_return_t kr = thread_info(mach_thread_self(), THREAD_BASIC_INFO,
                                   reinterpret_cast<thread_info_t>(&info), &count);
    if (kr != KERN_SUCCESS) return 0;
    return static_cast<uint64_t>(info.user_time.seconds) * 1000000ULL +
           static_cast<uint64_t>(info.user_time.microseconds) +
           static_cast<uint64_t>(info.system_time.seconds) * 1000000ULL +
           static_cast<uint64_t>(info.system_time.microseconds);
#else
    return 0;
#endif
}

SystemInfo Platform::getSystemInfo() {
    SystemInfo info;
    info.os = getCurrentOS();
    info.osName = std::string(getOSName());
    info.architecture = std::string(getArchitectureName());
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
