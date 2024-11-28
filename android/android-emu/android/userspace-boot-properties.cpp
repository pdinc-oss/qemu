// Copyright 2021 The Android Open Source Project
//
// This software is licensed under the terms of the GNU General Public
// License version 2, as published by the Free Software Foundation, and
// may be copied, distributed, and modified under those terms.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.

#include "android/userspace-boot-properties.h"

#include <string.h>   // for strcmp, strchr
#include <algorithm>  // for replace_if
#include <map>        // for dedup user options
#include <ostream>    // for operator<<, ostream
#include <string>     // for string, operator+

#include "android/avd/info.h"
#include "aemu/base/Log.h"                      // for LOG, LogMessage
#include "aemu/base/StringFormat.h"             // for StringFormat
#include "android/base/system/System.h"        // for System
#include "aemu/base/misc/StringUtils.h"         // for splitTokens
#include "android/emulation/control/adb/adbkey.h"  // for getPrivateAdbKeyPath
#include "android/emulation/resizable_display_config.h"
#include "host-common/FeatureControl.h"  // for isEnabled
#include "host-common/Features.h"        // for AndroidbootProps2
#include "android/hw-sensors.h"                     // for android_foldable_...
#include "android/utils/debug.h"                    // for dwarning

namespace {

// Note: The ACPI _HID that follows devices/ must match the one defined in the
// ACPI tables (hw/i386/acpi_build.c)
static const char kSysfsAndroidDtDir[] =
        "/sys/bus/platform/devices/ANDR0001:00/properties/android/";
static const char kSysfsAndroidDtDirDtb[] =
        "/proc/device-tree/firmware/android/";

}  // namespace

using android::base::splitTokens;
using android::base::StringFormat;
using android::base::System;

std::string getDeviceStateString(const AndroidHwConfig* hw) {
    const bool not_pixel_fold = !android_foldable_is_pixel_fold();
    if (android_foldable_hinge_configured() && not_pixel_fold) {
        int numHinges = hw->hw_sensor_hinge_count;
        if (numHinges < 0 || numHinges > ANDROID_FOLDABLE_MAX_HINGES) {
            derror("Incorrect hinge count %d", hw->hw_sensor_hinge_count);
            return std::string();
        }
        std::string postureList(
                hw->hw_sensor_posture_list ? hw->hw_sensor_posture_list : "");
        std::string postureValues(
                hw->hw_sensor_hinge_angles_posture_definitions
                        ? hw->hw_sensor_hinge_angles_posture_definitions
                        : "");
        std::vector<std::string> postureListTokens, postureValuesTokens;
        splitTokens(postureList, &postureListTokens, ",");
        splitTokens(postureValues, &postureValuesTokens, ",");
        if (postureList.empty() || postureValues.empty() ||
            postureListTokens.size() != postureValuesTokens.size()) {
            derror("Incorrect posture list %s or posture mapping %s",
                   postureList.c_str(), postureValues.c_str());
            return std::string();
        }
        int foldAtPosture =
                hw->hw_sensor_hinge_fold_to_displayRegion_0_1_at_posture;
        std::string ret("<device-state-config>");
        std::vector<std::string> valuesToken;
        for (int i = 0; i < postureListTokens.size(); i++) {
            char name[16];
            ret += "<device-state>";
            if (foldAtPosture != 1 &&
                postureListTokens[i] == std::to_string(foldAtPosture)) {
                // "device/generic/goldfish/overlay/frameworks/base/core/res/res/values/config.xml"
                // specified "config_foldedDeviceStates" as "1" (CLOSED).
                // If foldablbe AVD configs "fold" at other deviceState, rewrite
                // it to "1"
                postureListTokens[i] = "1";
            }
            ret += "<identifier>" + postureListTokens[i] + "</identifier>";
            if (!android_foldable_posture_name(
                        std::stoi(postureListTokens[i], nullptr), name)) {
                return std::string();
            }
            ret += "<name>" + std::string(name) + "</name>";
            ret += "<conditions>";
            splitTokens(postureValuesTokens[i], &valuesToken, "&");
            if (valuesToken.size() != numHinges) {
                derror("Incorrect posture mapping %s",
                       postureValuesTokens[i].c_str());
                return std::string();
            }
            std::vector<std::string> values;
            struct AnglesToPosture valuesToPosture;
            for (int j = 0; j < valuesToken.size(); j++) {
                ret += "<sensor>";
                ret += "<type>android.sensor.hinge_angle</type>";
                ret += "<name>Goldfish hinge sensor" + std::to_string(j) +
                       " (in degrees)</name>";
                splitTokens(valuesToken[j], &values, "-");
                size_t tokenCount = values.size();
                if (tokenCount != 2 && tokenCount != 3) {
                    derror("Incorrect posture mapping %s", valuesToken[j]);
                    return std::string();
                }
                ret += "<value>";
                ret += "<min-inclusive>" + values[0] + "</min-inclusive>";
                ret += "<max-inclusive>" + values[1] + "</max-inclusive>";
                ret += "</value>";
                ret += "</sensor>";
            }
            ret += "</conditions>";
            ret += "</device-state>";
        }
        ret += "</device-state-config>";
        return ret;
    }

    if (android_foldable_rollable_configured()) {
        return std::string(
                "<device-state-config><device-state><identifier>1</identifier>"
                "<name>CLOSED</name><conditions><lid-switch><open>false</open>"
                "</lid-switch></conditions></device-state><device-state>"
                "<identifier>3</identifier><name>OPENED</name><conditions>"
                "<lid-switch><open>true</open></lid-switch></conditions>"
                "</device-state></device-state-config>");
    }

    return std::string();
}

std::vector<std::pair<std::string, std::string>> getUserspaceBootProperties(
        const AndroidOptions* opts,
        const char* targetArch,
        const char* serialno,
        const AndroidGlesEmulationMode glesMode,
        const int bootPropOpenglesVersion,
        const int apiLevel,
        const char* kernelSerialPrefix,
        const std::vector<std::string>* verifiedBootParameters,
        const AndroidHwConfig* hw) {
    const bool isX86ish =
            !strcmp(targetArch, "x86") || !strcmp(targetArch, "x86_64");
    const bool hasShellConsole = opts->logcat || opts->shell;
#ifdef __APPLE__
    constexpr const bool isMac = true;
#else
    constexpr const bool isMac = false;
#endif

    const char* androidbootVerityMode;
    const char* checkjniProp;
    const char* bootanimProp;
    const char* bootanimPropValue;
    const char* qemuGlesProp;
    const char* qemuScreenOffTimeoutProp;
    const char* qemuEncryptProp;
    const char* qemuMediaProfileVideoProp;
    const char* qemuVsyncProp;
    const char* qemuGltransportNameProp;
    const char* hwGltransportNameProp;
    const char* qemuDrawFlushIntervalProp;
    const char* qemuOpenglesVersionProp;
    const char* qemuUirendererProp;
    const char* dalvikVmHeapsizeProp;
    const char* qemuLegacyFakeCameraProp;
    const char* qemuCameraProtocolVerProp;
    const char* qemuCameraHqEdgeProp;
    const char* qemuDisplaySettingsXmlProp;
    const char* qemuVirtioWifiProp;
    const char* qemuWifiProp;
    const char* androidQemudProp;
    const char* qemuHwcodecAvcdecProp;
    const char* qemuHwcodecHevcdecProp;
    const char* qemuHwcodecVpxdecProp;
    const char* androidbootLogcatProp;
    const char* adbKeyProp;
    const char* avdNameProp;
    const char* deviceStateProp;
    const char* qemuCpuVulkanVersionProp;
    const char* emulatorCircularProp;
    const char* autoRotateProp;
    const char* qemuExternalDisplays;
    const char* qemuDualModeMouseDriverProp;
    const char* qemuDualModeMouseHideGuestCursorProp;

    namespace fc = android::featurecontrol;
    if (fc::isEnabled(fc::AndroidbootProps) ||
        fc::isEnabled(fc::AndroidbootProps2)) {
        androidbootVerityMode = "androidboot.veritymode";
        checkjniProp = "androidboot.dalvik.vm.checkjni";
        bootanimProp = "androidboot.debug.sf.nobootanimation";
        bootanimPropValue = "1";
        qemuGlesProp = nullptr;  // deprecated
        qemuScreenOffTimeoutProp =
                "androidboot.qemu.settings.system.screen_off_timeout";
        qemuEncryptProp = nullptr;            // deprecated
        qemuMediaProfileVideoProp = nullptr;  // deprecated
        qemuVsyncProp = "androidboot.qemu.vsync";
        qemuGltransportNameProp = "androidboot.qemu.gltransport.name";
        hwGltransportNameProp = "androidboot.hardware.gltransport";
        qemuDrawFlushIntervalProp =
                "androidboot.qemu.gltransport.drawFlushInterval";
        qemuOpenglesVersionProp = "androidboot.opengles.version";
        qemuUirendererProp = "androidboot.debug.hwui.renderer";
        dalvikVmHeapsizeProp = "androidboot.dalvik.vm.heapsize";
        qemuLegacyFakeCameraProp = "androidboot.qemu.legacy_fake_camera";
        qemuCameraProtocolVerProp = "androidboot.qemu.camera_protocol_ver";
        qemuCameraHqEdgeProp = "androidboot.qemu.camera_hq_edge_processing";
        qemuDisplaySettingsXmlProp = "androidboot.qemu.display.settings.xml";
        qemuVirtioWifiProp = "androidboot.qemu.virtiowifi";
        qemuWifiProp = "androidboot.qemu.wifi";
        androidQemudProp = nullptr;  // deprecated
        qemuHwcodecAvcdecProp = "androidboot.qemu.hwcodec.avcdec";
        qemuHwcodecHevcdecProp = "androidboot.qemu.hwcodec.hevcdec";
        qemuHwcodecVpxdecProp = "androidboot.qemu.hwcodec.vpxdec";
        androidbootLogcatProp = "androidboot.logcat";
        adbKeyProp = "androidboot.qemu.adb.pubkey";
        avdNameProp = "androidboot.qemu.avd_name";
        deviceStateProp = "androidboot.qemu.device_state";
        qemuCpuVulkanVersionProp = "androidboot.qemu.cpuvulkan.version";
        emulatorCircularProp = "androidboot.emulator.circular";
        autoRotateProp = "androidboot.qemu.autorotate";
        qemuExternalDisplays = "androidboot.qemu.external.displays";
        qemuDualModeMouseDriverProp = "androidboot.qemu.dual_mode_mouse_driver";
        qemuDualModeMouseHideGuestCursorProp = "androidboot.qemu.dual_mode_mouse_hide_guest_cursor";
    } else {
        androidbootVerityMode = nullptr;
        checkjniProp = "android.checkjni";
        bootanimProp = "android.bootanim";
        bootanimPropValue = "0";
        qemuGlesProp = "qemu.gles";
        qemuScreenOffTimeoutProp = "qemu.settings.system.screen_off_timeout";
        qemuEncryptProp = "qemu.encrypt";
        qemuMediaProfileVideoProp = "qemu.mediaprofile.video";
        qemuVsyncProp = "qemu.vsync";
        qemuGltransportNameProp = "qemu.gltransport";
        hwGltransportNameProp = nullptr;
        qemuDrawFlushIntervalProp = "qemu.gltransport.drawFlushInterval";
        qemuOpenglesVersionProp = "qemu.opengles.version";
        qemuUirendererProp = "qemu.uirenderer";
        dalvikVmHeapsizeProp = "qemu.dalvik.vm.heapsize";
        qemuLegacyFakeCameraProp = "qemu.legacy_fake_camera";
        qemuCameraProtocolVerProp = "qemu.camera_protocol_ver";
        qemuCameraHqEdgeProp = "qemu.camera_hq_edge_processing";
        qemuDisplaySettingsXmlProp = "qemu.display.settings.xml";
        qemuVirtioWifiProp = "qemu.virtiowifi";
        qemuWifiProp = "qemu.wifi";
        androidQemudProp = "android.qemud";
        qemuHwcodecAvcdecProp = "qemu.hwcodec.avcdec";
        qemuHwcodecHevcdecProp = "qemu.hwcodec.hevcdec";
        qemuHwcodecVpxdecProp = "qemu.hwcodec.vpxdec";
        androidbootLogcatProp = nullptr;
        adbKeyProp = nullptr;
        avdNameProp = "qemu.avd_name";
        deviceStateProp = "qemu.device_state";
        qemuCpuVulkanVersionProp = nullptr;
        emulatorCircularProp = "ro.emulator.circular";
        autoRotateProp = "qemu.autorotate";
        qemuExternalDisplays = "qemu.external.displays";
        qemuDualModeMouseDriverProp = "qemu.dual_mode_mouse_driver";
        qemuDualModeMouseHideGuestCursorProp = "qemu.dual_mode_mouse_hide_guest_cursor";
    }

    std::vector<std::pair<std::string, std::string>> params;

    // We always force qemu=1 when running inside QEMU.
    if (fc::isEnabled(fc::AndroidbootProps2)) {
        params.push_back({"androidboot.qemu", "1"});
    } else {
        params.push_back({"qemu", "1"});
    }

    params.push_back({"androidboot.hardware", "ranchu"});
    const char* qemuUirendererPropValue = nullptr;

    if (opts->guest_angle) {
        dwarning(
                "Command line option -guest-angle is deprecated and will be "
                "removed, use '-feature GuestAngle' instead.");
        fc::setEnabledOverride(fc::GuestAngle, true);
    }
    if (fc::isEnabled(fc::GuestAngle)) {
        params.push_back({"androidboot.hardwareegl", "angle"});

        if (!fc::isEnabled(fc::Vulkan)) {
            // Cannot use GuestAngle without Vulkan enabled
            // This might happen because of unsupported API level or GPU
            dfatal("Vulkan is not supported: GuestAngle feature won't work!");
        }
        // There's an emulator-specific hack in API 35 to disable specific GL
        // extensions. You can provide your own colon-delimited list or set to 0
        // to not disable any extensions, as we disable a large set of GL
        // extensions by default. See below.
        std::string aemu_angle_overrides_disabled =
                System::get()->envGet("AEMU_ANGLE_OVERRIDES_DISABLED");
        // The official angle feature set. See angle source code for more info.
        std::string angle_overrides_enabled =
                System::get()->envGet("ANGLE_FEATURE_OVERRIDES_ENABLED");
        std::string angle_overrides_disabled =
                System::get()->envGet("ANGLE_FEATURE_OVERRIDES_DISABLED");

        // GuestAngle boot parameters are only valid for some system images with
        // API level 34 and above.
        if (apiLevel >= 34) {
            if (angle_overrides_disabled.empty()) {
                // b/264575911: Nvidia seems to have issues with YUV samplers
                // with 'lowp' and 'mediump' precision qualifiers.
                // This should ideally use graphicsdetecto rresults at
                // GraphicsDetectorVkPrecisionQualifiersOnYuvSamplers.cpp
                const bool hwGpuRequested =
                        (emuglConfig_get_current_renderer() ==
                         SELECTED_RENDERER_HOST);
                if (!isMac && hwGpuRequested) {
                    char* vkVendor = nullptr;
                    int vkMajor, vkMinor, vkPatch;
                    emuglConfig_get_vulkan_hardware_gpu(
                            &vkVendor, &vkMajor, &vkMinor, &vkPatch, nullptr);
                    bool isNVIDIA =
                            (vkVendor && strncmp("NVIDIA", vkVendor, 6) == 0);
                    if (isNVIDIA) {
                        angle_overrides_disabled = "enablePrecisionQualifiers";

                        // TODO(b/378737781): Usage of external fence/semaphore
                        // fd objects causes device lost crashes and hangs.
                        angle_overrides_disabled +=
                                ":supportsExternalFenceFd"
                                ":supportsExternalSemaphoreFd";

                        // NVIDIA cards can satisfy 2-graphics-queue requirement
                        // for SkiaVK, and it works better with GuestAngle.
                        qemuUirendererPropValue = "skiavk";
                        params.push_back(
                                {"androidboot.debug.renderengine.backend",
                                 "skiavk"});
                    }
                }

                // Without turning off exposeNonConformantExtensionsAndVersions,
                // ANGLE will bypass the supported extensions check when guest
                // creates a GL context, which means a ES 3.2 context can be
                // created even without the above extensions.
                // TODO(b/238024366): this may not fit into character
                // limitations
                const char* extensionLimitStr =
                        "exposeNonConformantExtensionsAndVersions";
                const int MAX_PARAM_LENGTH = 92;
                const bool safeToAdd =
                        (angle_overrides_disabled.size() +
                         strlen(extensionLimitStr)) < MAX_PARAM_LENGTH;
                if (safeToAdd) {
                    if (!angle_overrides_disabled.size()) {
                        angle_overrides_disabled += ":";
                    }
                    angle_overrides_disabled += extensionLimitStr;
                } else {
                    WARN("Cannot add angle boot parameter '%s', character "
                         "limit exceeded.",
                         extensionLimitStr);
                }
            }
        }

        if (apiLevel == 35) {
            // TODO(b/376893591): The feature set below is only tested on
            // API 35. Adjust accordingly for other APIs.
            if (aemu_angle_overrides_disabled.empty()) {
                // Turning these off effectively disables support for GLES 3.2.
                aemu_angle_overrides_disabled =
                        "textureCompressionAstcLdrKHR"
                        ":sampleShadingOES"
                        ":sampleVariablesOES"
                        ":shaderMultisampleInterpolationOES"
                        ":copyImageEXT"
                        ":drawBuffersIndexedEXT"
                        ":geometryShaderEXT"
                        ":gpuShader5EXT"
                        ":primitiveBoundingBoxEXT"
                        ":shaderIoBlocksEXT"
                        ":textureBorderClampEXT"
                        ":textureBufferEXT"
                        ":textureCubeMapArrayEXT"
                        // Other extensions
                        ":drawElementsBaseVertexOES"
                        ":colorBufferFloatEXT"
                        ":robustnessKHR"
                        // Turn off tessellation shader (Required in ES 3.2)
                        ":tessellationShaderEXT"
                        ":tessellationShaderOES"
                        // Turn off geometry shader (Required in ES 3.2)
                        ":geometryShaderEXT"
                        ":geometryShaderOES";
            }
        }

        // Set the boot parameters for GuestAngle mode
        if (aemu_angle_overrides_disabled != "0") {
            params.push_back(
                    {"androidboot.hardware.aemu_feature_overrides_disabled",
                     aemu_angle_overrides_disabled});
        }
        if (angle_overrides_disabled != "0") {
            params.push_back(
                    {"androidboot.hardware.angle_feature_overrides_disabled",
                     angle_overrides_disabled});
        }
        if (angle_overrides_enabled != "0") {
            params.push_back(
                    {"androidboot.hardware.angle_feature_overrides_enabled",
                     angle_overrides_enabled});
        }
    }

    if (fc::isEnabled(fc::Vulkan)) {
        params.push_back({"androidboot.hardware.vulkan", "ranchu"});
    }

    if (serialno) {
        params.push_back({"androidboot.serialno", serialno});
    }

    if (opts->dalvik_vm_checkjni) {
        params.push_back({checkjniProp, "1"});
    }
    if (opts->no_boot_anim) {
        params.push_back({bootanimProp, bootanimPropValue});
    }

    // qemu.gles is used to pass the GPU emulation mode to the guest
    // through kernel parameters. Note that the ro.opengles.version
    // boot property must also be defined for |gles > 0|, but this
    // is not handled here (see vl-android.c for QEMU1).
    if (qemuGlesProp) {
        int gles;
        switch (glesMode) {
            case kAndroidGlesEmulationHost:
                gles = 1;
                break;
            case kAndroidGlesEmulationGuest:
                gles = 2;
                break;
            default:
                gles = 0;
        }
        params.push_back({qemuGlesProp, StringFormat("%d", gles)});
    }

    if (qemuCpuVulkanVersionProp && glesMode == kAndroidGlesEmulationHost) {
        // Put our swiftshader version string there, which is currently
        // Vulkan 1.1 (0x402000)
        params.push_back(
                {qemuCpuVulkanVersionProp, StringFormat("%d", 0x402000)});
    }

    const char* pTimeout = avdInfo_screen_off_timeout(apiLevel);
    params.push_back({qemuScreenOffTimeoutProp, pTimeout});

    if (opts->xts && fc::isEnabled(fc::AndroidVirtualizationFramework)) {
        params.push_back({"androidboot.hypervisor.version", "gfapi-35"});
        params.push_back({"androidboot.hypervisor.vm.supported", "1"});
        params.push_back({"androidboot.hypervisor.protected_vm.supported", "0"});
    }

    if (apiLevel >= 31 && androidbootVerityMode) {
        params.push_back({androidbootVerityMode, "enforcing"});
    }

    if (fc::isEnabled(fc::EncryptUserData) && qemuEncryptProp) {
        params.push_back({qemuEncryptProp, "1"});
    }

    // Android media profile selection
    // 1. If the SelectMediaProfileConfig is on, then select
    // <media_profile_name> if the resolution is above 1080p (1920x1080).
    if (fc::isEnabled(fc::DynamicMediaProfile) &&
        qemuMediaProfileVideoProp) {
        if ((hw->hw_lcd_width > 1920 && hw->hw_lcd_height > 1080) ||
            (hw->hw_lcd_width > 1080 && hw->hw_lcd_height > 1920)) {
            dwarning(
                    "Display resolution > 1080p. Using different media "
                    "profile.");
            params.push_back(
                    {qemuMediaProfileVideoProp,
                     "/data/vendor/etc/media_codecs_google_video_v2.xml"});
        }
    }

    // Set vsync rate
    if (opts->vsync_rate) {
      std::string param = opts->vsync_rate;
      params.push_back({qemuVsyncProp, param});
    } else {
      params.push_back({qemuVsyncProp, StringFormat("%u", hw->hw_lcd_vsync)});
    }


    // Set gl transport props
    params.push_back({qemuGltransportNameProp, hw->hw_gltransport});
    if (hwGltransportNameProp) {
        params.push_back({hwGltransportNameProp, hw->hw_gltransport});
    }
    params.push_back(
            {qemuDrawFlushIntervalProp,
             StringFormat("%u", hw->hw_gltransport_drawFlushInterval)});

    // OpenGL ES related setup
    // 1. Set opengles.version and set Skia as UI renderer if
    // GLESDynamicVersion = on (i.e., is a reasonably good driver)
    params.push_back({qemuOpenglesVersionProp,
                      StringFormat("%d", bootPropOpenglesVersion)});

    if (fc::isEnabled(fc::GLESDynamicVersion) && !qemuUirendererPropValue) {
        qemuUirendererPropValue = "skiagl";
    }

    if (qemuUirendererPropValue) {
        params.push_back({qemuUirendererProp, qemuUirendererPropValue});
    }

    if (androidbootLogcatProp) {
        if (opts->logcat) {
            std::string param = opts->logcat;

            // Replace any space with a comma.
            std::replace_if(
                    param.begin(), param.end(),
                    [](char c) {
                        switch (c) {
                            case ' ':
                            case '\t':
                                return true;

                            default:
                                return false;
                        }
                    },
                    ',');

            params.push_back({androidbootLogcatProp, param});
        } else {
            params.push_back({androidbootLogcatProp, "*:V"});
        }
    }

    // Send adb public key to device
    if (adbKeyProp) {
        auto privkey = getPrivateAdbKeyPath();
        std::string key = "";

        if (!privkey.empty() && pubkey_from_privkey(privkey, &key)) {
            params.push_back({adbKeyProp, key});
            dinfo("Sending adb public key [%s]", key);
        } else {
            dwarning("No adb private key exists");
        }
    }

    if (opts->bootchart) {
        params.push_back({"androidboot.bootchart", opts->bootchart});
    }

    if (opts->selinux) {
        params.push_back({"androidboot.selinux", opts->selinux});
    }

    if (hw->vm_heapSize > 0) {
        params.push_back(
                {dalvikVmHeapsizeProp, StringFormat("%dm", hw->vm_heapSize)});
    }

    if (opts->legacy_fake_camera) {
        params.push_back({qemuLegacyFakeCameraProp, "1"});
    }

    if (apiLevel > 29) {
        params.push_back({qemuCameraProtocolVerProp, "1"});
    }

    if (!opts->camera_hq_edge) {
        params.push_back({qemuCameraHqEdgeProp, "0"});
    }

    const bool isDynamicPartition = fc::isEnabled(fc::DynamicPartition);
    if (isX86ish && !isDynamicPartition) {
        // x86 and x86_64 platforms use an alternative Android DT directory that
        // mimics the layout of /proc/device-tree/firmware/android/
        params.push_back({"androidboot.android_dt_dir",
                          (fc::isEnabled(fc::KernelDeviceTreeBlobSupport)
                                   ? kSysfsAndroidDtDirDtb
                                   : kSysfsAndroidDtDir)});
    }

    if (verifiedBootParameters) {
        for (const std::string& param : *verifiedBootParameters) {
            const size_t i = param.find('=');
            if (i == std::string::npos) {
                params.push_back({param, ""});
            } else {
                params.push_back({param.substr(0, i), param.substr(i + 1)});
            }
        }
    }

    // display settings file name
    if (hw->display_settings_xml && hw->display_settings_xml[0] &&
        qemuDisplaySettingsXmlProp) {
        params.push_back(
                {qemuDisplaySettingsXmlProp, hw->display_settings_xml});
    }

    if (resizableEnabled()) {
        params.push_back({qemuDisplaySettingsXmlProp, "resizable"});
    }

    if (android_foldable_hinge_configured()) {
        params.push_back({autoRotateProp, "1"});
    }

    if (fc::isEnabled(fc::VirtioWifi)) {
        params.push_back({qemuVirtioWifiProp, "1"});
    } else if (fc::isEnabled(fc::Wifi)) {
        params.push_back({qemuWifiProp, "1"});
    }

    if (fc::isEnabled(fc::HardwareDecoder)) {
        params.push_back({qemuHwcodecAvcdecProp, "2"});
        params.push_back({qemuHwcodecHevcdecProp, "2"});
        params.push_back({qemuHwcodecVpxdecProp, "2"});
    }

    if (fc::isEnabled(fc::SupportPixelFold)) {
        if (android_foldable_hinge_configured() &&
            android_foldable_is_pixel_fold()) {
            int width{0}, height{0};
            width = hw->hw_displayRegion_0_1_width;
            height = hw->hw_displayRegion_0_1_height;
            dinfo("Configuring second built-in display with width %d and "
                  "height %d for pixel_fold device",
                  width, height);
            std::string display_list = StringFormat("1,%d,%d,%d,0", width,
                                                    height, hw->hw_lcd_density);
            params.push_back({qemuExternalDisplays, display_list});
        }
    }

    if (hasShellConsole) {
        params.push_back({"androidboot.console",
                          StringFormat("%s0", kernelSerialPrefix)});
    }

    if (androidQemudProp) {
        params.push_back({androidQemudProp, "1"});
    }

    params.push_back({avdNameProp, hw->avd_name});

    if (deviceStateProp &&
        android::featurecontrol::isEnabled(
                android::featurecontrol::DeviceStateOnBoot)) {
        std::string deviceState = getDeviceStateString(hw);
        if (deviceState != "") {
            dinfo(" sending device_state_config:%s", deviceState);
            params.push_back({deviceStateProp, deviceState});
        }
    }

    for (auto i = opts->append_userspace_opt; i; i = i->next) {
        const char* const val = i->param;
        const char* const eq = strchr(val, '=');
        if (eq) {
            params.push_back({std::string(val, eq), eq + 1});
        } else {
            params.push_back({val, ""});
        }
    }

    if (hw->hw_lcd_circular) {
      params.push_back({emulatorCircularProp, "1"});
    }

    if (fc::isEnabled(fc::VirtioDualModeMouse)) {
        params.push_back({qemuDualModeMouseDriverProp, "1"});
        if (fc::isEnabled(fc::DualModeMouseDisplayHostCursor)) {
            params.push_back({qemuDualModeMouseHideGuestCursorProp, "1"});
        }
    }

    std::map<std::string, std::string> key_to_val_map;
    for (int i = 0; i < params.size(); ++i) {
        const std::string& key = params[i].first;
        const std::string& val = params[i].second;
        if (key_to_val_map.find(key) != key_to_val_map.end()) {
            dwarning(
                    "found new value '%s' for option '%s', override previous "
                    "value '%s'",
                    val.c_str(), key.c_str(), key_to_val_map[key].c_str());
        }
        key_to_val_map[key] = params[i].second;
    }

    INFO("Userspace boot properties:");
    std::vector<std::pair<std::string, std::string>> unique_params;
    for (const auto& it : key_to_val_map) {
        unique_params.push_back({it.first, it.second});
        INFO("  %s=%s", it.first.c_str(), it.second.c_str());
    }

    return unique_params;
}
