// Copyright (C) 2023 The Android Open Source Project
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
#include "android/emulation/control/utils/SimpleVirtualSceneClient.h"

#include <grpcpp/grpcpp.h>
#include <tuple>

#include "absl/status/status.h"
#include "absl/strings/string_view.h"
#include "aemu/base/logging/CLog.h"
#include "android/grpc/utils/SimpleAsyncGrpc.h"
#include "android/emulation/control/utils/GenericCallbackFunctions.h"


using google::protobuf::Empty;
namespace android {
namespace emulation {
namespace control {
void SimpleVirtualSceneServiceClient::listPostersAsync(
        OnCompleted<PosterList> onDone) {
    auto [request, response, context] =
            createGrpcRequestContext<Empty, PosterList>(mClient);
    mService->async()->listPosters(
            context, request, response,
            grpcCallCompletionHandler(context, request, response, onDone));
}

void SimpleVirtualSceneServiceClient::setPosterAsync(
        Poster poster,
        OnCompleted<Poster> onDone) {
    auto [request, response, context] =
            createGrpcRequestContext<Poster, Poster>(mClient);
    request->CopyFrom(poster);
    mService->async()->setPoster(
            context, request, response,
            grpcCallCompletionHandler(context, request, response, onDone));
}

void SimpleVirtualSceneServiceClient::setAnimationState(
        AnimationState state,
        OnCompleted<AnimationState> onDone) {
    auto [request, response, context] =
            createGrpcRequestContext<AnimationState, AnimationState>(mClient);
    request->CopyFrom(state);
    mService->async()->setAnimationState(
            context, request, response,
            grpcCallCompletionHandler(context, request, response, onDone));
}
void SimpleVirtualSceneServiceClient::getAnimationState(
        OnCompleted<AnimationState> onDone) {
    auto [request, response, context] =
            createGrpcRequestContext<Empty, AnimationState>(mClient);
    mService->async()->getAnimationState(
            context, request, response,
            grpcCallCompletionHandler(context, request, response, onDone));
}

}  // namespace control
}  // namespace emulation
}  // namespace android