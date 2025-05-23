/*
 * Copyright (C) 2019 The Android Open Source Project
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

#include <fuzzbinder/random_parcel.h>
#include <fuzzer/FuzzedDataProvider.h>

#include <functional>

template <typename P>
using ParcelRead = std::function<void(const P& p, FuzzedDataProvider& provider)>;
template <typename P>
using ParcelWrite = std::function<void(P& p, FuzzedDataProvider& provider,
                                       android::RandomParcelOptions* options)>;
