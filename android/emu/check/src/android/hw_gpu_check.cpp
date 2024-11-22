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

#include "host-common/FeatureControl.h"  // for isEnabled
#include "host-common/opengl/emugl_config.h"

namespace android {
namespace emulation {

using android::base::System;
using android_studio::EmulatorCompatibilityInfo;

// A check to make sure there is a enough GPU capabilities available
// for the given avd.
AvdCompatibilityCheckResult hasSufficientHwGpu(AvdInfo* avd) {
    EmulatorCompatibilityInfo metrics;
    if (avd == nullptr) {
        metrics.set_check(
                EmulatorCompatibilityInfo::AVD_COMPATIBILITY_CHECK_NO_AVD);
        return {.description =
                        "No avd present, cannot check for system capabilities",
                .status = AvdCompatibility::Error,
                .metrics = metrics};
    }

    // Allow users and tests to skip compatibility checks
    if (System::get()->envGet("ANDROID_EMU_SKIP_GPU_CHECKS") == "1") {
        metrics.set_check(EmulatorCompatibilityInfo::
                                  AVD_COMPATIBILITY_CHECK_GPU_CHECK_SKIP);
        return {.description = "GPU compatibility checks are disabled",
                .status = AvdCompatibility::Warning,
                .metrics = metrics};
    }

    const char* name = avdInfo_getName(avd);

    // Check XR specific compatibility issues
    // TODO(b/373601997): Improve supported platforms and configurations
    const bool isXrAvd = (avdInfo_getAvdFlavor(avd) == AVD_DEV_2024);
    if (isXrAvd) {
        // Not supported on Mac Intel due to missing GPU features
#if defined(__APPLE__) && !defined(__arm64__)
        return {
                .description =
                        absl::StrFormat("`%s` is not supported to run on "
                                        "Mac with Intel processors",
                                        name),
                .status = AvdCompatibility::Error,
        };
#endif

        // Linux platform is not very well tested on XR scenarios, independently of the GPU
// TODO(b/373601997) Change this warning when we will have more tests
#ifdef __linux__
        return {
                .description =
                        absl::StrFormat("`%s` is not yet "
                                        "fully supported on Linux",
                                        name),
                .status = AvdCompatibility::Warning,
        };
#endif
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

    if (!requiresHwGpuCheck) {
        return {.description = absl::StrFormat(
                        "Hardware GPU requirements to run avd: `%s` are passed",
                        name),
                .status = AvdCompatibility::Ok,
                .metrics = metrics};
    }

    char* vkVendor = nullptr;
    int vkMajor = 0;
    int vkMinor = 0;
    int vkPatch = 0;
    uint64_t vkDeviceMemBytes = 0;

    emuglConfig_get_vulkan_hardware_gpu(&vkVendor, &vkMajor, &vkMinor, &vkPatch,
                                        &vkDeviceMemBytes);

    if (!vkVendor) {
        // Could not properly detect the hardware parameters, disable Vulkan
        metrics.set_details("VulkanFail");
        return {.description = absl::StrFormat(
                        "Could not detect GPU for Vulkan compatibility "
                        "checks. Please try updating your GPU Drivers"),
                .status = AvdCompatibility::Error,
                .metrics = metrics};
    }

    bool isAMD = (strncmp("AMD", vkVendor, 3) == 0);
    bool isIntel = (strncmp("Intel", vkVendor, 5) == 0);
    bool isUnsupportedGpuDriver = false;
    // Based on androidEmuglConfigInit
    if (isAMD) {
        if (vkMajor == 1 && vkMinor < 3) {
            // on windows, amd gpu with api 1.2.x does not work
            // for vulkan, disable it
            isUnsupportedGpuDriver = true;
        }
    } else if (isIntel) {
        bool apiLevelLow = vkMajor == 1 && ((vkMinor == 3 && vkPatch < 240) || (vkMinor < 3));
        if (apiLevelLow || isXrAvd) {
            // Intel gpu with api < 1.3.240 does not work
            // for vulkan, disable it
            isUnsupportedGpuDriver = true;
        }
    }

    const std::string vendorName = vkVendor;
    free(vkVendor);

    if (isUnsupportedGpuDriver) {
        metrics.set_check(
                EmulatorCompatibilityInfo::
                        AVD_COMPATIBILITY_CHECK_GPU_CHECK_UNSUPPORTED_VULKAN_VERSION);
        metrics.set_details(absl::StrFormat("GPU:%s, API: %d.%d.%d",
                                            vendorName.c_str(), vkMajor,
                                            vkMinor, vkPatch));
        return {.description = absl::StrFormat(
                        "GPU driver is not supported to run avd: `%s`. "
                        "Your '%s' GPU has Vulkan API version %d.%d.%d, "
                        "and is not supported for Vulkan",
                        name, vendorName.c_str(), vkMajor, vkMinor, vkPatch),
                .status = AvdCompatibility::Error,
                .metrics = metrics};
    }

    // Check available GPU memory
    const uint64_t deviceMemMiB = vkDeviceMemBytes / (1024 * 1024);
    const uint64_t avdMinGpuMemMiB = isXrAvd ? 2048 : 0;
    if (deviceMemMiB < avdMinGpuMemMiB) {
        metrics.set_check(
                EmulatorCompatibilityInfo::
                        AVD_COMPATIBILITY_CHECK_GPU_CHECK_INSUFFICIENT_MEMORY);
        metrics.set_details(std::to_string(deviceMemMiB));
        return {.description = absl::StrFormat(
                        "Not enough GPU memory available to run avd: `%s`. "
                        "Available: %llu MB, minimum required: %llu MB",
                        name, deviceMemMiB, avdMinGpuMemMiB),
                .status = AvdCompatibility::Error,
                .metrics = metrics};
    }
    const uint64_t avdSuggestedGpuMemMiB = isXrAvd ? 4096 : 0;
    if (deviceMemMiB < avdSuggestedGpuMemMiB) {
        return {
                .description = absl::StrFormat(
                        "GPU memory available (%llu MB) to run avd: `%s` is below "
                        "the suggested level (%llu MB)",
                        deviceMemMiB, name, avdMinGpuMemMiB),
                .status = AvdCompatibility::Warning,
        };
    }

    return {.description = absl::StrFormat(
                    "Hardware GPU requirements to run avd: `%s` are met", name),
            .status = AvdCompatibility::Ok,
            .metrics = metrics};
}

REGISTER_COMPATIBILITY_CHECK(hasSufficientHwGpu);

}  // namespace emulation
}  // namespace android
