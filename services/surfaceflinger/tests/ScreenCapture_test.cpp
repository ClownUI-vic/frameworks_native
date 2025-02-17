/*
 * Copyright (C) 2020 The Android Open Source Project
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

// TODO(b/129481165): remove the #pragma below and fix conversion issues
#include <sys/types.h>
#include <cstdint>
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wconversion"

#include <gui/AidlUtil.h>
#include <private/android_filesystem_config.h>
#include <ui/DisplayState.h>

#include "LayerTransactionTest.h"

namespace android {

class ScreenCaptureTest : public LayerTransactionTest {
protected:
    virtual void SetUp() {
        LayerTransactionTest::SetUp();
        ASSERT_EQ(NO_ERROR, mClient->initCheck());

        // Root surface
        mRootSurfaceControl =
                createLayer(String8("RootTestSurface"), mDisplayWidth, mDisplayHeight, 0);
        ASSERT_TRUE(mRootSurfaceControl != nullptr);
        ASSERT_TRUE(mRootSurfaceControl->isValid());

        // Background surface
        mBGSurfaceControl = createLayer(String8("BG Test Surface"), mDisplayWidth, mDisplayHeight,
                                        0, mRootSurfaceControl.get());
        ASSERT_TRUE(mBGSurfaceControl != nullptr);
        ASSERT_TRUE(mBGSurfaceControl->isValid());
        TransactionUtils::fillSurfaceRGBA8(mBGSurfaceControl, 63, 63, 195);

        // Foreground surface
        mFGSurfaceControl =
                createLayer(String8("FG Test Surface"), 64, 64, 0, mRootSurfaceControl.get());

        ASSERT_TRUE(mFGSurfaceControl != nullptr);
        ASSERT_TRUE(mFGSurfaceControl->isValid());

        TransactionUtils::fillSurfaceRGBA8(mFGSurfaceControl, 195, 63, 63);

        asTransaction([&](Transaction& t) {
            t.setDisplayLayerStack(mDisplay, ui::DEFAULT_LAYER_STACK);

            t.setLayer(mBGSurfaceControl, INT32_MAX - 2).show(mBGSurfaceControl);

            t.setLayer(mFGSurfaceControl, INT32_MAX - 1)
                    .setPosition(mFGSurfaceControl, 64, 64)
                    .show(mFGSurfaceControl);
        });

        mCaptureArgs.captureArgs.sourceCrop = gui::aidl_utils::toARect(mDisplayRect);
        mCaptureArgs.layerHandle = mRootSurfaceControl->getHandle();
    }

    virtual void TearDown() {
        LayerTransactionTest::TearDown();
        mBGSurfaceControl = 0;
        mFGSurfaceControl = 0;
    }

    sp<SurfaceControl> mRootSurfaceControl;
    sp<SurfaceControl> mBGSurfaceControl;
    sp<SurfaceControl> mFGSurfaceControl;
    std::unique_ptr<ScreenCapture> mCapture;
    LayerCaptureArgs mCaptureArgs;
};

TEST_F(ScreenCaptureTest, SetFlagsSecureEUidSystem) {
    sp<SurfaceControl> layer;
    ASSERT_NO_FATAL_FAILURE(
            layer = createLayer("test", 32, 32,
                                ISurfaceComposerClient::eSecure |
                                        ISurfaceComposerClient::eFXSurfaceBufferQueue,
                                mRootSurfaceControl.get()));
    ASSERT_NO_FATAL_FAILURE(fillBufferQueueLayerColor(layer, Color::RED, 32, 32));

    Transaction().show(layer).setLayer(layer, INT32_MAX).apply(true);

    {
        // Ensure the UID is not root because root has all permissions
        UIDFaker f(AID_APP_START);
        ASSERT_EQ(PERMISSION_DENIED, ScreenCapture::captureLayers(mCaptureArgs, mCaptureResults));
    }

    {
        UIDFaker f(AID_SYSTEM);

        // By default the system can capture screenshots with secure layers but they
        // will be blacked out
        ASSERT_EQ(NO_ERROR, ScreenCapture::captureLayers(mCaptureArgs, mCaptureResults));

        {
            SCOPED_TRACE("as system");
            auto shot = screenshot();
            shot->expectColor(Rect(0, 0, 32, 32), Color::BLACK);
        }

        mCaptureArgs.captureArgs.captureSecureLayers = true;
        // AID_SYSTEM is allowed to capture secure content.
        ASSERT_EQ(NO_ERROR, ScreenCapture::captureLayers(mCaptureArgs, mCaptureResults));
        ASSERT_TRUE(mCaptureResults.capturedSecureLayers);
        ScreenCapture sc(mCaptureResults.buffer, mCaptureResults.capturedHdrLayers);
        sc.expectColor(Rect(0, 0, 32, 32), Color::RED);
    }

    {
        // Attempt secure screenshot from shell since it doesn't have CAPTURE_BLACKOUT_CONTENT
        // permission, but is allowed normal screenshots.
        UIDFaker faker(AID_SHELL);
        ASSERT_EQ(PERMISSION_DENIED, ScreenCapture::captureLayers(mCaptureArgs, mCaptureResults));
    }

    // Remove flag secure from the layer.
    Transaction().setFlags(layer, 0, layer_state_t::eLayerSecure).apply(true);
    {
        // Assert that screenshot fails without CAPTURE_BLACKOUT_CONTENT when requesting
        // captureSecureLayers even if there are no actual secure layers on screen.
        UIDFaker faker(AID_SHELL);
        ASSERT_EQ(PERMISSION_DENIED, ScreenCapture::captureLayers(mCaptureArgs, mCaptureResults));
    }
}

TEST_F(ScreenCaptureTest, CaptureChildSetParentFlagsSecureEUidSystem) {
    sp<SurfaceControl> parentLayer;
    ASSERT_NO_FATAL_FAILURE(
            parentLayer = createLayer("parent-test", 32, 32,
                                      ISurfaceComposerClient::eSecure |
                                              ISurfaceComposerClient::eFXSurfaceBufferQueue,
                                      mRootSurfaceControl.get()));
    ASSERT_NO_FATAL_FAILURE(fillBufferQueueLayerColor(parentLayer, Color::RED, 32, 32));

    sp<SurfaceControl> childLayer;
    ASSERT_NO_FATAL_FAILURE(childLayer = createLayer("child-test", 10, 10,
                                                     ISurfaceComposerClient::eFXSurfaceBufferQueue,
                                                     parentLayer.get()));
    ASSERT_NO_FATAL_FAILURE(fillBufferQueueLayerColor(childLayer, Color::BLUE, 10, 10));

    Transaction().show(parentLayer).setLayer(parentLayer, INT32_MAX).show(childLayer).apply(true);

    UIDFaker f(AID_SYSTEM);

    {
        SCOPED_TRACE("as system");
        auto shot = screenshot();
        shot->expectColor(Rect(0, 0, 10, 10), Color::BLACK);
    }

    // Here we pass captureSecureLayers = true and since we are AID_SYSTEM we should be able
    // to receive them...we are expected to take care with the results.
    mCaptureArgs.captureArgs.captureSecureLayers = true;
    ASSERT_EQ(NO_ERROR, ScreenCapture::captureLayers(mCaptureArgs, mCaptureResults));
    ASSERT_TRUE(mCaptureResults.capturedSecureLayers);
    ScreenCapture sc(mCaptureResults.buffer, mCaptureResults.capturedHdrLayers);
    sc.expectColor(Rect(0, 0, 10, 10), Color::BLUE);
}

/**
 * If a parent layer sets the secure flag, but the screenshot requests is for the child hierarchy,
 * we need to ensure the secure flag is respected from the parent even though the parent isn't
 * in the captured sub-hierarchy
 */
TEST_F(ScreenCaptureTest, CaptureChildRespectsParentSecureFlag) {
    Rect size(0, 0, 100, 100);
    Transaction().hide(mBGSurfaceControl).hide(mFGSurfaceControl).apply();
    sp<SurfaceControl> parentLayer;
    ASSERT_NO_FATAL_FAILURE(parentLayer = createLayer("parent-test", 0, 0,
                                                      ISurfaceComposerClient::eHidden,
                                                      mRootSurfaceControl.get()));

    sp<SurfaceControl> childLayer;
    ASSERT_NO_FATAL_FAILURE(childLayer = createLayer("child-test", 0, 0,
                                                     ISurfaceComposerClient::eFXSurfaceBufferState,
                                                     parentLayer.get()));
    ASSERT_NO_FATAL_FAILURE(
            fillBufferLayerColor(childLayer, Color::GREEN, size.width(), size.height()));

    // hide the parent layer to ensure secure flag is passed down to child when screenshotting
    Transaction().setLayer(parentLayer, INT32_MAX).show(childLayer).apply(true);
    Transaction()
            .setFlags(parentLayer, layer_state_t::eLayerSecure, layer_state_t::eLayerSecure)
            .apply();
    LayerCaptureArgs captureArgs;
    captureArgs.layerHandle = childLayer->getHandle();
    captureArgs.captureArgs.sourceCrop = gui::aidl_utils::toARect(size);
    captureArgs.captureArgs.captureSecureLayers = false;
    {
        SCOPED_TRACE("parent hidden");
        ASSERT_EQ(NO_ERROR, ScreenCapture::captureLayers(captureArgs, mCaptureResults));
        ASSERT_TRUE(mCaptureResults.capturedSecureLayers);
        ScreenCapture sc(mCaptureResults.buffer, mCaptureResults.capturedHdrLayers);
        sc.expectColor(size, Color::BLACK);
    }

    captureArgs.captureArgs.captureSecureLayers = true;
    {
        SCOPED_TRACE("capture secure parent not visible");
        ASSERT_EQ(NO_ERROR, ScreenCapture::captureLayers(captureArgs, mCaptureResults));
        ASSERT_TRUE(mCaptureResults.capturedSecureLayers);
        ScreenCapture sc(mCaptureResults.buffer, mCaptureResults.capturedHdrLayers);
        sc.expectColor(size, Color::GREEN);
    }

    Transaction().show(parentLayer).apply();
    captureArgs.captureArgs.captureSecureLayers = false;
    {
        SCOPED_TRACE("parent visible");
        ASSERT_EQ(NO_ERROR, ScreenCapture::captureLayers(captureArgs, mCaptureResults));
        ASSERT_TRUE(mCaptureResults.capturedSecureLayers);
        ScreenCapture sc(mCaptureResults.buffer, mCaptureResults.capturedHdrLayers);
        sc.expectColor(size, Color::BLACK);
    }

    captureArgs.captureArgs.captureSecureLayers = true;
    {
        SCOPED_TRACE("capture secure parent visible");
        ASSERT_EQ(NO_ERROR, ScreenCapture::captureLayers(captureArgs, mCaptureResults));
        ASSERT_TRUE(mCaptureResults.capturedSecureLayers);
        ScreenCapture sc(mCaptureResults.buffer, mCaptureResults.capturedHdrLayers);
        sc.expectColor(size, Color::GREEN);
    }
}

TEST_F(ScreenCaptureTest, CaptureOffscreenChildRespectsParentSecureFlag) {
    Rect size(0, 0, 100, 100);
    Transaction().hide(mBGSurfaceControl).hide(mFGSurfaceControl).apply();
    // Parent layer should be offscreen.
    sp<SurfaceControl> parentLayer;
    ASSERT_NO_FATAL_FAILURE(
            parentLayer = createLayer("parent-test", 0, 0, ISurfaceComposerClient::eHidden));

    sp<SurfaceControl> childLayer;
    ASSERT_NO_FATAL_FAILURE(childLayer = createLayer("child-test", 0, 0,
                                                     ISurfaceComposerClient::eFXSurfaceBufferState,
                                                     parentLayer.get()));
    ASSERT_NO_FATAL_FAILURE(
            fillBufferLayerColor(childLayer, Color::GREEN, size.width(), size.height()));

    // hide the parent layer to ensure secure flag is passed down to child when screenshotting
    Transaction().setLayer(parentLayer, INT32_MAX).show(childLayer).apply(true);
    Transaction()
            .setFlags(parentLayer, layer_state_t::eLayerSecure, layer_state_t::eLayerSecure)
            .apply();
    LayerCaptureArgs captureArgs;
    captureArgs.layerHandle = childLayer->getHandle();
    captureArgs.captureArgs.sourceCrop = gui::aidl_utils::toARect(size);
    captureArgs.captureArgs.captureSecureLayers = false;
    {
        SCOPED_TRACE("parent hidden");
        ASSERT_EQ(NO_ERROR, ScreenCapture::captureLayers(captureArgs, mCaptureResults));
        ASSERT_TRUE(mCaptureResults.capturedSecureLayers);
        ScreenCapture sc(mCaptureResults.buffer, mCaptureResults.capturedHdrLayers);
        sc.expectColor(size, Color::BLACK);
    }

    captureArgs.captureArgs.captureSecureLayers = true;
    {
        SCOPED_TRACE("capture secure parent not visible");
        ASSERT_EQ(NO_ERROR, ScreenCapture::captureLayers(captureArgs, mCaptureResults));
        ASSERT_TRUE(mCaptureResults.capturedSecureLayers);
        ScreenCapture sc(mCaptureResults.buffer, mCaptureResults.capturedHdrLayers);
        sc.expectColor(size, Color::GREEN);
    }

    Transaction().show(parentLayer).apply();
    captureArgs.captureArgs.captureSecureLayers = false;
    {
        SCOPED_TRACE("parent visible");
        ASSERT_EQ(NO_ERROR, ScreenCapture::captureLayers(captureArgs, mCaptureResults));
        ASSERT_TRUE(mCaptureResults.capturedSecureLayers);
        ScreenCapture sc(mCaptureResults.buffer, mCaptureResults.capturedHdrLayers);
        sc.expectColor(size, Color::BLACK);
    }

    captureArgs.captureArgs.captureSecureLayers = true;
    {
        SCOPED_TRACE("capture secure parent visible");
        ASSERT_EQ(NO_ERROR, ScreenCapture::captureLayers(captureArgs, mCaptureResults));
        ASSERT_TRUE(mCaptureResults.capturedSecureLayers);
        ScreenCapture sc(mCaptureResults.buffer, mCaptureResults.capturedHdrLayers);
        sc.expectColor(size, Color::GREEN);
    }
}

TEST_F(ScreenCaptureTest, CaptureSingleLayer) {
    LayerCaptureArgs captureArgs;
    captureArgs.layerHandle = mBGSurfaceControl->getHandle();
    ScreenCapture::captureLayers(&mCapture, captureArgs);
    mCapture->expectBGColor(0, 0);
    // Doesn't capture FG layer which is at 64, 64
    mCapture->expectBGColor(64, 64);
}

TEST_F(ScreenCaptureTest, CaptureLayerWithChild) {
    sp<SurfaceControl> child = createSurface(mClient, "Child surface", 10, 10,
                                             PIXEL_FORMAT_RGBA_8888, 0, mFGSurfaceControl.get());
    TransactionUtils::fillSurfaceRGBA8(child, 200, 200, 200);

    SurfaceComposerClient::Transaction().show(child).apply(true);

    // Captures mFGSurfaceControl layer and its child.
    LayerCaptureArgs captureArgs;
    captureArgs.layerHandle = mFGSurfaceControl->getHandle();
    ScreenCapture::captureLayers(&mCapture, captureArgs);
    mCapture->expectFGColor(10, 10);
    mCapture->expectChildColor(0, 0);
}

TEST_F(ScreenCaptureTest, CaptureLayerChildOnly) {
    auto fgHandle = mFGSurfaceControl->getHandle();

    sp<SurfaceControl> child = createSurface(mClient, "Child surface", 10, 10,
                                             PIXEL_FORMAT_RGBA_8888, 0, mFGSurfaceControl.get());
    TransactionUtils::fillSurfaceRGBA8(child, 200, 200, 200);

    SurfaceComposerClient::Transaction().show(child).apply(true);

    // Captures mFGSurfaceControl's child
    LayerCaptureArgs captureArgs;
    captureArgs.layerHandle = fgHandle;
    captureArgs.childrenOnly = true;
    ScreenCapture::captureLayers(&mCapture, captureArgs);
    mCapture->checkPixel(10, 10, 0, 0, 0);
    mCapture->expectChildColor(0, 0);
}

TEST_F(ScreenCaptureTest, CaptureLayerExclude) {
    auto fgHandle = mFGSurfaceControl->getHandle();

    sp<SurfaceControl> child = createSurface(mClient, "Child surface", 10, 10,
                                             PIXEL_FORMAT_RGBA_8888, 0, mFGSurfaceControl.get());
    TransactionUtils::fillSurfaceRGBA8(child, 200, 200, 200);
    sp<SurfaceControl> child2 = createSurface(mClient, "Child surface", 10, 10,
                                              PIXEL_FORMAT_RGBA_8888, 0, mFGSurfaceControl.get());
    TransactionUtils::fillSurfaceRGBA8(child2, 200, 0, 200);

    SurfaceComposerClient::Transaction()
            .show(child)
            .show(child2)
            .setLayer(child, 1)
            .setLayer(child2, 2)
            .apply(true);

    // Child2 would be visible but its excluded, so we should see child1 color instead.
    LayerCaptureArgs captureArgs;
    captureArgs.layerHandle = fgHandle;
    captureArgs.childrenOnly = true;
    captureArgs.captureArgs.excludeHandles = {child2->getHandle()};
    ScreenCapture::captureLayers(&mCapture, captureArgs);
    mCapture->checkPixel(10, 10, 0, 0, 0);
    mCapture->checkPixel(0, 0, 200, 200, 200);
}

TEST_F(ScreenCaptureTest, CaptureLayerExcludeThroughDisplayArgs) {
    mCaptureArgs.captureArgs.excludeHandles = {mFGSurfaceControl->getHandle()};
    ScreenCapture::captureLayers(&mCapture, mCaptureArgs);
    mCapture->expectBGColor(0, 0);
    // Doesn't capture FG layer which is at 64, 64
    mCapture->expectBGColor(64, 64);
}

// Like the last test but verifies that children are also exclude.
TEST_F(ScreenCaptureTest, CaptureLayerExcludeTree) {
    auto fgHandle = mFGSurfaceControl->getHandle();

    sp<SurfaceControl> child = createSurface(mClient, "Child surface", 10, 10,
                                             PIXEL_FORMAT_RGBA_8888, 0, mFGSurfaceControl.get());
    TransactionUtils::fillSurfaceRGBA8(child, 200, 200, 200);
    sp<SurfaceControl> child2 = createSurface(mClient, "Child surface", 10, 10,
                                              PIXEL_FORMAT_RGBA_8888, 0, mFGSurfaceControl.get());
    TransactionUtils::fillSurfaceRGBA8(child2, 200, 0, 200);
    sp<SurfaceControl> child3 = createSurface(mClient, "Child surface", 10, 10,
                                              PIXEL_FORMAT_RGBA_8888, 0, child2.get());
    TransactionUtils::fillSurfaceRGBA8(child2, 200, 0, 200);

    SurfaceComposerClient::Transaction()
            .show(child)
            .show(child2)
            .show(child3)
            .setLayer(child, 1)
            .setLayer(child2, 2)
            .apply(true);

    // Child2 would be visible but its excluded, so we should see child1 color instead.
    LayerCaptureArgs captureArgs;
    captureArgs.layerHandle = fgHandle;
    captureArgs.childrenOnly = true;
    captureArgs.captureArgs.excludeHandles = {child2->getHandle()};
    ScreenCapture::captureLayers(&mCapture, captureArgs);
    mCapture->checkPixel(10, 10, 0, 0, 0);
    mCapture->checkPixel(0, 0, 200, 200, 200);
}

TEST_F(ScreenCaptureTest, CaptureTransparent) {
    sp<SurfaceControl> child = createSurface(mClient, "Child surface", 10, 10,
                                             PIXEL_FORMAT_RGBA_8888, 0, mFGSurfaceControl.get());

    TransactionUtils::fillSurfaceRGBA8(child, 200, 200, 200);

    SurfaceComposerClient::Transaction().show(child).apply(true);

    // Captures child
    LayerCaptureArgs captureArgs;
    captureArgs.layerHandle = child->getHandle();
    captureArgs.captureArgs.sourceCrop = gui::aidl_utils::toARect(10, 20);
    ScreenCapture::captureLayers(&mCapture, captureArgs);
    mCapture->expectColor(Rect(0, 0, 9, 9), {200, 200, 200, 255});
    // Area outside of child's bounds is transparent.
    mCapture->expectColor(Rect(0, 10, 9, 19), {0, 0, 0, 0});
}

TEST_F(ScreenCaptureTest, DontCaptureRelativeOutsideTree) {
    sp<SurfaceControl> child = createSurface(mClient, "Child surface", 10, 10,
                                             PIXEL_FORMAT_RGBA_8888, 0, mFGSurfaceControl.get());
    ASSERT_NE(nullptr, child.get()) << "failed to create surface";
    sp<SurfaceControl> relative = createLayer(String8("Relative surface"), 10, 10, 0);
    TransactionUtils::fillSurfaceRGBA8(child, 200, 200, 200);
    TransactionUtils::fillSurfaceRGBA8(relative, 100, 100, 100);

    SurfaceComposerClient::Transaction()
            .show(child)
            // Set relative layer above fg layer so should be shown above when computing all layers.
            .setRelativeLayer(relative, mFGSurfaceControl, 1)
            .show(relative)
            .apply(true);

    // Captures mFGSurfaceControl layer and its child. Relative layer shouldn't be captured.
    LayerCaptureArgs captureArgs;
    captureArgs.layerHandle = mFGSurfaceControl->getHandle();
    ScreenCapture::captureLayers(&mCapture, captureArgs);
    mCapture->expectFGColor(10, 10);
    mCapture->expectChildColor(0, 0);
}

TEST_F(ScreenCaptureTest, CaptureRelativeInTree) {
    sp<SurfaceControl> child = createSurface(mClient, "Child surface", 10, 10,
                                             PIXEL_FORMAT_RGBA_8888, 0, mFGSurfaceControl.get());
    sp<SurfaceControl> relative = createSurface(mClient, "Relative surface", 10, 10,
                                                PIXEL_FORMAT_RGBA_8888, 0, mFGSurfaceControl.get());
    TransactionUtils::fillSurfaceRGBA8(child, 200, 200, 200);
    TransactionUtils::fillSurfaceRGBA8(relative, 100, 100, 100);

    SurfaceComposerClient::Transaction()
            .show(child)
            // Set relative layer below fg layer but relative to child layer so it should be shown
            // above child layer.
            .setLayer(relative, -1)
            .setRelativeLayer(relative, child, 1)
            .show(relative)
            .apply(true);

    // Captures mFGSurfaceControl layer and its children. Relative layer is a child of fg so its
    // relative value should be taken into account, placing it above child layer.
    LayerCaptureArgs captureArgs;
    captureArgs.layerHandle = mFGSurfaceControl->getHandle();
    ScreenCapture::captureLayers(&mCapture, captureArgs);
    mCapture->expectFGColor(10, 10);
    // Relative layer is showing on top of child layer
    mCapture->expectColor(Rect(0, 0, 9, 9), {100, 100, 100, 255});
}

TEST_F(ScreenCaptureTest, CaptureBoundlessLayerWithSourceCrop) {
    sp<SurfaceControl> child = createColorLayer("Child layer", Color::RED, mFGSurfaceControl.get());
    SurfaceComposerClient::Transaction().show(child).apply(true);

    LayerCaptureArgs captureArgs;
    captureArgs.layerHandle = child->getHandle();
    captureArgs.captureArgs.sourceCrop = gui::aidl_utils::toARect(10, 10);
    ScreenCapture::captureLayers(&mCapture, captureArgs);

    mCapture->expectColor(Rect(0, 0, 9, 9), Color::RED);
}

TEST_F(ScreenCaptureTest, CaptureBoundedLayerWithoutSourceCrop) {
    sp<SurfaceControl> child = createColorLayer("Child layer", Color::RED, mFGSurfaceControl.get());
    Rect layerCrop(0, 0, 10, 10);
    SurfaceComposerClient::Transaction().setCrop(child, layerCrop).show(child).apply(true);

    LayerCaptureArgs captureArgs;
    captureArgs.layerHandle = child->getHandle();
    ScreenCapture::captureLayers(&mCapture, captureArgs);

    mCapture->expectColor(Rect(0, 0, 9, 9), Color::RED);
}

TEST_F(ScreenCaptureTest, CaptureBoundlessLayerWithoutSourceCropFails) {
    sp<SurfaceControl> child = createColorLayer("Child layer", Color::RED, mFGSurfaceControl.get());
    SurfaceComposerClient::Transaction().show(child).apply(true);

    LayerCaptureArgs args;
    args.layerHandle = child->getHandle();

    ScreenCaptureResults captureResults;
    ASSERT_EQ(BAD_VALUE, ScreenCapture::captureLayers(args, captureResults));
}

TEST_F(ScreenCaptureTest, CaptureBufferLayerWithoutBufferFails) {
    sp<SurfaceControl> child = createSurface(mClient, "Child surface", 10, 10,
                                             PIXEL_FORMAT_RGBA_8888,
                                             ISurfaceComposerClient::eFXSurfaceBufferState,
                                             mFGSurfaceControl.get());

    SurfaceComposerClient::Transaction().show(child).apply(true);
    sp<GraphicBuffer> outBuffer;

    LayerCaptureArgs args;
    args.layerHandle = child->getHandle();
    args.childrenOnly = false;

    ScreenCaptureResults captureResults;
    ASSERT_EQ(BAD_VALUE, ScreenCapture::captureLayers(args, captureResults));

    ASSERT_NO_FATAL_FAILURE(fillBufferLayerColor(child, Color::RED, 32, 32));
    SurfaceComposerClient::Transaction().apply(true);
    ASSERT_EQ(NO_ERROR, ScreenCapture::captureLayers(args, captureResults));
    ScreenCapture sc(captureResults.buffer, captureResults.capturedHdrLayers);
    sc.expectColor(Rect(0, 0, 9, 9), Color::RED);
}

TEST_F(ScreenCaptureTest, CaptureLayerWithGrandchild) {
    sp<SurfaceControl> child = createSurface(mClient, "Child surface", 10, 10,
                                             PIXEL_FORMAT_RGBA_8888, 0, mFGSurfaceControl.get());
    TransactionUtils::fillSurfaceRGBA8(child, 200, 200, 200);

    sp<SurfaceControl> grandchild = createSurface(mClient, "Grandchild surface", 5, 5,
                                                  PIXEL_FORMAT_RGBA_8888, 0, child.get());

    TransactionUtils::fillSurfaceRGBA8(grandchild, 50, 50, 50);
    SurfaceComposerClient::Transaction()
            .show(child)
            .setPosition(grandchild, 5, 5)
            .show(grandchild)
            .apply(true);

    // Captures mFGSurfaceControl, its child, and the grandchild.
    LayerCaptureArgs captureArgs;
    captureArgs.layerHandle = mFGSurfaceControl->getHandle();
    ScreenCapture::captureLayers(&mCapture, captureArgs);
    mCapture->expectFGColor(10, 10);
    mCapture->expectChildColor(0, 0);
    mCapture->checkPixel(5, 5, 50, 50, 50);
}

TEST_F(ScreenCaptureTest, CaptureChildOnly) {
    sp<SurfaceControl> child = createSurface(mClient, "Child surface", 10, 10,
                                             PIXEL_FORMAT_RGBA_8888, 0, mFGSurfaceControl.get());
    TransactionUtils::fillSurfaceRGBA8(child, 200, 200, 200);

    SurfaceComposerClient::Transaction().setPosition(child, 5, 5).show(child).apply(true);

    // Captures only the child layer, and not the parent.
    LayerCaptureArgs captureArgs;
    captureArgs.layerHandle = child->getHandle();
    ScreenCapture::captureLayers(&mCapture, captureArgs);
    mCapture->expectChildColor(0, 0);
    mCapture->expectChildColor(9, 9);
}

TEST_F(ScreenCaptureTest, CaptureGrandchildOnly) {
    sp<SurfaceControl> child = createSurface(mClient, "Child surface", 10, 10,
                                             PIXEL_FORMAT_RGBA_8888, 0, mFGSurfaceControl.get());
    TransactionUtils::fillSurfaceRGBA8(child, 200, 200, 200);
    auto childHandle = child->getHandle();

    sp<SurfaceControl> grandchild = createSurface(mClient, "Grandchild surface", 5, 5,
                                                  PIXEL_FORMAT_RGBA_8888, 0, child.get());
    TransactionUtils::fillSurfaceRGBA8(grandchild, 50, 50, 50);

    SurfaceComposerClient::Transaction()
            .show(child)
            .setPosition(grandchild, 5, 5)
            .show(grandchild)
            .apply(true);

    // Captures only the grandchild.
    LayerCaptureArgs captureArgs;
    captureArgs.layerHandle = grandchild->getHandle();
    ScreenCapture::captureLayers(&mCapture, captureArgs);
    mCapture->checkPixel(0, 0, 50, 50, 50);
    mCapture->checkPixel(4, 4, 50, 50, 50);
}

TEST_F(ScreenCaptureTest, CaptureCrop) {
    sp<SurfaceControl> redLayer = createLayer(String8("Red surface"), 60, 60,
                                              ISurfaceComposerClient::eFXSurfaceBufferState);
    sp<SurfaceControl> blueLayer = createSurface(mClient, "Blue surface", 30, 30,
                                                 PIXEL_FORMAT_RGBA_8888,
                                                 ISurfaceComposerClient::eFXSurfaceBufferState,
                                                 redLayer.get());

    ASSERT_NO_FATAL_FAILURE(fillBufferLayerColor(redLayer, Color::RED, 60, 60));
    ASSERT_NO_FATAL_FAILURE(fillBufferLayerColor(blueLayer, Color::BLUE, 30, 30));

    SurfaceComposerClient::Transaction()
            .setLayer(redLayer, INT32_MAX - 1)
            .show(redLayer)
            .show(blueLayer)
            .apply(true);

    // Capturing full screen should have both red and blue are visible.
    LayerCaptureArgs captureArgs;
    captureArgs.layerHandle = redLayer->getHandle();
    ScreenCapture::captureLayers(&mCapture, captureArgs);
    mCapture->expectColor(Rect(0, 0, 29, 29), Color::BLUE);
    // red area below the blue area
    mCapture->expectColor(Rect(0, 30, 59, 59), Color::RED);
    // red area to the right of the blue area
    mCapture->expectColor(Rect(30, 0, 59, 59), Color::RED);

    captureArgs.captureArgs.sourceCrop = gui::aidl_utils::toARect(30, 30);
    ScreenCapture::captureLayers(&mCapture, captureArgs);
    // Capturing the cropped screen, cropping out the shown red area, should leave only the blue
    // area visible.
    mCapture->expectColor(Rect(0, 0, 29, 29), Color::BLUE);
    mCapture->checkPixel(30, 30, 0, 0, 0);
}

TEST_F(ScreenCaptureTest, CaptureSize) {
  sp<SurfaceControl> redLayer =
      createLayer(String8("Red surface"), 60, 60, ISurfaceComposerClient::eFXSurfaceBufferState);
    sp<SurfaceControl> blueLayer = createSurface(mClient, "Blue surface", 30, 30,
                                                 PIXEL_FORMAT_RGBA_8888,
                                                 ISurfaceComposerClient::eFXSurfaceBufferState,
                                                 redLayer.get());

    ASSERT_NO_FATAL_FAILURE(fillBufferLayerColor(redLayer, Color::RED, 60, 60));
    ASSERT_NO_FATAL_FAILURE(fillBufferLayerColor(blueLayer, Color::BLUE, 30, 30));

    SurfaceComposerClient::Transaction()
            .setLayer(redLayer, INT32_MAX - 1)
            .show(redLayer)
            .show(blueLayer)
            .apply(true);

    // Capturing full screen should have both red and blue are visible.
    LayerCaptureArgs captureArgs;
    captureArgs.layerHandle = redLayer->getHandle();
    ScreenCapture::captureLayers(&mCapture, captureArgs);
    mCapture->expectColor(Rect(0, 0, 29, 29), Color::BLUE);
    // red area below the blue area
    mCapture->expectColor(Rect(0, 30, 59, 59), Color::RED);
    // red area to the right of the blue area
    mCapture->expectColor(Rect(30, 0, 59, 59), Color::RED);

    captureArgs.captureArgs.frameScaleX = 0.5f;
    captureArgs.captureArgs.frameScaleY = 0.5f;
    sleep(1);

    ScreenCapture::captureLayers(&mCapture, captureArgs);
    // Capturing the downsized area (30x30) should leave both red and blue but in a smaller area.
    mCapture->expectColor(Rect(0, 0, 14, 14), Color::BLUE);
    // red area below the blue area
    mCapture->expectColor(Rect(0, 15, 29, 29), Color::RED);
    // red area to the right of the blue area
    mCapture->expectColor(Rect(15, 0, 29, 29), Color::RED);
    mCapture->checkPixel(30, 30, 0, 0, 0);
}

TEST_F(ScreenCaptureTest, CaptureInvalidLayer) {
    LayerCaptureArgs args;
    args.layerHandle = sp<BBinder>::make();

    ScreenCaptureResults captureResults;
    // Layer was deleted so captureLayers should fail with NAME_NOT_FOUND
    ASSERT_EQ(NAME_NOT_FOUND, ScreenCapture::captureLayers(args, captureResults));
}

TEST_F(ScreenCaptureTest, CaptureTooLargeLayer) {
    sp<SurfaceControl> redLayer = createLayer(String8("Red surface"), 60, 60);
    ASSERT_NO_FATAL_FAILURE(fillBufferQueueLayerColor(redLayer, Color::RED, 60, 60));

    Transaction().show(redLayer).setLayer(redLayer, INT32_MAX).apply(true);

    LayerCaptureArgs captureArgs;
    captureArgs.layerHandle = redLayer->getHandle();
    captureArgs.captureArgs.frameScaleX = INT32_MAX / 60;
    captureArgs.captureArgs.frameScaleY = INT32_MAX / 60;

    ScreenCaptureResults captureResults;
    ASSERT_EQ(BAD_VALUE, ScreenCapture::captureLayers(captureArgs, captureResults));
}

TEST_F(ScreenCaptureTest, CaptureSecureLayer) {
    sp<SurfaceControl> redLayer = createLayer(String8("Red surface"), 60, 60,
                                              ISurfaceComposerClient::eFXSurfaceBufferState);
    sp<SurfaceControl> secureLayer =
            createLayer(String8("Secure surface"), 30, 30,
                        ISurfaceComposerClient::eSecure |
                                ISurfaceComposerClient::eFXSurfaceBufferState,
                        redLayer.get());
    ASSERT_NO_FATAL_FAILURE(fillBufferLayerColor(redLayer, Color::RED, 60, 60));
    ASSERT_NO_FATAL_FAILURE(fillBufferLayerColor(secureLayer, Color::BLUE, 30, 30));

    auto redLayerHandle = redLayer->getHandle();
    Transaction()
            .show(redLayer)
            .show(secureLayer)
            .setLayerStack(redLayer, ui::DEFAULT_LAYER_STACK)
            .setLayer(redLayer, INT32_MAX)
            .apply();

    LayerCaptureArgs args;
    args.layerHandle = redLayerHandle;
    args.childrenOnly = false;
    ScreenCaptureResults captureResults;

    {
        // Ensure the UID is not root because root has all permissions
        UIDFaker f(AID_APP_START);
        // Call from outside system with secure layers will result in permission denied
        ASSERT_EQ(PERMISSION_DENIED, ScreenCapture::captureLayers(args, captureResults));
    }

    UIDFaker f(AID_SYSTEM);

    // From system request, only red layer will be screenshot since the blue layer is secure.
    // Black will be present where the secure layer is.
    ScreenCapture::captureLayers(&mCapture, args);
    mCapture->expectColor(Rect(0, 0, 30, 30), Color::BLACK);
    mCapture->expectColor(Rect(30, 30, 60, 60), Color::RED);

    // Passing flag secure so the blue layer should be screenshot too.
    args.captureArgs.captureSecureLayers = true;
    ScreenCapture::captureLayers(&mCapture, args);
    mCapture->expectColor(Rect(0, 0, 30, 30), Color::BLUE);
    mCapture->expectColor(Rect(30, 30, 60, 60), Color::RED);
}

TEST_F(ScreenCaptureTest, ScreenshotProtectedBuffer) {
    const uint32_t bufferWidth = 60;
    const uint32_t bufferHeight = 60;

    sp<SurfaceControl> layer =
            createLayer(String8("Colored surface"), bufferWidth, bufferHeight,
                        ISurfaceComposerClient::eFXSurfaceBufferState, mRootSurfaceControl.get());

    Transaction().show(layer).setLayer(layer, INT32_MAX).apply(true);

    sp<Surface> surface = layer->getSurface();
    ASSERT_TRUE(surface != nullptr);
    sp<ANativeWindow> anw(surface);

    ASSERT_EQ(NO_ERROR, native_window_api_connect(anw.get(), NATIVE_WINDOW_API_CPU));
    ASSERT_EQ(NO_ERROR, native_window_set_usage(anw.get(), GRALLOC_USAGE_PROTECTED));

    int fenceFd;
    ANativeWindowBuffer* buf = nullptr;

    // End test if device does not support USAGE_PROTECTED
    // b/309965549 This check does not exit the test when running on AVDs
    status_t err = anw->dequeueBuffer(anw.get(), &buf, &fenceFd);
    if (err) {
        return;
    }
    anw->queueBuffer(anw.get(), buf, fenceFd);

    // USAGE_PROTECTED buffer is read as a black screen
    ScreenCaptureResults captureResults;
    ASSERT_EQ(NO_ERROR, ScreenCapture::captureLayers(mCaptureArgs, captureResults));

    ScreenCapture sc(captureResults.buffer, captureResults.capturedHdrLayers);
    sc.expectColor(Rect(0, 0, bufferWidth, bufferHeight), Color::BLACK);

    // Reading color data will expectedly result in crash, only check usage bit
    // b/309965549 Checking that the usage bit is protected does not work for
    // devices that do not support usage protected.
    mCaptureArgs.captureArgs.allowProtected = true;
    ASSERT_EQ(NO_ERROR, ScreenCapture::captureLayers(mCaptureArgs, captureResults));
    // ASSERT_EQ(GRALLOC_USAGE_PROTECTED, GRALLOC_USAGE_PROTECTED &
    // captureResults.buffer->getUsage());
}

TEST_F(ScreenCaptureTest, CaptureLayer) {
    sp<SurfaceControl> layer;
    ASSERT_NO_FATAL_FAILURE(layer = createLayer("test layer", 0, 0,
                                                ISurfaceComposerClient::eFXSurfaceEffect,
                                                mRootSurfaceControl.get()));

    const Color layerColor = Color::RED;
    const Rect bounds = Rect(10, 10, 40, 40);

    Transaction()
            .show(layer)
            .hide(mFGSurfaceControl)
            .setLayer(layer, INT32_MAX)
            .setColor(layer, {layerColor.r / 255, layerColor.g / 255, layerColor.b / 255})
            .setCrop(layer, bounds)
            .apply();

    {
        ScreenCapture::captureLayers(&mCapture, mCaptureArgs);
        mCapture->expectColor(bounds, layerColor);
        mCapture->expectBorder(bounds, {63, 63, 195, 255});
    }

    Transaction()
            .setFlags(layer, layer_state_t::eLayerSkipScreenshot,
                      layer_state_t::eLayerSkipScreenshot)
            .apply();

    {
        // Can't screenshot test layer since it now has flag
        // eLayerSkipScreenshot
        ScreenCapture::captureLayers(&mCapture, mCaptureArgs);
        mCapture->expectColor(bounds, {63, 63, 195, 255});
        mCapture->expectBorder(bounds, {63, 63, 195, 255});
    }
}

TEST_F(ScreenCaptureTest, CaptureLayerChild) {
    sp<SurfaceControl> layer;
    sp<SurfaceControl> childLayer;
    ASSERT_NO_FATAL_FAILURE(layer = createLayer("test layer", 0, 0,
                                                ISurfaceComposerClient::eFXSurfaceEffect,
                                                mRootSurfaceControl.get()));
    ASSERT_NO_FATAL_FAILURE(childLayer = createLayer("test layer", 0, 0,
                                                     ISurfaceComposerClient::eFXSurfaceEffect,
                                                     layer.get()));

    const Color layerColor = Color::RED;
    const Color childColor = Color::BLUE;
    const Rect bounds = Rect(10, 10, 40, 40);
    const Rect childBounds = Rect(20, 20, 30, 30);

    Transaction()
            .show(layer)
            .show(childLayer)
            .hide(mFGSurfaceControl)
            .setLayer(layer, INT32_MAX)
            .setColor(layer, {layerColor.r / 255, layerColor.g / 255, layerColor.b / 255})
            .setColor(childLayer, {childColor.r / 255, childColor.g / 255, childColor.b / 255})
            .setCrop(layer, bounds)
            .setCrop(childLayer, childBounds)
            .apply();

    {
        ScreenCapture::captureLayers(&mCapture, mCaptureArgs);
        mCapture->expectColor(childBounds, childColor);
        mCapture->expectBorder(childBounds, layerColor);
        mCapture->expectBorder(bounds, {63, 63, 195, 255});
    }

    Transaction()
            .setFlags(layer, layer_state_t::eLayerSkipScreenshot,
                      layer_state_t::eLayerSkipScreenshot)
            .apply();

    {
        // Can't screenshot child layer since the parent has the flag
        // eLayerSkipScreenshot
        ScreenCapture::captureLayers(&mCapture, mCaptureArgs);
        mCapture->expectColor(childBounds, {63, 63, 195, 255});
        mCapture->expectBorder(childBounds, {63, 63, 195, 255});
        mCapture->expectBorder(bounds, {63, 63, 195, 255});
    }
}

TEST_F(ScreenCaptureTest, CaptureLayerWithUid) {
    uid_t fakeUid = 12345;

    sp<SurfaceControl> layer;
    ASSERT_NO_FATAL_FAILURE(layer = createLayer("test layer", 32, 32,
                                                ISurfaceComposerClient::eFXSurfaceBufferQueue,
                                                mBGSurfaceControl.get()));
    ASSERT_NO_FATAL_FAILURE(fillBufferQueueLayerColor(layer, Color::RED, 32, 32));

    Transaction().show(layer).setLayer(layer, INT32_MAX).apply();

    LayerCaptureArgs captureArgs;
    captureArgs.layerHandle = mBGSurfaceControl->getHandle();
    captureArgs.childrenOnly = false;

    // Make sure red layer with the background layer is screenshot.
    ScreenCapture::captureLayers(&mCapture, captureArgs);
    mCapture->expectColor(Rect(0, 0, 32, 32), Color::RED);
    mCapture->expectBorder(Rect(0, 0, 32, 32), {63, 63, 195, 255});

    // From non system uid, can't request screenshot without a specified uid.
    std::unique_ptr<UIDFaker> uidFaker = std::make_unique<UIDFaker>(fakeUid);

    ASSERT_EQ(PERMISSION_DENIED, ScreenCapture::captureLayers(captureArgs, mCaptureResults));

    // Make screenshot request with current uid set. No layers were created with the current
    // uid so screenshot will be black.
    captureArgs.captureArgs.uid = fakeUid;
    ScreenCapture::captureLayers(&mCapture, captureArgs);
    mCapture->expectColor(Rect(0, 0, 32, 32), Color::TRANSPARENT);
    mCapture->expectBorder(Rect(0, 0, 32, 32), Color::TRANSPARENT);

    sp<SurfaceControl> layerWithFakeUid;
    // Create a new layer with the current uid
    ASSERT_NO_FATAL_FAILURE(layerWithFakeUid =
                                    createLayer("new test layer", 32, 32,
                                                ISurfaceComposerClient::eFXSurfaceBufferQueue,
                                                mBGSurfaceControl.get()));
    ASSERT_NO_FATAL_FAILURE(fillBufferQueueLayerColor(layerWithFakeUid, Color::GREEN, 32, 32));
    Transaction()
            .show(layerWithFakeUid)
            .setLayer(layerWithFakeUid, INT32_MAX)
            .setPosition(layerWithFakeUid, 128, 128)
            // reparent a layer that was created with a different uid to the new layer.
            .reparent(layer, layerWithFakeUid)
            .apply();

    // Screenshot from the fakeUid caller with the uid requested allows the layer
    // with that uid to be screenshotted. The child layer is skipped since it was created
    // from a different uid.
    ScreenCapture::captureLayers(&mCapture, captureArgs);
    mCapture->expectColor(Rect(128, 128, 160, 160), Color::GREEN);
    mCapture->expectBorder(Rect(128, 128, 160, 160), Color::TRANSPARENT);

    // Clear fake calling uid so it's back to system.
    uidFaker = nullptr;
    // Screenshot from the test caller with the uid requested allows the layer
    // with that uid to be screenshotted. The child layer is skipped since it was created
    // from a different uid.
    ScreenCapture::captureLayers(&mCapture, captureArgs);
    mCapture->expectColor(Rect(128, 128, 160, 160), Color::GREEN);
    mCapture->expectBorder(Rect(128, 128, 160, 160), Color::TRANSPARENT);

    // Screenshot from the fakeUid caller with no uid requested allows everything to be screenshot.
    captureArgs.captureArgs.uid = -1;
    ScreenCapture::captureLayers(&mCapture, captureArgs);
    mCapture->expectColor(Rect(128, 128, 160, 160), Color::RED);
    mCapture->expectBorder(Rect(128, 128, 160, 160), {63, 63, 195, 255});
}

TEST_F(ScreenCaptureTest, CaptureWithGrayscale) {
    sp<SurfaceControl> layer;
    ASSERT_NO_FATAL_FAILURE(layer = createLayer("test layer", 32, 32,
                                                ISurfaceComposerClient::eFXSurfaceBufferState,
                                                mBGSurfaceControl.get()));
    ASSERT_NO_FATAL_FAILURE(fillBufferLayerColor(layer, Color::RED, 32, 32));
    Transaction().show(layer).setLayer(layer, INT32_MAX).apply();

    LayerCaptureArgs captureArgs;
    captureArgs.layerHandle = layer->getHandle();

    ScreenCapture::captureLayers(&mCapture, captureArgs);
    mCapture->expectColor(Rect(0, 0, 32, 32), Color::RED);

    captureArgs.captureArgs.grayscale = true;

    const uint8_t tolerance = 1;

    // Values based on SurfaceFlinger::calculateColorMatrix
    float3 luminance{0.213f, 0.715f, 0.072f};

    ScreenCapture::captureLayers(&mCapture, captureArgs);

    uint8_t expectedColor = luminance.r * 255;
    mCapture->expectColor(Rect(0, 0, 32, 32),
                          Color{expectedColor, expectedColor, expectedColor, 255}, tolerance);

    ASSERT_NO_FATAL_FAILURE(fillBufferLayerColor(layer, Color::BLUE, 32, 32));
    ScreenCapture::captureLayers(&mCapture, captureArgs);

    expectedColor = luminance.b * 255;
    mCapture->expectColor(Rect(0, 0, 32, 32),
                          Color{expectedColor, expectedColor, expectedColor, 255}, tolerance);
}

TEST_F(ScreenCaptureTest, CaptureOffscreen) {
    sp<SurfaceControl> layer;
    ASSERT_NO_FATAL_FAILURE(layer = createLayer("test layer", 32, 32,
                                                ISurfaceComposerClient::eFXSurfaceBufferState,
                                                mBGSurfaceControl.get()));
    ASSERT_NO_FATAL_FAILURE(fillBufferLayerColor(layer, Color::RED, 32, 32));

    Transaction().show(layer).hide(mFGSurfaceControl).reparent(layer, nullptr).apply();

    {
        // Validate that the red layer is not on screen
        ScreenCapture::captureLayers(&mCapture, mCaptureArgs);
        mCapture->expectColor(Rect(0, 0, mDisplayWidth, mDisplayHeight), {63, 63, 195, 255});
    }

    LayerCaptureArgs captureArgs;
    captureArgs.layerHandle = layer->getHandle();

    ScreenCapture::captureLayers(&mCapture, captureArgs);
    mCapture->expectSize(32, 32);
    mCapture->expectColor(Rect(0, 0, 32, 32), Color::RED);
}

TEST_F(ScreenCaptureTest, CaptureNonHdrLayer) {
    sp<SurfaceControl> layer;
    ASSERT_NO_FATAL_FAILURE(layer = createLayer("test layer", 32, 32,
                                                ISurfaceComposerClient::eFXSurfaceBufferState,
                                                mBGSurfaceControl.get()));
    ASSERT_NO_FATAL_FAILURE(fillBufferLayerColor(layer, Color::BLACK, 32, 32));
    Transaction()
            .show(layer)
            .setLayer(layer, INT32_MAX)
            .setDataspace(layer, ui::Dataspace::V0_SRGB)
            .apply();

    LayerCaptureArgs captureArgs;
    captureArgs.layerHandle = layer->getHandle();

    ScreenCapture::captureLayers(&mCapture, captureArgs);
    mCapture->expectColor(Rect(0, 0, 32, 32), Color::BLACK);
    ASSERT_FALSE(mCapture->capturedHdrLayers());
}

TEST_F(ScreenCaptureTest, CaptureHdrLayer) {
    sp<SurfaceControl> layer;
    ASSERT_NO_FATAL_FAILURE(layer = createLayer("test layer", 32, 32,
                                                ISurfaceComposerClient::eFXSurfaceBufferState,
                                                mBGSurfaceControl.get()));
    ASSERT_NO_FATAL_FAILURE(fillBufferLayerColor(layer, Color::BLACK, 32, 32));
    Transaction()
            .show(layer)
            .setLayer(layer, INT32_MAX)
            .setDataspace(layer, ui::Dataspace::BT2020_ITU_PQ)
            .apply();

    LayerCaptureArgs captureArgs;
    captureArgs.layerHandle = layer->getHandle();

    ScreenCapture::captureLayers(&mCapture, captureArgs);
    mCapture->expectColor(Rect(0, 0, 32, 32), Color::BLACK);
    ASSERT_TRUE(mCapture->capturedHdrLayers());
}

TEST_F(ScreenCaptureTest, captureOffscreenNullSnapshot) {
    sp<SurfaceControl> layer;
    ASSERT_NO_FATAL_FAILURE(layer = createLayer("test layer", 32, 32,
                                                ISurfaceComposerClient::eFXSurfaceBufferState,
                                                mBGSurfaceControl.get()));

    // A mirrored layer will not have a snapshot. Testing an offscreen mirrored layer
    // ensures that the screenshot path handles cases where snapshots are null.
    sp<SurfaceControl> mirroredLayer;
    ASSERT_NO_FATAL_FAILURE(mirroredLayer = mirrorSurface(layer.get()));

    LayerCaptureArgs captureArgs;
    captureArgs.layerHandle = mirroredLayer->getHandle();
    captureArgs.captureArgs.sourceCrop = gui::aidl_utils::toARect(1, 1);

    // Screenshot path should only use the children of the layer hierarchy so
    // that it will not create a new snapshot. A snapshot would otherwise be
    // created to pass on the properties of the parent, which is not needed
    // for the purposes of this test since we explicitly want a null snapshot.
    captureArgs.childrenOnly = true;
    ScreenCapture::captureLayers(&mCapture, captureArgs);
}

// In the following tests we verify successful skipping of a parent layer,
// so we use the same verification logic and only change how we mutate
// the parent layer to verify that various properties are ignored.
class ScreenCaptureChildOnlyTest : public ScreenCaptureTest {
public:
    void SetUp() override {
        ScreenCaptureTest::SetUp();

        mChild = createSurface(mClient, "Child surface", 10, 10, PIXEL_FORMAT_RGBA_8888, 0,
                               mFGSurfaceControl.get());
        TransactionUtils::fillSurfaceRGBA8(mChild, 200, 200, 200);

        SurfaceComposerClient::Transaction().show(mChild).apply(true);
    }

    void verify(std::function<void()> verifyStartingState) {
        // Verify starting state before a screenshot is taken.
        verifyStartingState();

        // Verify child layer does not inherit any of the properties of its
        // parent when its screenshot is captured.
        LayerCaptureArgs captureArgs;
        captureArgs.layerHandle = mFGSurfaceControl->getHandle();
        captureArgs.childrenOnly = true;
        ScreenCapture::captureLayers(&mCapture, captureArgs);
        mCapture->checkPixel(10, 10, 0, 0, 0);
        mCapture->expectChildColor(0, 0);

        // Verify all assumptions are still true after the screenshot is taken.
        verifyStartingState();
    }

    std::unique_ptr<ScreenCapture> mCapture;
    sp<SurfaceControl> mChild;
};

// Regression test b/76099859
TEST_F(ScreenCaptureChildOnlyTest, CaptureLayerIgnoresParentVisibility) {
    SurfaceComposerClient::Transaction().hide(mFGSurfaceControl).apply(true);

    // Even though the parent is hidden we should still capture the child.

    // Before and after reparenting, verify child is properly hidden
    // when rendering full-screen.
    verify([&] { screenshot()->expectBGColor(64, 64); });
}

TEST_F(ScreenCaptureChildOnlyTest, CaptureLayerIgnoresParentCrop) {
    SurfaceComposerClient::Transaction().setCrop(mFGSurfaceControl, Rect(0, 0, 1, 1)).apply(true);

    // Even though the parent is cropped out we should still capture the child.

    // Before and after reparenting, verify child is cropped by parent.
    verify([&] { screenshot()->expectBGColor(65, 65); });
}

// Regression test b/124372894
TEST_F(ScreenCaptureChildOnlyTest, CaptureLayerIgnoresTransform) {
    SurfaceComposerClient::Transaction().setMatrix(mFGSurfaceControl, 2, 0, 0, 2).apply(true);

    // We should not inherit the parent scaling.

    // Before and after reparenting, verify child is properly scaled.
    verify([&] { screenshot()->expectChildColor(80, 80); });
}

} // namespace android

// TODO(b/129481165): remove the #pragma below and fix conversion issues
#pragma clang diagnostic pop // ignored "-Wconversion"
