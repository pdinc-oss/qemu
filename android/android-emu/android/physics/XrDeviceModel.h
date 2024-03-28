/*
 * Copyright (C) 2024 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

#include <mutex>
#include <vector>

#include "aemu/base/EventNotificationSupport.h"  // for EventNotifi...
#include "android/emulation/android_qemud.h"
#include "android/hw-sensors.h"
#include "android/physics/Physics.h"
#include "xr_emulator_conn.pb.h"

namespace android {
namespace physics {

class XrDeviceModel {
public:
    XrDeviceModel();

    // called by physical model to send a XR Input mode to the system.
    void setXrInputMode(float value, PhysicalInterpolation mode);
    // Required to support PhysicalModel.
    float getXrInputMode(ParameterValueType parameterValueType) const;
    // called by physical model to send a XR Environment mode to the system.
    void setXrEnvironmentMode(float value, PhysicalInterpolation mode);
    // Required to support PhysicalModel.
    float getXrEnvironmentMode(ParameterValueType parameterValueType) const;
    // called by physical model to send a XR Screen Recenter to the system.
    void setXrScreenRecenter(float value, PhysicalInterpolation mode);
    // Required to support PhysicalModel.
    float getXrScreenRecenter(ParameterValueType parameterValueType) const;
    // called by physical model to send a Viewport Control mode to the system.
    void setXrViewportControlMode(float value, PhysicalInterpolation mode);
    // Required to support PhysicalModel.
    float getXrViewportControlMode(ParameterValueType parameterValueType) const;

    QemudClient* initializeQemudClient(int channel, const char* client_param);
    void qemudClientRecv(uint8_t* msg, int msglen);
    void qemudClientClose();
    void qemudClientSend(const xr_emulator_proto::EmulatorRequest& request);

private:
    XrInputMode mLastInputModeRequested =
            XrInputMode::XR_INPUT_MODE_MOUSE_KEYBOARD;
    XrEnvironmentMode mLastEnvironmentModeRequested =
            XrEnvironmentMode::XR_ENVIRONMENT_MODE_PASSTHROUGH_OFF;
    XrViewportControlMode mLastViewportControlModeRequested =
            XrViewportControlMode::VIEWPORT_CONTROL_MODE_PAN;

    void sendXrInputMode(enum XrInputMode mode);
    void sendXrEnvironmentMode(enum XrEnvironmentMode mode);
    void sendXrScreenRecenter();
    void sendXrViewportControlMode(enum XrViewportControlMode mode);

    QemudService* qemud_service = nullptr;
    QemudClient* qemud_client = nullptr;
};

}  // namespace physics
}  // namespace android
