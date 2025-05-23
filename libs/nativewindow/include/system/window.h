/*
 * Copyright (C) 2011 The Android Open Source Project
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

/*************************************************************************************************
 *
 * IMPORTANT:
 *
 * There is an old copy of this file in system/core/include/system/window.h, which exists only
 * for backward source compatibility.
 * But there are binaries out there as well, so this version of window.h must stay binary
 * backward compatible with the one found in system/core.
 *
 *
 * Source compatibility is also required for now, because this is how we're handling the
 * transition from system/core/include (global include path) to nativewindow/include.
 *
 *************************************************************************************************/

#pragma once

#include <cutils/native_handle.h>
#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <sys/cdefs.h>
#include <system/graphics.h>
#include <unistd.h>

#include <vndk/hardware_buffer.h>

// system/window.h is a superset of the vndk and apex apis
#include <apex/window.h>
#include <vndk/window.h>


#ifndef __UNUSED
#define __UNUSED __attribute__((__unused__))
#endif
#ifndef __deprecated
#define __deprecated __attribute__((__deprecated__))
#endif

__BEGIN_DECLS

/*****************************************************************************/

#define ANDROID_NATIVE_WINDOW_MAGIC     ANDROID_NATIVE_MAKE_CONSTANT('_','w','n','d')

// ---------------------------------------------------------------------------

/* attributes queriable with query() */
enum {
    NATIVE_WINDOW_WIDTH = 0,
    NATIVE_WINDOW_HEIGHT = 1,
    NATIVE_WINDOW_FORMAT = 2,

    /* see ANativeWindowQuery in vndk/window.h */
    NATIVE_WINDOW_MIN_UNDEQUEUED_BUFFERS = ANATIVEWINDOW_QUERY_MIN_UNDEQUEUED_BUFFERS,

    /* Check whether queueBuffer operations on the ANativeWindow send the buffer
     * to the window compositor.  The query sets the returned 'value' argument
     * to 1 if the ANativeWindow DOES send queued buffers directly to the window
     * compositor and 0 if the buffers do not go directly to the window
     * compositor.
     *
     * This can be used to determine whether protected buffer content should be
     * sent to the ANativeWindow.  Note, however, that a result of 1 does NOT
     * indicate that queued buffers will be protected from applications or users
     * capturing their contents.  If that behavior is desired then some other
     * mechanism (e.g. the GRALLOC_USAGE_PROTECTED flag) should be used in
     * conjunction with this query.
     */
    NATIVE_WINDOW_QUEUES_TO_WINDOW_COMPOSER = 4,

    /* Get the concrete type of a ANativeWindow.  See below for the list of
     * possible return values.
     *
     * This query should not be used outside the Android framework and will
     * likely be removed in the near future.
     */
    NATIVE_WINDOW_CONCRETE_TYPE = 5,

    /*
     * Default width and height of ANativeWindow buffers, these are the
     * dimensions of the window buffers irrespective of the
     * NATIVE_WINDOW_SET_BUFFERS_DIMENSIONS call and match the native window
     * size unless overridden by NATIVE_WINDOW_SET_BUFFERS_USER_DIMENSIONS.
     */
    NATIVE_WINDOW_DEFAULT_WIDTH = ANATIVEWINDOW_QUERY_DEFAULT_WIDTH,
    NATIVE_WINDOW_DEFAULT_HEIGHT = ANATIVEWINDOW_QUERY_DEFAULT_HEIGHT,

    /* see ANativeWindowQuery in vndk/window.h */
    NATIVE_WINDOW_TRANSFORM_HINT = ANATIVEWINDOW_QUERY_TRANSFORM_HINT,

    /*
     * Boolean that indicates whether the consumer is running more than
     * one buffer behind the producer.
     */
    NATIVE_WINDOW_CONSUMER_RUNNING_BEHIND = 9,

    /*
     * The consumer gralloc usage bits currently set by the consumer.
     * The values are defined in hardware/libhardware/include/gralloc.h.
     */
    NATIVE_WINDOW_CONSUMER_USAGE_BITS = 10, /* deprecated */

    /**
     * Transformation that will by applied to buffers by the hwcomposer.
     * This must not be set or checked by producer endpoints, and will
     * disable the transform hint set in SurfaceFlinger (see
     * NATIVE_WINDOW_TRANSFORM_HINT).
     *
     * INTENDED USE:
     * Temporary - Please do not use this.  This is intended only to be used
     * by the camera's LEGACY mode.
     *
     * In situations where a SurfaceFlinger client wishes to set a transform
     * that is not visible to the producer, and will always be applied in the
     * hardware composer, the client can set this flag with
     * native_window_set_buffers_sticky_transform.  This can be used to rotate
     * and flip buffers consumed by hardware composer without actually changing
     * the aspect ratio of the buffers produced.
     */
    NATIVE_WINDOW_STICKY_TRANSFORM = 11,

    /**
     * The default data space for the buffers as set by the consumer.
     * The values are defined in graphics.h.
     */
    NATIVE_WINDOW_DEFAULT_DATASPACE = 12,

    /* see ANativeWindowQuery in vndk/window.h */
    NATIVE_WINDOW_BUFFER_AGE = ANATIVEWINDOW_QUERY_BUFFER_AGE,

    /*
     * Returns the duration of the last dequeueBuffer call in microseconds
     * Deprecated: please use NATIVE_WINDOW_GET_LAST_DEQUEUE_DURATION in
     * perform() instead, which supports nanosecond precision.
     */
    NATIVE_WINDOW_LAST_DEQUEUE_DURATION = 14,

    /*
     * Returns the duration of the last queueBuffer call in microseconds
     * Deprecated: please use NATIVE_WINDOW_GET_LAST_QUEUE_DURATION in
     * perform() instead, which supports nanosecond precision.
     */
    NATIVE_WINDOW_LAST_QUEUE_DURATION = 15,

    /*
     * Returns the number of image layers that the ANativeWindow buffer
     * contains. By default this is 1, unless a buffer is explicitly allocated
     * to contain multiple layers.
     */
    NATIVE_WINDOW_LAYER_COUNT = 16,

    /*
     * Returns 1 if the native window is valid, 0 otherwise. native window is valid
     * if it is safe (i.e. no crash will occur) to call any method on it.
     */
    NATIVE_WINDOW_IS_VALID = 17,

    /*
     * Returns 1 if NATIVE_WINDOW_GET_FRAME_TIMESTAMPS will return display
     * present info, 0 if it won't.
     */
    NATIVE_WINDOW_FRAME_TIMESTAMPS_SUPPORTS_PRESENT = 18,

    /*
     * The consumer end is capable of handling protected buffers, i.e. buffer
     * with GRALLOC_USAGE_PROTECTED usage bits on.
     */
    NATIVE_WINDOW_CONSUMER_IS_PROTECTED = 19,

    /*
     * Returns data space for the buffers.
     */
    NATIVE_WINDOW_DATASPACE = 20,

    /*
     * Returns maxBufferCount set by BufferQueueConsumer
     */
    NATIVE_WINDOW_MAX_BUFFER_COUNT = 21,
};

/* Valid operations for the (*perform)() hook.
 *
 * Values marked as 'deprecated' are supported, but have been superceded by
 * other functionality.
 *
 * Values marked as 'private' should be considered private to the framework.
 * HAL implementation code with access to an ANativeWindow should not use these,
 * as it may not interact properly with the framework's use of the
 * ANativeWindow.
 */
enum {
    // clang-format off
    NATIVE_WINDOW_SET_USAGE                       =  ANATIVEWINDOW_PERFORM_SET_USAGE,   /* deprecated */
    NATIVE_WINDOW_CONNECT                         =  1,   /* deprecated */
    NATIVE_WINDOW_DISCONNECT                      =  2,   /* deprecated */
    NATIVE_WINDOW_SET_CROP                        =  3,   /* private */
    NATIVE_WINDOW_SET_BUFFER_COUNT                =  4,
    NATIVE_WINDOW_SET_BUFFERS_GEOMETRY            =  ANATIVEWINDOW_PERFORM_SET_BUFFERS_GEOMETRY,   /* deprecated */
    NATIVE_WINDOW_SET_BUFFERS_TRANSFORM           =  6,
    NATIVE_WINDOW_SET_BUFFERS_TIMESTAMP           =  7,
    NATIVE_WINDOW_SET_BUFFERS_DIMENSIONS          =  8,
    NATIVE_WINDOW_SET_BUFFERS_FORMAT              =  ANATIVEWINDOW_PERFORM_SET_BUFFERS_FORMAT,
    NATIVE_WINDOW_SET_SCALING_MODE                = 10,   /* private */
    NATIVE_WINDOW_LOCK                            = 11,   /* private */
    NATIVE_WINDOW_UNLOCK_AND_POST                 = 12,   /* private */
    NATIVE_WINDOW_API_CONNECT                     = 13,   /* private */
    NATIVE_WINDOW_API_DISCONNECT                  = 14,   /* private */
    NATIVE_WINDOW_SET_BUFFERS_USER_DIMENSIONS     = 15,   /* private */
    NATIVE_WINDOW_SET_POST_TRANSFORM_CROP         = 16,   /* deprecated, unimplemented */
    NATIVE_WINDOW_SET_BUFFERS_STICKY_TRANSFORM    = 17,   /* private */
    NATIVE_WINDOW_SET_SIDEBAND_STREAM             = 18,
    NATIVE_WINDOW_SET_BUFFERS_DATASPACE           = 19,
    NATIVE_WINDOW_SET_SURFACE_DAMAGE              = 20,   /* private */
    NATIVE_WINDOW_SET_SHARED_BUFFER_MODE          = 21,
    NATIVE_WINDOW_SET_AUTO_REFRESH                = 22,
    NATIVE_WINDOW_GET_REFRESH_CYCLE_DURATION      = 23,
    NATIVE_WINDOW_GET_NEXT_FRAME_ID               = 24,
    NATIVE_WINDOW_ENABLE_FRAME_TIMESTAMPS         = 25,
    NATIVE_WINDOW_GET_COMPOSITOR_TIMING           = 26,
    NATIVE_WINDOW_GET_FRAME_TIMESTAMPS            = 27,
    /* 28, removed: NATIVE_WINDOW_GET_WIDE_COLOR_SUPPORT */
    /* 29, removed: NATIVE_WINDOW_GET_HDR_SUPPORT */
    NATIVE_WINDOW_SET_USAGE64                     = ANATIVEWINDOW_PERFORM_SET_USAGE64,
    NATIVE_WINDOW_GET_CONSUMER_USAGE64            = 31,
    NATIVE_WINDOW_SET_BUFFERS_SMPTE2086_METADATA  = 32,
    NATIVE_WINDOW_SET_BUFFERS_CTA861_3_METADATA   = 33,
    NATIVE_WINDOW_SET_BUFFERS_HDR10_PLUS_METADATA = 34,
    NATIVE_WINDOW_SET_AUTO_PREROTATION            = 35,
    NATIVE_WINDOW_GET_LAST_DEQUEUE_START          = 36,    /* private */
    NATIVE_WINDOW_SET_DEQUEUE_TIMEOUT             = 37,    /* private */
    NATIVE_WINDOW_GET_LAST_DEQUEUE_DURATION       = 38,    /* private */
    NATIVE_WINDOW_GET_LAST_QUEUE_DURATION         = 39,    /* private */
    NATIVE_WINDOW_SET_FRAME_RATE                  = 40,
    NATIVE_WINDOW_SET_CANCEL_INTERCEPTOR          = 41,    /* private */
    NATIVE_WINDOW_SET_DEQUEUE_INTERCEPTOR         = 42,    /* private */
    NATIVE_WINDOW_SET_PERFORM_INTERCEPTOR         = 43,    /* private */
    NATIVE_WINDOW_SET_QUEUE_INTERCEPTOR           = 44,    /* private */
    NATIVE_WINDOW_ALLOCATE_BUFFERS                = 45,    /* private */
    NATIVE_WINDOW_GET_LAST_QUEUED_BUFFER          = 46,    /* private */
    NATIVE_WINDOW_SET_QUERY_INTERCEPTOR           = 47,    /* private */
    NATIVE_WINDOW_SET_FRAME_TIMELINE_INFO         = 48,    /* private */
    NATIVE_WINDOW_GET_LAST_QUEUED_BUFFER2         = 49,    /* private */
    NATIVE_WINDOW_SET_BUFFERS_ADDITIONAL_OPTIONS  = 50,
    // clang-format on
};

/* parameter for NATIVE_WINDOW_[API_][DIS]CONNECT */
enum {
    /* Buffers will be queued by EGL via eglSwapBuffers after being filled using
     * OpenGL ES.
     */
    NATIVE_WINDOW_API_EGL = 1,

    /* Buffers will be queued after being filled using the CPU
     */
    NATIVE_WINDOW_API_CPU = 2,

    /* Buffers will be queued by Stagefright after being filled by a video
     * decoder.  The video decoder can either be a software or hardware decoder.
     */
    NATIVE_WINDOW_API_MEDIA = 3,

    /* Buffers will be queued by the the camera HAL.
     */
    NATIVE_WINDOW_API_CAMERA = 4,
};

/* parameter for NATIVE_WINDOW_SET_BUFFERS_TRANSFORM */
enum {
    /* flip source image horizontally */
    NATIVE_WINDOW_TRANSFORM_FLIP_H = HAL_TRANSFORM_FLIP_H ,
    /* flip source image vertically */
    NATIVE_WINDOW_TRANSFORM_FLIP_V = HAL_TRANSFORM_FLIP_V,
    /* rotate source image 90 degrees clock-wise, and is applied after TRANSFORM_FLIP_{H|V} */
    NATIVE_WINDOW_TRANSFORM_ROT_90 = HAL_TRANSFORM_ROT_90,
    /* rotate source image 180 degrees */
    NATIVE_WINDOW_TRANSFORM_ROT_180 = HAL_TRANSFORM_ROT_180,
    /* rotate source image 270 degrees clock-wise */
    NATIVE_WINDOW_TRANSFORM_ROT_270 = HAL_TRANSFORM_ROT_270,
    /* transforms source by the inverse transform of the screen it is displayed onto. This
     * transform is applied last */
    NATIVE_WINDOW_TRANSFORM_INVERSE_DISPLAY = 0x08
};

/* parameter for NATIVE_WINDOW_SET_SCALING_MODE
 * keep in sync with Surface.java in frameworks/base */
enum {
    /* the window content is not updated (frozen) until a buffer of
     * the window size is received (enqueued)
     */
    NATIVE_WINDOW_SCALING_MODE_FREEZE           = 0,
    /* the buffer is scaled in both dimensions to match the window size */
    NATIVE_WINDOW_SCALING_MODE_SCALE_TO_WINDOW  = 1,
    /* the buffer is scaled uniformly such that the smaller dimension
     * of the buffer matches the window size (cropping in the process)
     */
    NATIVE_WINDOW_SCALING_MODE_SCALE_CROP       = 2,
    /* the window is clipped to the size of the buffer's crop rectangle; pixels
     * outside the crop rectangle are treated as if they are completely
     * transparent.
     */
    NATIVE_WINDOW_SCALING_MODE_NO_SCALE_CROP    = 3,
};

/* values returned by the NATIVE_WINDOW_CONCRETE_TYPE query */
enum {
    NATIVE_WINDOW_FRAMEBUFFER               = 0, /* FramebufferNativeWindow */
    NATIVE_WINDOW_SURFACE                   = 1, /* Surface */
};

/* parameter for NATIVE_WINDOW_SET_BUFFERS_TIMESTAMP
 *
 * Special timestamp value to indicate that timestamps should be auto-generated
 * by the native window when queueBuffer is called.  This is equal to INT64_MIN,
 * defined directly to avoid problems with C99/C++ inclusion of stdint.h.
 */
static const int64_t NATIVE_WINDOW_TIMESTAMP_AUTO = (-9223372036854775807LL-1);

/* parameter for NATIVE_WINDOW_GET_FRAME_TIMESTAMPS
 *
 * Special timestamp value to indicate the timestamps aren't yet known or
 * that they are invalid.
 */
static const int64_t NATIVE_WINDOW_TIMESTAMP_PENDING = -2;
static const int64_t NATIVE_WINDOW_TIMESTAMP_INVALID = -1;

struct ANativeWindow
{
#ifdef __cplusplus
    ANativeWindow()
        : flags(0), minSwapInterval(0), maxSwapInterval(0), xdpi(0), ydpi(0)
    {
        common.magic = ANDROID_NATIVE_WINDOW_MAGIC;
        common.version = sizeof(ANativeWindow);
        memset(common.reserved, 0, sizeof(common.reserved));
    }

    /* Implement the methods that sp<ANativeWindow> expects so that it
       can be used to automatically refcount ANativeWindow's. */
    void incStrong(const void* /*id*/) const {
        common.incRef(const_cast<android_native_base_t*>(&common));
    }
    void decStrong(const void* /*id*/) const {
        common.decRef(const_cast<android_native_base_t*>(&common));
    }
#endif

    struct android_native_base_t common;

    /* flags describing some attributes of this surface or its updater */
    const uint32_t flags;

    /* min swap interval supported by this updated */
    const int   minSwapInterval;

    /* max swap interval supported by this updated */
    const int   maxSwapInterval;

    /* horizontal and vertical resolution in DPI */
    const float xdpi;
    const float ydpi;

    /* Some storage reserved for the OEM's driver. */
    intptr_t    oem[4];

    /*
     * Set the swap interval for this surface.
     *
     * Returns 0 on success or -errno on error.
     */
    int     (*setSwapInterval)(struct ANativeWindow* window,
                int interval);

    /*
     * Hook called by EGL to acquire a buffer. After this call, the buffer
     * is not locked, so its content cannot be modified. This call may block if
     * no buffers are available.
     *
     * The window holds a reference to the buffer between dequeueBuffer and
     * either queueBuffer or cancelBuffer, so clients only need their own
     * reference if they might use the buffer after queueing or canceling it.
     * Holding a reference to a buffer after queueing or canceling it is only
     * allowed if a specific buffer count has been set.
     *
     * Returns 0 on success or -errno on error.
     *
     * XXX: This function is deprecated.  It will continue to work for some
     * time for binary compatibility, but the new dequeueBuffer function that
     * outputs a fence file descriptor should be used in its place.
     */
    int     (*dequeueBuffer_DEPRECATED)(struct ANativeWindow* window,
                struct ANativeWindowBuffer** buffer);

    /*
     * hook called by EGL to lock a buffer. This MUST be called before modifying
     * the content of a buffer. The buffer must have been acquired with
     * dequeueBuffer first.
     *
     * Returns 0 on success or -errno on error.
     *
     * XXX: This function is deprecated.  It will continue to work for some
     * time for binary compatibility, but it is essentially a no-op, and calls
     * to it should be removed.
     */
    int     (*lockBuffer_DEPRECATED)(struct ANativeWindow* window,
                struct ANativeWindowBuffer* buffer);

    /*
     * Hook called by EGL when modifications to the render buffer are done.
     * This unlocks and post the buffer.
     *
     * The window holds a reference to the buffer between dequeueBuffer and
     * either queueBuffer or cancelBuffer, so clients only need their own
     * reference if they might use the buffer after queueing or canceling it.
     * Holding a reference to a buffer after queueing or canceling it is only
     * allowed if a specific buffer count has been set.
     *
     * Buffers MUST be queued in the same order than they were dequeued.
     *
     * Returns 0 on success or -errno on error.
     *
     * XXX: This function is deprecated.  It will continue to work for some
     * time for binary compatibility, but the new queueBuffer function that
     * takes a fence file descriptor should be used in its place (pass a value
     * of -1 for the fence file descriptor if there is no valid one to pass).
     */
    int     (*queueBuffer_DEPRECATED)(struct ANativeWindow* window,
                struct ANativeWindowBuffer* buffer);

    /*
     * hook used to retrieve information about the native window.
     *
     * Returns 0 on success or -errno on error.
     */
    int     (*query)(const struct ANativeWindow* window,
                int what, int* value);

    /*
     * hook used to perform various operations on the surface.
     * (*perform)() is a generic mechanism to add functionality to
     * ANativeWindow while keeping backward binary compatibility.
     *
     * DO NOT CALL THIS HOOK DIRECTLY.  Instead, use the helper functions
     * defined below.
     *
     * (*perform)() returns -ENOENT if the 'what' parameter is not supported
     * by the surface's implementation.
     *
     * See above for a list of valid operations, such as
     * NATIVE_WINDOW_SET_USAGE or NATIVE_WINDOW_CONNECT
     */
    int     (*perform)(struct ANativeWindow* window,
                int operation, ... );

    /*
     * Hook used to cancel a buffer that has been dequeued.
     * No synchronization is performed between dequeue() and cancel(), so
     * either external synchronization is needed, or these functions must be
     * called from the same thread.
     *
     * The window holds a reference to the buffer between dequeueBuffer and
     * either queueBuffer or cancelBuffer, so clients only need their own
     * reference if they might use the buffer after queueing or canceling it.
     * Holding a reference to a buffer after queueing or canceling it is only
     * allowed if a specific buffer count has been set.
     *
     * XXX: This function is deprecated.  It will continue to work for some
     * time for binary compatibility, but the new cancelBuffer function that
     * takes a fence file descriptor should be used in its place (pass a value
     * of -1 for the fence file descriptor if there is no valid one to pass).
     */
    int     (*cancelBuffer_DEPRECATED)(struct ANativeWindow* window,
                struct ANativeWindowBuffer* buffer);

    /*
     * Hook called by EGL to acquire a buffer. This call may block if no
     * buffers are available.
     *
     * The window holds a reference to the buffer between dequeueBuffer and
     * either queueBuffer or cancelBuffer, so clients only need their own
     * reference if they might use the buffer after queueing or canceling it.
     * Holding a reference to a buffer after queueing or canceling it is only
     * allowed if a specific buffer count has been set.
     *
     * The libsync fence file descriptor returned in the int pointed to by the
     * fenceFd argument will refer to the fence that must signal before the
     * dequeued buffer may be written to.  A value of -1 indicates that the
     * caller may access the buffer immediately without waiting on a fence.  If
     * a valid file descriptor is returned (i.e. any value except -1) then the
     * caller is responsible for closing the file descriptor.
     *
     * Returns 0 on success or -errno on error.
     */
    int     (*dequeueBuffer)(struct ANativeWindow* window,
                struct ANativeWindowBuffer** buffer, int* fenceFd);

    /*
     * Hook called by EGL when modifications to the render buffer are done.
     * This unlocks and post the buffer.
     *
     * The window holds a reference to the buffer between dequeueBuffer and
     * either queueBuffer or cancelBuffer, so clients only need their own
     * reference if they might use the buffer after queueing or canceling it.
     * Holding a reference to a buffer after queueing or canceling it is only
     * allowed if a specific buffer count has been set.
     *
     * The fenceFd argument specifies a libsync fence file descriptor for a
     * fence that must signal before the buffer can be accessed.  If the buffer
     * can be accessed immediately then a value of -1 should be used.  The
     * caller must not use the file descriptor after it is passed to
     * queueBuffer, and the ANativeWindow implementation is responsible for
     * closing it.
     *
     * Returns 0 on success or -errno on error.
     */
    int     (*queueBuffer)(struct ANativeWindow* window,
                struct ANativeWindowBuffer* buffer, int fenceFd);

    /*
     * Hook used to cancel a buffer that has been dequeued.
     * No synchronization is performed between dequeue() and cancel(), so
     * either external synchronization is needed, or these functions must be
     * called from the same thread.
     *
     * The window holds a reference to the buffer between dequeueBuffer and
     * either queueBuffer or cancelBuffer, so clients only need their own
     * reference if they might use the buffer after queueing or canceling it.
     * Holding a reference to a buffer after queueing or canceling it is only
     * allowed if a specific buffer count has been set.
     *
     * The fenceFd argument specifies a libsync fence file decsriptor for a
     * fence that must signal before the buffer can be accessed.  If the buffer
     * can be accessed immediately then a value of -1 should be used.
     *
     * Note that if the client has not waited on the fence that was returned
     * from dequeueBuffer, that same fence should be passed to cancelBuffer to
     * ensure that future uses of the buffer are preceded by a wait on that
     * fence.  The caller must not use the file descriptor after it is passed
     * to cancelBuffer, and the ANativeWindow implementation is responsible for
     * closing it.
     *
     * Returns 0 on success or -errno on error.
     */
    int     (*cancelBuffer)(struct ANativeWindow* window,
                struct ANativeWindowBuffer* buffer, int fenceFd);
};

 /* Backwards compatibility: use ANativeWindow (struct ANativeWindow in C).
  * android_native_window_t is deprecated.
  */
typedef struct ANativeWindow android_native_window_t __deprecated;

/*
 *  native_window_set_usage64(..., usage)
 *  Sets the intended usage flags for the next buffers
 *  acquired with (*lockBuffer)() and on.
 *
 *  Valid usage flags are defined in android/hardware_buffer.h
 *  All AHARDWAREBUFFER_USAGE_* flags can be specified as needed.
 *
 *  Calling this function will usually cause following buffers to be
 *  reallocated.
 */
static inline int native_window_set_usage(struct ANativeWindow* window, uint64_t usage) {
    return window->perform(window, NATIVE_WINDOW_SET_USAGE64, usage);
}

/* deprecated. Always returns 0. Don't call. */
static inline int native_window_connect(
        struct ANativeWindow* window __UNUSED, int api __UNUSED) __deprecated;

static inline int native_window_connect(
        struct ANativeWindow* window __UNUSED, int api __UNUSED) {
    return 0;
}

/* deprecated. Always returns 0. Don't call. */
static inline int native_window_disconnect(
        struct ANativeWindow* window __UNUSED, int api __UNUSED) __deprecated;

static inline int native_window_disconnect(
        struct ANativeWindow* window __UNUSED, int api __UNUSED) {
    return 0;
}

/*
 * native_window_set_crop(..., crop)
 * Sets which region of the next queued buffers needs to be considered.
 * Depending on the scaling mode, a buffer's crop region is scaled and/or
 * cropped to match the surface's size.  This function sets the crop in
 * pre-transformed buffer pixel coordinates.
 *
 * The specified crop region applies to all buffers queued after it is called.
 *
 * If 'crop' is NULL, subsequently queued buffers won't be cropped.
 *
 * An error is returned if for instance the crop region is invalid, out of the
 * buffer's bound or if the window is invalid.
 */
static inline int native_window_set_crop(
        struct ANativeWindow* window,
        android_native_rect_t const * crop)
{
    return window->perform(window, NATIVE_WINDOW_SET_CROP, crop);
}

/*
 * native_window_set_buffer_count(..., count)
 * Sets the number of buffers associated with this native window.
 */
static inline int native_window_set_buffer_count(
        struct ANativeWindow* window,
        size_t bufferCount)
{
    return window->perform(window, NATIVE_WINDOW_SET_BUFFER_COUNT, bufferCount);
}

/*
 * native_window_set_buffers_geometry(..., int w, int h, int format)
 * All buffers dequeued after this call will have the dimensions and format
 * specified.  A successful call to this function has the same effect as calling
 * native_window_set_buffers_size and native_window_set_buffers_format.
 *
 * XXX: This function is deprecated.  The native_window_set_buffers_dimensions
 * and native_window_set_buffers_format functions should be used instead.
 */
static inline int native_window_set_buffers_geometry(
        struct ANativeWindow* window,
        int w, int h, int format) __deprecated;

static inline int native_window_set_buffers_geometry(
        struct ANativeWindow* window,
        int w, int h, int format)
{
    return window->perform(window, NATIVE_WINDOW_SET_BUFFERS_GEOMETRY,
            w, h, format);
}

/*
 * native_window_set_buffers_dimensions(..., int w, int h)
 * All buffers dequeued after this call will have the dimensions specified.
 * In particular, all buffers will have a fixed-size, independent from the
 * native-window size. They will be scaled according to the scaling mode
 * (see native_window_set_scaling_mode) upon window composition.
 *
 * If w and h are 0, the normal behavior is restored. That is, dequeued buffers
 * following this call will be sized to match the window's size.
 *
 * Calling this function will reset the window crop to a NULL value, which
 * disables cropping of the buffers.
 */
static inline int native_window_set_buffers_dimensions(
        struct ANativeWindow* window,
        int w, int h)
{
    return window->perform(window, NATIVE_WINDOW_SET_BUFFERS_DIMENSIONS,
            w, h);
}

/*
 * native_window_set_buffers_user_dimensions(..., int w, int h)
 *
 * Sets the user buffer size for the window, which overrides the
 * window's size.  All buffers dequeued after this call will have the
 * dimensions specified unless overridden by
 * native_window_set_buffers_dimensions.  All buffers will have a
 * fixed-size, independent from the native-window size. They will be
 * scaled according to the scaling mode (see
 * native_window_set_scaling_mode) upon window composition.
 *
 * If w and h are 0, the normal behavior is restored. That is, the
 * default buffer size will match the windows's size.
 *
 * Calling this function will reset the window crop to a NULL value, which
 * disables cropping of the buffers.
 */
static inline int native_window_set_buffers_user_dimensions(
        struct ANativeWindow* window,
        int w, int h)
{
    return window->perform(window, NATIVE_WINDOW_SET_BUFFERS_USER_DIMENSIONS,
            w, h);
}

/*
 * native_window_set_buffers_format(..., int format)
 * All buffers dequeued after this call will have the format specified.
 *
 * If the specified format is 0, the default buffer format will be used.
 */
static inline int native_window_set_buffers_format(
        struct ANativeWindow* window,
        int format)
{
    return window->perform(window, NATIVE_WINDOW_SET_BUFFERS_FORMAT, format);
}

/*
 * native_window_set_buffers_data_space(..., int dataSpace)
 * All buffers queued after this call will be associated with the dataSpace
 * parameter specified.
 *
 * dataSpace specifies additional information about the buffer that's dependent
 * on the buffer format and the endpoints. For example, it can be used to convey
 * the color space of the image data in the buffer, or it can be used to
 * indicate that the buffers contain depth measurement data instead of color
 * images.  The default dataSpace is 0, HAL_DATASPACE_UNKNOWN, unless it has been
 * overridden by the consumer.
 */
static inline int native_window_set_buffers_data_space(
        struct ANativeWindow* window,
        android_dataspace_t dataSpace)
{
    return window->perform(window, NATIVE_WINDOW_SET_BUFFERS_DATASPACE,
            dataSpace);
}

/*
 * native_window_set_buffers_smpte2086_metadata(..., metadata)
 * All buffers queued after this call will be associated with the SMPTE
 * ST.2086 metadata specified.
 *
 * metadata specifies additional information about the contents of the buffer
 * that may affect how it's displayed.  When it is nullptr, it means no such
 * information is available.  No SMPTE ST.2086 metadata is associated with the
 * buffers by default.
 */
static inline int native_window_set_buffers_smpte2086_metadata(
        struct ANativeWindow* window,
        const struct android_smpte2086_metadata* metadata)
{
    return window->perform(window, NATIVE_WINDOW_SET_BUFFERS_SMPTE2086_METADATA,
            metadata);
}

/*
 * native_window_set_buffers_cta861_3_metadata(..., metadata)
 * All buffers queued after this call will be associated with the CTA-861.3
 * metadata specified.
 *
 * metadata specifies additional information about the contents of the buffer
 * that may affect how it's displayed.  When it is nullptr, it means no such
 * information is available.  No CTA-861.3 metadata is associated with the
 * buffers by default.
 */
static inline int native_window_set_buffers_cta861_3_metadata(
        struct ANativeWindow* window,
        const struct android_cta861_3_metadata* metadata)
{
    return window->perform(window, NATIVE_WINDOW_SET_BUFFERS_CTA861_3_METADATA,
            metadata);
}

/*
 * native_window_set_buffers_hdr10_plus_metadata(..., metadata)
 * All buffers queued after this call will be associated with the
 * HDR10+ dynamic metadata specified.
 *
 * metadata specifies additional dynamic information about the
 * contents of the buffer that may affect how it is displayed.  When
 * it is nullptr, it means no such information is available.  No
 * HDR10+ dynamic emtadata is associated with the buffers by default.
 *
 * Parameter "size" refers to the length of the metadata blob pointed to
 * by parameter "data".  The metadata blob will adhere to the HDR10+ SEI
 * message standard.
 */
static inline int native_window_set_buffers_hdr10_plus_metadata(struct ANativeWindow* window,
                                                           const size_t size,
                                                           const uint8_t* metadata) {
    return window->perform(window, NATIVE_WINDOW_SET_BUFFERS_HDR10_PLUS_METADATA, size,
                           metadata);
}

/*
 * native_window_set_buffers_transform(..., int transform)
 * All buffers queued after this call will be displayed transformed according
 * to the transform parameter specified.
 */
static inline int native_window_set_buffers_transform(
        struct ANativeWindow* window,
        int transform)
{
    return window->perform(window, NATIVE_WINDOW_SET_BUFFERS_TRANSFORM,
            transform);
}

/*
 * native_window_set_buffers_sticky_transform(..., int transform)
 * All buffers queued after this call will be displayed transformed according
 * to the transform parameter specified applied on top of the regular buffer
 * transform.  Setting this transform will disable the transform hint.
 *
 * Temporary - This is only intended to be used by the LEGACY camera mode, do
 *   not use this for anything else.
 */
static inline int native_window_set_buffers_sticky_transform(
        struct ANativeWindow* window,
        int transform)
{
    return window->perform(window, NATIVE_WINDOW_SET_BUFFERS_STICKY_TRANSFORM,
            transform);
}

/*
 * native_window_set_buffers_timestamp(..., int64_t timestamp)
 * All buffers queued after this call will be associated with the timestamp
 * parameter specified. If the timestamp is set to NATIVE_WINDOW_TIMESTAMP_AUTO
 * (the default), timestamps will be generated automatically when queueBuffer is
 * called. The timestamp is measured in nanoseconds, and is normally monotonically
 * increasing. The timestamp should be unaffected by time-of-day adjustments,
 * and for a camera should be strictly monotonic but for a media player may be
 * reset when the position is set.
 */
static inline int native_window_set_buffers_timestamp(
        struct ANativeWindow* window,
        int64_t timestamp)
{
    return window->perform(window, NATIVE_WINDOW_SET_BUFFERS_TIMESTAMP,
            timestamp);
}

/*
 * native_window_set_scaling_mode(..., int mode)
 * All buffers queued after this call will be associated with the scaling mode
 * specified.
 */
static inline int native_window_set_scaling_mode(
        struct ANativeWindow* window,
        int mode)
{
    return window->perform(window, NATIVE_WINDOW_SET_SCALING_MODE,
            mode);
}

/*
 * native_window_api_connect(..., int api)
 * connects an API to this window. only one API can be connected at a time.
 * Returns -EINVAL if for some reason the window cannot be connected, which
 * can happen if it's connected to some other API.
 */
static inline int native_window_api_connect(
        struct ANativeWindow* window, int api)
{
    return window->perform(window, NATIVE_WINDOW_API_CONNECT, api);
}

/*
 * native_window_api_disconnect(..., int api)
 * disconnect the API from this window.
 * An error is returned if for instance the window wasn't connected in the
 * first place.
 */
static inline int native_window_api_disconnect(
        struct ANativeWindow* window, int api)
{
    return window->perform(window, NATIVE_WINDOW_API_DISCONNECT, api);
}

/*
 * native_window_dequeue_buffer_and_wait(...)
 * Dequeue a buffer and wait on the fence associated with that buffer.  The
 * buffer may safely be accessed immediately upon this function returning.  An
 * error is returned if either of the dequeue or the wait operations fail.
 */
static inline int native_window_dequeue_buffer_and_wait(ANativeWindow *anw,
        struct ANativeWindowBuffer** anb) {
    return anw->dequeueBuffer_DEPRECATED(anw, anb);
}

/*
 * native_window_set_sideband_stream(..., native_handle_t*)
 * Attach a sideband buffer stream to a native window.
 */
static inline int native_window_set_sideband_stream(
        struct ANativeWindow* window,
        native_handle_t* sidebandHandle)
{
    return window->perform(window, NATIVE_WINDOW_SET_SIDEBAND_STREAM,
            sidebandHandle);
}

/*
 * native_window_set_surface_damage(..., android_native_rect_t* rects, int numRects)
 * Set the surface damage (i.e., the region of the surface that has changed
 * since the previous frame). The damage set by this call will be reset (to the
 * default of full-surface damage) after calling queue, so this must be called
 * prior to every frame with damage that does not cover the whole surface if the
 * caller desires downstream consumers to use this optimization.
 *
 * The damage region is specified as an array of rectangles, with the important
 * caveat that the origin of the surface is considered to be the bottom-left
 * corner, as in OpenGL ES.
 *
 * If numRects is set to 0, rects may be NULL, and the surface damage will be
 * set to the full surface (the same as if this function had not been called for
 * this frame).
 */
static inline int native_window_set_surface_damage(
        struct ANativeWindow* window,
        const android_native_rect_t* rects, size_t numRects)
{
    return window->perform(window, NATIVE_WINDOW_SET_SURFACE_DAMAGE,
            rects, numRects);
}

/*
 * native_window_set_shared_buffer_mode(..., bool sharedBufferMode)
 * Enable/disable shared buffer mode
 */
static inline int native_window_set_shared_buffer_mode(
        struct ANativeWindow* window,
        bool sharedBufferMode)
{
    return window->perform(window, NATIVE_WINDOW_SET_SHARED_BUFFER_MODE,
            sharedBufferMode);
}

/*
 * native_window_set_auto_refresh(..., autoRefresh)
 * Enable/disable auto refresh when in shared buffer mode
 */
static inline int native_window_set_auto_refresh(
        struct ANativeWindow* window,
        bool autoRefresh)
{
    return window->perform(window, NATIVE_WINDOW_SET_AUTO_REFRESH, autoRefresh);
}

static inline int native_window_get_refresh_cycle_duration(
        struct ANativeWindow* window,
        int64_t* outRefreshDuration)
{
    return window->perform(window, NATIVE_WINDOW_GET_REFRESH_CYCLE_DURATION,
            outRefreshDuration);
}

static inline int native_window_get_next_frame_id(
        struct ANativeWindow* window, uint64_t* frameId)
{
    return window->perform(window, NATIVE_WINDOW_GET_NEXT_FRAME_ID, frameId);
}

static inline int native_window_enable_frame_timestamps(
        struct ANativeWindow* window, bool enable)
{
    return window->perform(window, NATIVE_WINDOW_ENABLE_FRAME_TIMESTAMPS,
            enable);
}

static inline int native_window_get_compositor_timing(
        struct ANativeWindow* window,
        int64_t* compositeDeadline, int64_t* compositeInterval,
        int64_t* compositeToPresentLatency)
{
    return window->perform(window, NATIVE_WINDOW_GET_COMPOSITOR_TIMING,
            compositeDeadline, compositeInterval, compositeToPresentLatency);
}

static inline int native_window_get_frame_timestamps(
        struct ANativeWindow* window, uint64_t frameId,
        int64_t* outRequestedPresentTime, int64_t* outAcquireTime,
        int64_t* outLatchTime, int64_t* outFirstRefreshStartTime,
        int64_t* outLastRefreshStartTime, int64_t* outGpuCompositionDoneTime,
        int64_t* outDisplayPresentTime, int64_t* outDequeueReadyTime,
        int64_t* outReleaseTime)
{
    return window->perform(window, NATIVE_WINDOW_GET_FRAME_TIMESTAMPS,
            frameId, outRequestedPresentTime, outAcquireTime, outLatchTime,
            outFirstRefreshStartTime, outLastRefreshStartTime,
            outGpuCompositionDoneTime, outDisplayPresentTime,
            outDequeueReadyTime, outReleaseTime);
}

/* deprecated. Always returns 0 and outSupport holds true. Don't call. */
static inline int native_window_get_wide_color_support (
    struct ANativeWindow* window __UNUSED, bool* outSupport) __deprecated;

/*
   Deprecated(b/242763577): to be removed, this method should not be used
   Surface support should not be tied to the display
   Return true since most displays should have this support
*/
static inline int native_window_get_wide_color_support (
    struct ANativeWindow* window __UNUSED, bool* outSupport) {
    *outSupport = true;
    return 0;
}

/* deprecated. Always returns 0 and outSupport holds true. Don't call. */
static inline int native_window_get_hdr_support(struct ANativeWindow* window __UNUSED,
                                                bool* outSupport) __deprecated;

/*
   Deprecated(b/242763577): to be removed, this method should not be used
   Surface support should not be tied to the display
   Return true since most displays should have this support
*/
static inline int native_window_get_hdr_support(struct ANativeWindow* window __UNUSED,
                                                bool* outSupport) {
    *outSupport = true;
    return 0;
}

static inline int native_window_get_consumer_usage(struct ANativeWindow* window,
                                                   uint64_t* outUsage) {
    return window->perform(window, NATIVE_WINDOW_GET_CONSUMER_USAGE64, outUsage);
}

/*
 * native_window_set_auto_prerotation(..., autoPrerotation)
 * Enable/disable the auto prerotation at buffer allocation when the buffer size
 * is driven by the consumer.
 *
 * When buffer size is driven by the consumer and the transform hint specifies
 * a 90 or 270 degree rotation, if auto prerotation is enabled, the width and
 * height used for dequeueBuffer will be additionally swapped.
 */
static inline int native_window_set_auto_prerotation(struct ANativeWindow* window,
                                                     bool autoPrerotation) {
    return window->perform(window, NATIVE_WINDOW_SET_AUTO_PREROTATION, autoPrerotation);
}

/*
 * Internal extension of ANativeWindow_FrameRateCompatibility.
 */
enum {
    /**
     * This surface belongs to an app on the High Refresh Rate Deny list, and needs the display
     * to operate at the exact frame rate.
     *
     * Keep in sync with Surface.java constant.
     */
    ANATIVEWINDOW_FRAME_RATE_EXACT = 100,

    /**
     * This surface is ignored while choosing the refresh rate.
     */
    ANATIVEWINDOW_FRAME_RATE_NO_VOTE,

    /**
     * This surface will vote for the minimum refresh rate.
     */
    ANATIVEWINDOW_FRAME_RATE_MIN
};

/*
 * Frame rate category values that can be used in Transaction::setFrameRateCategory.
 */
enum {
    /**
     * Default value. This value can also be set to return to default behavior, such as layers
     * without animations.
     */
    ANATIVEWINDOW_FRAME_RATE_CATEGORY_DEFAULT = 0,

    /**
     * The layer will explicitly not influence the frame rate.
     * This may indicate a frame rate suitable for no animation updates (such as a cursor blinking
     * or a sporadic update).
     */
    ANATIVEWINDOW_FRAME_RATE_CATEGORY_NO_PREFERENCE = 1,

    /**
     * Indicates a frame rate suitable for animations that looks fine even if played at a low frame
     * rate.
     */
    ANATIVEWINDOW_FRAME_RATE_CATEGORY_LOW = 2,

    /**
     * Indicates a middle frame rate suitable for animations that do not require higher frame
     * rates, or do not benefit from high smoothness. This is normally 60 Hz or close to it.
     */
    ANATIVEWINDOW_FRAME_RATE_CATEGORY_NORMAL = 3,

    /**
     * Indicates that, as a result of a user interaction, an animation is likely to start.
     * This category is a signal that a user interaction heuristic determined the need of a
     * high refresh rate, and is not an explicit request from the app.
     * As opposed to FRAME_RATE_CATEGORY_HIGH, this vote may be ignored in favor of
     * more explicit votes.
     */
    ANATIVEWINDOW_FRAME_RATE_CATEGORY_HIGH_HINT = 4,

    /**
     * Indicates a frame rate suitable for animations that require a high frame rate, which may
     * increase smoothness but may also increase power usage.
     */
    ANATIVEWINDOW_FRAME_RATE_CATEGORY_HIGH = 5
};

/*
 * Frame rate selection strategy values that can be used in
 * Transaction::setFrameRateSelectionStrategy.
 */
enum {
    /**
     * Default value. The layer uses its own frame rate specifications, assuming it has any
     * specifications, instead of its parent's. If it does not have its own frame rate
     * specifications, it will try to use its parent's. It will propagate its specifications to any
     * descendants that do not have their own.
     *
     * However, FRAME_RATE_SELECTION_STRATEGY_OVERRIDE_CHILDREN on an ancestor layer
     * supersedes this behavior, meaning that this layer will inherit frame rate specifications
     * regardless of whether it has its own.
     */
    ANATIVEWINDOW_FRAME_RATE_SELECTION_STRATEGY_PROPAGATE = 0,

    /**
     * The layer's frame rate specifications will propagate to and override those of its descendant
     * layers.
     *
     * The layer itself has the FRAME_RATE_SELECTION_STRATEGY_PROPAGATE behavior.
     * Thus, ancestor layer that also has the strategy
     * FRAME_RATE_SELECTION_STRATEGY_OVERRIDE_CHILDREN will override this layer's
     * frame rate specifications.
     */
    ANATIVEWINDOW_FRAME_RATE_SELECTION_STRATEGY_OVERRIDE_CHILDREN = 1,

    /**
     * The layer's frame rate specifications will not propagate to its descendant
     * layers, even if the descendant layer has no frame rate specifications.
     * However, FRAME_RATE_SELECTION_STRATEGY_OVERRIDE_CHILDREN on an ancestor
     * layer supersedes this behavior.
     */
    ANATIVEWINDOW_FRAME_RATE_SELECTION_STRATEGY_SELF = 2,
};

static inline int native_window_set_frame_rate(struct ANativeWindow* window, float frameRate,
                                        int8_t compatibility, int8_t changeFrameRateStrategy) {
    return window->perform(window, NATIVE_WINDOW_SET_FRAME_RATE, (double)frameRate,
                           (int)compatibility, (int)changeFrameRateStrategy);
}

struct ANativeWindowFrameTimelineInfo {
    // Frame Id received from ANativeWindow_getNextFrameId.
    uint64_t frameNumber;

    // VsyncId received from the Choreographer callback that started this frame.
    int64_t frameTimelineVsyncId;

    // Input Event ID received from the input event that started this frame.
    int32_t inputEventId;

    // The time which this frame rendering started (i.e. when Choreographer callback actually run)
    int64_t startTimeNanos;

    // Whether or not to use the vsyncId to determine the refresh rate. Used for TextureView only.
    int32_t useForRefreshRateSelection;

    // The VsyncId of a frame that was not drawn and squashed into this frame.
    // Used for UI thread updates that were not picked up by RenderThread on time.
    int64_t skippedFrameVsyncId;

    // The start time of a frame that was not drawn and squashed into this frame.
    int64_t skippedFrameStartTimeNanos;
};

static inline int native_window_set_frame_timeline_info(
        struct ANativeWindow* window, struct ANativeWindowFrameTimelineInfo frameTimelineInfo) {
    return window->perform(window, NATIVE_WINDOW_SET_FRAME_TIMELINE_INFO, frameTimelineInfo);
}

/**
 * native_window_set_buffers_additional_options(..., ExtendableType* additionalOptions, size_t size)
 * All buffers dequeued after this call will have the additionalOptions specified.
 *
 * This must only be called after api_connect, otherwise NO_INIT is returned. The options are
 * cleared in api_disconnect & api_connect
 *
 * If IAllocator is not v2 or newer this method returns INVALID_OPERATION
 *
 * \return NO_ERROR on success.
 * \return NO_INIT if no api is connected
 * \return INVALID_OPERATION if additional option support is not available
 */
static inline int native_window_set_buffers_additional_options(
        struct ANativeWindow* window, const AHardwareBufferLongOptions* additionalOptions,
        size_t additionalOptionsSize) {
    return window->perform(window, NATIVE_WINDOW_SET_BUFFERS_ADDITIONAL_OPTIONS, additionalOptions,
                           additionalOptionsSize);
}

// ------------------------------------------------------------------------------------------------
// Candidates for APEX visibility
// These functions are planned to be made stable for APEX modules, but have not
// yet been stabilized to a specific api version.
// ------------------------------------------------------------------------------------------------

/**
 * Retrieves the last queued buffer for this window, along with the fence that
 * fires when the buffer is ready to be read, and the 4x4 coordinate
 * transform matrix that should be applied to the buffer's content. The
 * transform matrix is represented in column-major order.
 *
 * If there was no buffer previously queued, then outBuffer will be NULL and
 * the value of outFence will be -1.
 *
 * Note that if outBuffer is not NULL, then the caller will hold a reference
 * onto the buffer. Accordingly, the caller must call AHardwareBuffer_release
 * when the buffer is no longer needed so that the system may reclaim the
 * buffer.
 *
 * \return NO_ERROR on success.
 * \return NO_MEMORY if there was insufficient memory.
 */
static inline int ANativeWindow_getLastQueuedBuffer(ANativeWindow* window,
                                                    AHardwareBuffer** outBuffer, int* outFence,
                                                    float outTransformMatrix[16]) {
    return window->perform(window, NATIVE_WINDOW_GET_LAST_QUEUED_BUFFER, outBuffer, outFence,
                           outTransformMatrix);
}

/**
 * Retrieves the last queued buffer for this window, along with the fence that
 * fires when the buffer is ready to be read. The cropRect & transform should be applied to the
 * buffer's content.
 *
 * If there was no buffer previously queued, then outBuffer will be NULL and
 * the value of outFence will be -1.
 *
 * Note that if outBuffer is not NULL, then the caller will hold a reference
 * onto the buffer. Accordingly, the caller must call AHardwareBuffer_release
 * when the buffer is no longer needed so that the system may reclaim the
 * buffer.
 *
 * \return NO_ERROR on success.
 * \return NO_MEMORY if there was insufficient memory.
 * \return STATUS_UNKNOWN_TRANSACTION if this ANativeWindow doesn't support this method, callers
 *         should fall back to ANativeWindow_getLastQueuedBuffer instead.
 */
static inline int ANativeWindow_getLastQueuedBuffer2(ANativeWindow* window,
                                                     AHardwareBuffer** outBuffer, int* outFence,
                                                     ARect* outCropRect, uint32_t* outTransform) {
    return window->perform(window, NATIVE_WINDOW_GET_LAST_QUEUED_BUFFER2, outBuffer, outFence,
                           outCropRect, outTransform);
}

/**
 * Retrieves an identifier for the next frame to be queued by this window.
 *
 * Frame ids start at 1 and are incremented on each new frame until the underlying surface changes,
 * in which case the frame id is reset to 1.
 *
 * \return the next frame id (0 being uninitialized).
 */
static inline uint64_t ANativeWindow_getNextFrameId(ANativeWindow* window) {
    uint64_t value;
    window->perform(window, NATIVE_WINDOW_GET_NEXT_FRAME_ID, &value);
    return value;
}

/**
 * Prototype of the function that an ANativeWindow implementation would call
 * when ANativeWindow_query is called.
 */
typedef int (*ANativeWindow_queryFn)(const ANativeWindow* window, int what, int* value);

/**
 * Prototype of the function that intercepts an invocation of
 * ANativeWindow_queryFn, along with a data pointer that's passed by the
 * caller who set the interceptor, as well as arguments that would be
 * passed to ANativeWindow_queryFn if it were to be called.
 */
typedef int (*ANativeWindow_queryInterceptor)(const ANativeWindow* window,
                                                ANativeWindow_queryFn perform, void* data,
                                                int what, int* value);

/**
 * Registers an interceptor for ANativeWindow_query. Instead of calling
 * the underlying query function, instead the provided interceptor is
 * called, which may optionally call the underlying query function. An
 * optional data pointer is also provided to side-channel additional arguments.
 *
 * Note that usage of this should only be used for specialized use-cases by
 * either the system partition or to Mainline modules. This should never be
 * exposed to NDK or LL-NDK.
 *
 * Returns NO_ERROR on success, -errno if registration failed.
 */
static inline int ANativeWindow_setQueryInterceptor(ANativeWindow* window,
                                            ANativeWindow_queryInterceptor interceptor,
                                            void* data) {
    return window->perform(window, NATIVE_WINDOW_SET_QUERY_INTERCEPTOR, interceptor, data);
}

__END_DECLS
