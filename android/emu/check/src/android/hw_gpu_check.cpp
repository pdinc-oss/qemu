// Copyright 2024 The Android Open Source Project
//
// This software is licensed under the terms of the GNU General Public
// License version 2, as published by the Free Software Foundation, and
// may be copied, distributed, and modified under those terms.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
#include "absl/strings/str_format.h"
#include "android/avd/info.h"
#include "android/base/system/System.h"
#include "android/emulation/compatibility_check.h"

#include <vulkan/vulkan_core.h>
#include "host-common/FeatureControl.h"  // for isEnabled
#include "host-common/opengl/emugl_config.h"

namespace android {
namespace emulation {

using android::base::System;

// A check to make sure there is a enough GPU capabilities available
// for the given avd.
AvdCompatibilityCheckResult hasSufficientHwGpu(AvdInfo* avd) {
    if (avd == nullptr) {
        return {
                .description =
                        "No avd present, cannot check for GPU capabilities",
                .status = AvdCompatibility::Warning,
        };
    }

    // Allow users and tests to skip compatibility checks
    if (System::get()->envGet("ANDROID_EMU_SKIP_GPU_CHECKS") == "1") {
        return {
                .description = "GPU compatibility checks are disabled",
                .status = AvdCompatibility::Warning,
        };
    }

#ifdef _WIN32
    constexpr const bool isWindows = true;
#else
    constexpr const bool isWindows = false;
#endif

    // Only apply these checks on Windows when GuestAngle enabled
    namespace fc = android::featurecontrol;
    bool requiresHwGpuCheck = true;
    if (!isWindows || !fc::isEnabled(fc::GuestAngle)) {
        requiresHwGpuCheck = false;
    }

    const char* name = avdInfo_getName(avd);
    if (!requiresHwGpuCheck) {
        return {
                .description = absl::StrFormat(
                        "Hardware GPU requirements to run avd: `%s` are passed",
                        name),
                .status = AvdCompatibility::Ok,
        };
    }

    char* vkVendor = nullptr;
    int vkMajor = 0;
    int vkMinor = 0;
    int vkPatch = 0;
    uint64_t vkDeviceMemBytes = 0;
    uint32_t vkDriverVersion = 0;

    emuglConfig_get_vulkan_hardware_gpu(&vkVendor, &vkMajor, &vkMinor, &vkPatch,
                                        &vkDeviceMemBytes, &vkDriverVersion);
    if (!vkVendor) {
        // Could not properly detect the hardware parameters, disable Vulkan
        return {
                .description = absl::StrFormat(
                        "Could not detect GPU for Vulkan compatibility "
                        "checks. Please try updating your GPU Drivers"),
                .status = AvdCompatibility::Error,
        };
    }

    // TODO(b/381540970): Use servers side flags and deny listings for filtering
    // GPU compatibility
    bool isAMD = (strncmp("AMD", vkVendor, 3) == 0);
    bool isIntel = (strncmp("Intel", vkVendor, 5) == 0);
    bool isNvidia = (strncmp("NVIDIA", vkVendor, 6) == 0);
    bool isUnsupportedGpuDriver = false;
    std::string driverVersionStr;
    if (isNvidia) {
        // Decode Nvidia driver version to make it meaningful to the users
        // Reference: VulkanDeviceInfo::getDriverVersion() at 
        // https://github.com/SaschaWillems/VulkanCapsViewer/blob/master/vulkanDeviceInfo.cpp
        // 10 bits = major version (up to r1023)
        // 8 bits = minor version (up to 255)
        // 8 bits = secondary branch version/build version (up to 255)
        // 6 bits = tertiary branch/build version (up to 63)
        const uint32_t major = (vkDriverVersion >> 22) & 0x3ff;
        const uint32_t minor = (vkDriverVersion >> 14) & 0x0ff;

        driverVersionStr = std::to_string(major) + "." + std::to_string(minor);

        // Disallow driver versions below 553.35 as they may cause BSODs
        // (ref:b/379178011).
        if (major < 553 || (major == 553 && minor < 35)) {
            isUnsupportedGpuDriver = true;
        }
    } else {
        // Use regular VK_API_VERSION encoding to print the version.
        driverVersionStr =
                std::to_string(VK_API_VERSION_MAJOR(vkDriverVersion)) + "." +
                std::to_string(VK_API_VERSION_MINOR(vkDriverVersion)) + "." +
                std::to_string(VK_API_VERSION_PATCH(vkDriverVersion));

#if defined(_WIN32)
        // Based on androidEmuglConfigInit
        if (isAMD) {
            if (vkMajor == 1 && vkMinor < 3) {
                // on windows, amd gpu with api 1.2.x does not work
                // for vulkan, disable it
                isUnsupportedGpuDriver = true;
            }
        } else if (isIntel) {
            if (vkMajor == 1 &&
                ((vkMinor == 3 && vkPatch < 240) || (vkMinor < 3))) {
                // intel gpu with api < 1.3.240 does not work
                // for vulkan, disable it
                isUnsupportedGpuDriver = true;
            }
        }
#endif
    }

    const std::string vendorName = vkVendor;
    free(vkVendor);

    if (isUnsupportedGpuDriver) {
        return {
                .description = absl::StrFormat(
                        "GPU driver is not supported to run avd: `%s`. "
                        "Your '%s' GPU has Vulkan API version `%d.%d.%d`, "
                        "driver version `%s` and is not supported for Vulkan",
                        name, vendorName.c_str(), vkMajor, vkMinor, vkPatch,
                        driverVersionStr.c_str()),
                .status = AvdCompatibility::Error,
        };
    }

    // Check available GPU memory
    const uint64_t deviceMemMiB = vkDeviceMemBytes / (1024 * 1024);
    const uint64_t avdMinGpuMemMiB = 0;  // TODO: set from the AVD
    if (deviceMemMiB < avdMinGpuMemMiB) {
        return {
                .description = absl::StrFormat(
                        "Not enough GPU memory available to run avd: `%s`. "
                        "Available: %llu, minimum required: %llu MB",
                        name, deviceMemMiB, avdMinGpuMemMiB),
                .status = AvdCompatibility::Error,
        };
    }

    return {
            .description = absl::StrFormat(
                    "Hardware GPU requirements to run avd: `%s` are met",
                    name),
            .status = AvdCompatibility::Ok,
    };
}

REGISTER_COMPATIBILITY_CHECK(hasSufficientHwGpu);

}  // namespace emulation
}  // namespace android
