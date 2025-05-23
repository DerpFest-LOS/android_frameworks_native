/*
 * Copyright (C) 2014 The Android Open Source Project
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

#include <input/DisplayViewport.h>
#include <input/Input.h>
#include <utils/BitSet.h>

namespace android {

struct SpriteIcon;

/**
 * Interface for tracking a mouse / touch pad pointer and touch pad spots.
 *
 * The spots are sprites on screen that visually represent the positions of
 * fingers
 *
 * The pointer controller is responsible for providing synchronization and for tracking
 * display orientation changes if needed. It works in the display panel's coordinate space, which
 * is the same coordinate space used by InputReader.
 */
class PointerControllerInterface {
protected:
    PointerControllerInterface() {}
    virtual ~PointerControllerInterface() {}

public:
    /**
     * Enum used to differentiate various types of PointerControllers for the transition to
     * using PointerChoreographer.
     *
     * TODO(b/293587049): Refactor the PointerController class into different controller types.
     */
    enum class ControllerType {
        // Represents a single mouse pointer.
        MOUSE,
        // Represents multiple touch spots.
        TOUCH,
        // Represents a single stylus pointer.
        STYLUS,
    };

    /* Dumps the state of the pointer controller. */
    virtual std::string dump() = 0;

    /* Move the pointer and return unconsumed delta if the pointer has crossed the current
     * viewport bounds.
     *
     * Return value may be used to move pointer to corresponding adjacent display, if it exists in
     * the display-topology */
    [[nodiscard]] virtual vec2 move(float deltaX, float deltaY) = 0;

    /* Sets the absolute location of the pointer. */
    virtual void setPosition(float x, float y) = 0;

    /* Gets the absolute location of the pointer. */
    virtual vec2 getPosition() const = 0;

    enum class Transition {
        // Fade/unfade immediately.
        IMMEDIATE,
        // Fade/unfade gradually.
        GRADUAL,
    };

    /* Fades the pointer out now. */
    virtual void fade(Transition transition) = 0;

    /* Makes the pointer visible if it has faded out.
     * The pointer never unfades itself automatically.  This method must be called
     * by the client whenever the pointer is moved or a button is pressed and it
     * wants to ensure that the pointer becomes visible again. */
    virtual void unfade(Transition transition) = 0;

    enum class Presentation {
        // Show the mouse pointer.
        POINTER,
        // Show spots and a spot anchor in place of the mouse pointer.
        SPOT,
        // Show the stylus hover pointer.
        STYLUS_HOVER,

        ftl_last = STYLUS_HOVER,
    };

    /* Sets the mode of the pointer controller. */
    virtual void setPresentation(Presentation presentation) = 0;

    /* Sets the spots for the current gesture.
     * The spots are not subject to the inactivity timeout like the pointer
     * itself it since they are expected to remain visible for so long as
     * the fingers are on the touch pad.
     *
     * The values of the AMOTION_EVENT_AXIS_PRESSURE axis is significant.
     * For spotCoords, pressure != 0 indicates that the spot's location is being
     * pressed (not hovering).
     */
    virtual void setSpots(const PointerCoords* spotCoords, const uint32_t* spotIdToIndex,
                          BitSet32 spotIdBits, ui::LogicalDisplayId displayId) = 0;

    /* Removes all spots. */
    virtual void clearSpots() = 0;

    /* Gets the id of the display where the pointer should be shown. */
    virtual ui::LogicalDisplayId getDisplayId() const = 0;

    /* Sets the associated display of this pointer. Pointer should show on that display. */
    virtual void setDisplayViewport(const DisplayViewport& displayViewport) = 0;

    /* Sets the pointer icon type for mice or styluses. */
    virtual void updatePointerIcon(PointerIconStyle iconId) = 0;

    /* Sets the custom pointer icon for mice or styluses. */
    virtual void setCustomPointerIcon(const SpriteIcon& icon) = 0;

    /* Sets the flag to skip screenshot of the pointer indicators on the display for the specified
     * displayId. This flag can only be reset with resetSkipScreenshotFlags()
     */
    virtual void setSkipScreenshotFlagForDisplay(ui::LogicalDisplayId displayId) = 0;

    /* Resets the flag to skip screenshot of the pointer indicators for all displays. */
    virtual void clearSkipScreenshotFlags() = 0;

    virtual ui::Transform getDisplayTransform() const = 0;
};

} // namespace android
