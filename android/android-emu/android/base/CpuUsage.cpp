// Copyright 2018 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
#include "android/base/CpuUsage.h"

#include "android/base/async/Looper.h"
#include "android/base/memory/LazyInstance.h"
#include "android/base/synchronization/ConditionVariable.h"
#include "android/base/synchronization/Lock.h"
#include "android/base/system/System.h"
#include "android/base/threads/FunctorThread.h"

#include <array>

#include <stdio.h>

namespace android {
namespace base {

class CpuUsage::Impl {
public:
    Impl() : mWorkerThread([this] { workerThread(); }) {
        mWorkerThread.start();
    }

    struct LooperMeasurement {
        Looper* looper = nullptr;
        std::unique_ptr<Looper::Task> task;
        CpuTime cpuTime;
        CpuTime lastMeasurement;
    };

    void stop() {
        {
            AutoLock lock(mLock);
            for (auto& m : mMeasurements) {
                if (m.task) {
                    m.task->cancel();
                }
            }
            mStopping = true;
            mWorkerThreadCv.signal();
        }
        mWorkerThread.wait();
    }

    ~Impl() {
        stop();
    }

    void addLooper(int usageArea, Looper* looper) {
        if (usageArea >= mMeasurements.size()) return;

        AutoLock lock(mLock);
        mMeasurements[usageArea].looper = looper;
        mMeasurements[usageArea].task =
            looper->createTask(
                [this, usageArea] { doMeasurement(usageArea); });
    }

    void setEnabled(bool enable) {
        AutoLock lock(mLock);
        mEnabled = enable;
    }

    void setMeasurementInterval(CpuUsage::IntervalUs interval) {
        AutoLock lock(mLock);
        mMeasurementIntervalUs = interval;
    }

    void forEachMeasurement(int start, int end, CpuUsage::CpuTimeReader func) {
        AutoLock lock(mLock);
        for (int i = start; i < end; ++i) {
            if (!mMeasurements[i].looper) return;
            func(mMeasurements[i].lastMeasurement);
        }
    }

private:

    void doMeasurement(int usageArea) {
        auto& prevTime = mMeasurements[usageArea].cpuTime;
        auto& lastMeasurement = mMeasurements[usageArea].lastMeasurement;

        CpuTime nextMeasurement = System::cpuTime();
        lastMeasurement = nextMeasurement - prevTime;
        prevTime = nextMeasurement;
    }

    void workerThread() {
        auto nextDeadline = [this]() {
            return System::get()->getUnixTimeUs() + mMeasurementIntervalUs;
        };
        AutoLock lock(mLock);
        for (;;) {
            auto waitUntilUs = nextDeadline();
            while (System::get()->getUnixTimeUs() < waitUntilUs) {
                mWorkerThreadCv.timedWait(&mLock, waitUntilUs);
            }

            if (mStopping) {
                break;
            }

            if (!mEnabled)
                continue;

            for (auto& m : mMeasurements) {
                if (!m.looper) continue;
                m.task->schedule();
            }
        }
    }
    std::array<LooperMeasurement, CpuUsage::UsageArea::Max> mMeasurements = {};
    bool mEnabled = true;
    CpuUsage::IntervalUs mMeasurementIntervalUs = 1000000ULL;
    FunctorThread mWorkerThread;
    ConditionVariable mWorkerThreadCv;
    bool mStopping = false;
    Lock mLock;
};

CpuUsage::CpuUsage() : mImpl(new CpuUsage::Impl()) { }

void CpuUsage::addLooper(int usageArea, Looper* looper) {
    mImpl->addLooper(usageArea, looper);
}

void CpuUsage::setEnabled(bool enable) {
    mImpl->setEnabled(enable);
}

void CpuUsage::setMeasurementInterval(CpuUsage::IntervalUs interval) {
    mImpl->setMeasurementInterval(interval);
}

void CpuUsage::forEachUsage(UsageArea area, CpuTimeReader readerFunc) {
    int start;

    if (area >= UsageArea::MainLoop && area < UsageArea::Vcpu) {
        mImpl->forEachMeasurement(UsageArea::MainLoop, UsageArea::Vcpu, readerFunc);
    } else if (area >= UsageArea::Vcpu && area < UsageArea::RenderThreads) {
        mImpl->forEachMeasurement(UsageArea::Vcpu, UsageArea::RenderThreads, readerFunc);
    } else if (area >= UsageArea::RenderThreads && area < UsageArea::Max) {
        mImpl->forEachMeasurement(UsageArea::RenderThreads, UsageArea::Max, readerFunc);
    } else {
        fprintf(stderr, "%s: warning: invalid usage area %d detected.\n", __func__, area);
    }
}

static LazyInstance<CpuUsage> sCpuUsage = LAZY_INSTANCE_INIT;

// static
CpuUsage* CpuUsage::get() {
    return sCpuUsage.ptr();
}

// static
void CpuUsage::stop() {
    mImpl->stop();
}

} // namespace android
} // namespace base
