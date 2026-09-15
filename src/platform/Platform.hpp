#pragma once

#include <string>
#include <cstdint>
#include <chrono>

namespace optigd::platform {

enum class PlatformOS : uint8_t {
    Windows = 0,
    Linux = 1,
    Android = 2,
    MacOS = 3,
    iOS = 4,
    Unknown = 5
};

struct SystemInfo {
    PlatformOS os{PlatformOS::Unknown};
    std::string osName;
    uint32_t logicalCores{1};
    uint64_t totalRamMb{0};
    std::string compiler;
};

class Platform {
public:
    static PlatformOS getCurrentOS();
    static std::string_view getOSName();
    static SystemInfo getSystemInfo();

    static uint64_t getTimestampNs();
    static uint64_t getTimestampUs();

    // Memory usage of the current process in KB
    static size_t getCurrentProcessMemoryKb();
};

} // namespace optigd::platform
