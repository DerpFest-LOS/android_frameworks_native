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

#include <binder/Binder.h>

#include <gtest/gtest.h>
#include <gui/GLConsumer.h>
#include <gui/Surface.h>
#include <gui/SurfaceComposerClient.h>

namespace android {
namespace {

class VirtualDisplayTest : public ::testing::Test {
protected:
    void SetUp() override {
#if COM_ANDROID_GRAPHICS_LIBGUI_FLAGS(WB_CONSUMER_BASE_OWNS_BQ)
        mGLConsumer = sp<GLConsumer>::make(GLConsumer::TEXTURE_EXTERNAL, true, false, false);
        mGLConsumer->setName(String8("Virtual disp consumer (VirtualDisplayTest)"));
        mGLConsumer->setDefaultBufferSize(100, 100);
        mProducer = mGLConsumer->getSurface()->getIGraphicBufferProducer();
#else
        sp<IGraphicBufferConsumer> consumer;

        BufferQueue::createBufferQueue(&mProducer, &consumer);
        consumer->setConsumerName(String8("Virtual disp consumer (VirtualDisplayTest)"));
        consumer->setDefaultBufferSize(100, 100);

        mGLConsumer = sp<GLConsumer>::make(consumer, GLConsumer::TEXTURE_EXTERNAL, true, false);
#endif // COM_ANDROID_GRAPHICS_LIBGUI_FLAGS(WB_CONSUMER_BASE_OWNS_BQ)
    }

    sp<IGraphicBufferProducer> mProducer;
    sp<GLConsumer> mGLConsumer;
};

TEST_F(VirtualDisplayTest, VirtualDisplayDestroyedSurfaceReuse) {
    static const std::string kDisplayName("VirtualDisplay");
    sp<IBinder> virtualDisplay =
            SurfaceComposerClient::createVirtualDisplay(kDisplayName, false /*isSecure*/);

    SurfaceComposerClient::Transaction t;
    t.setDisplaySurface(virtualDisplay, mProducer);
    t.apply(true);

    EXPECT_EQ(NO_ERROR, SurfaceComposerClient::destroyVirtualDisplay(virtualDisplay));
    virtualDisplay.clear();
    // Sync here to ensure the display was completely destroyed in SF
    t.apply(true);
    // add another sync since we are deferring the display destruction
    t.apply(true);

    sp<Surface> surface = sp<Surface>::make(mProducer);
    sp<ANativeWindow> window(surface);

    ASSERT_EQ(NO_ERROR, native_window_api_connect(window.get(), NATIVE_WINDOW_API_EGL));
}

} // namespace
} // namespace android