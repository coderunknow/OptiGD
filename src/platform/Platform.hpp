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
    std::string architecture;
    uint32_t logicalCores{1};
    uint64_t totalRamMb{0};
    std::string compiler;
};

enum class PlatformArch : uint8_t {
    X86 = 0,
    X86_64,
    Arm32,
    Arm64,
    Unknown,
};

class Platform {
public:
    static PlatformOS getCurrentOS();
    static std::string_view getOSName();
    // Compile-time instruction set. Recorded in every report and session so two
    // captures taken on different architectures are never compared as if they
    // described the same machine.
    static PlatformArch getArchitecture();
    static std::string_view getArchitectureName();
    static SystemInfo getSystemInfo();

    // Thread CPU time for the calling thread in microseconds, or 0 when the
    // platform cannot supply it. Used to separate "the update took this much
    // CPU" from "the update was off-core for this long".
    static uint64_t getCurrentThreadCpuTimeUs();
    static bool supportsThreadCpuTime();

    static uint64_t getTimestampNs();
    static uint64_t getTimestampUs();

    // Memory usage of the current process in KB
    static size_t getCurrentProcessMemoryKb();
};

} // namespace optigd::platform
