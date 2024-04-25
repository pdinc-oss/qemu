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

#include "android/physics/XrDeviceModel.h"

#include "aemu/base/logging/Log.h"
#include "aemu/base/misc/StringUtils.h"
#include "android/console.h"
#include "android/emulation/control/adb/AdbInterface.h"
#include "android/utils/debug.h"
#include "host-common/FeatureControl.h"
#include "host-common/hw-config.h"

#define D(...) VERBOSE_PRINT(sensors, __VA_ARGS__)
#define W(...) dwarning(__VA_ARGS__)
#define E(...) derror(__VA_ARGS__)

namespace android {
namespace physics {

void XrDeviceModel::setXrInputMode(float value,
                                   PhysicalInterpolation /*mode*/) {
    enum XrInputMode m = (enum XrInputMode)value;
    sendXrInputMode(m);
    mLastInputModeRequested = m;
}

float XrDeviceModel::getXrInputMode(
        ParameterValueType /*parameterValueType*/) const {
    return mLastInputModeRequested;
}

void XrDeviceModel::setXrEnvironmentMode(float value,
                                         PhysicalInterpolation /*mode*/) {
    const enum XrEnvironmentMode m = (enum XrEnvironmentMode)value;
    sendXrEnvironmentMode(m);
    mLastEnvironmentModeRequested = m;
}

float XrDeviceModel::getXrEnvironmentMode(
        ParameterValueType /*parameterValueType*/) const {
    return mLastInputModeRequested;
}

void XrDeviceModel::setXrScreenRecenter(float value,
                                        PhysicalInterpolation /*mode*/) {
    sendXrScreenRecenter();
}

float XrDeviceModel::getXrScreenRecenter(
        ParameterValueType /*parameterValueType*/) const {
    return 0;
}

void XrDeviceModel::setXrViewportControlMode(float value,
                                             PhysicalInterpolation /*mode*/) {
    const enum XrViewportControlMode m = (enum XrViewportControlMode)value;
    sendXrViewportControlMode(m);
    mLastViewportControlModeRequested = m;
}

float XrDeviceModel::getXrViewportControlMode(
        ParameterValueType /*parameterValueType*/) const {
    return mLastViewportControlModeRequested;
}

void XrDeviceModel::sendXrInputMode(enum XrInputMode mode) {
    D("XrDeviceModel::sendXrInputMode");
}

void XrDeviceModel::sendXrEnvironmentMode(enum XrEnvironmentMode mode) {
    D("XrDeviceModel::sendXrEnvironmentMode");
}

void XrDeviceModel::sendXrScreenRecenter() {
    D("XrDeviceModel::sendXrScreenRecenter");
}

void XrDeviceModel::sendXrViewportControlMode(enum XrViewportControlMode mode) {
    D("XrDeviceModel::sendXrViewportControlMode");
}

}  // namespace physics
}  // namespace android
