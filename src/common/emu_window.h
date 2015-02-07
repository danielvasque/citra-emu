// Copyright 2014 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include "common/common.h"
#include "common/scm_rev.h"
#include "common/string_util.h"
#include "common/key_map.h"

/**
 * Abstraction class used to provide an interface between emulation code and the frontend
 * (e.g. SDL, QGLWidget, GLFW, etc...).
 *
 * Design notes on the interaction between EmuWindow and the emulation core:
 * - Generally, decisions on anything visible to the user should be left up to the GUI.
 *   For example, the emulation core should not try to dictate some window title or size.
 *   This stuff is not the core's business and only causes problems with regards to thread-safety
 *   anyway.
 * - Under certain circumstances, it may be desirable for the core to politely request the GUI
 *   to set e.g. a minimum window size. However, the GUI should always be free to ignore any
 *   such hints.
 * - EmuWindow may expose some of its state as read-only to the emulation core, however care
 *   should be taken to make sure the provided information is self-consistent. This requires
 *   some sort of synchronization (most of this is still a TODO).
 * - DO NOT TREAT THIS CLASS AS A GUI TOOLKIT ABSTRACTION LAYER. That's not what it is. Please
 *   re-read the upper points again and think about it if you don't see this.
 */
class EmuWindow
{
public:
    /// Data structure to store emuwindow configuration
    struct WindowConfig {
        bool    fullscreen;
        int     res_width;
        int     res_height;
        std::pair<unsigned,unsigned> min_client_area_size;
    };

    /// Swap buffers to display the next frame
    virtual void SwapBuffers() = 0;

    /// Polls window events
    virtual void PollEvents() = 0;

    /// Makes the graphics context current for the caller thread
    virtual void MakeCurrent() = 0;

    /// Releases (dunno if this is the "right" word) the GLFW context from the caller thread
    virtual void DoneCurrent() = 0;

    virtual void ReloadSetKeymaps() = 0;

    /// Signals a key press action to the HID module
    static void KeyPressed(KeyMap::HostDeviceKey key);

    /// Signals a key release action to the HID module
    static void KeyReleased(KeyMap::HostDeviceKey key);

    /**
     * Returns currently active configuration.
     * @note Accesses to the returned object need not be consistent because it may be modified in another thread
     */
    const WindowConfig& GetActiveConfig() const {
        return active_config;
    }

    /**
     * Requests the internal configuration to be replaced by the specified argument at some point in the future.
     * @note This method is thread-safe, because it delays configuration changes to the GUI event loop. Hence there is no guarantee on when the requested configuration will be active.
     */
    void SetConfig(const WindowConfig& val) {
        config = val;
    }

    /**
      * Gets the framebuffer size in pixels.
      * @note This method is thread-safe
      */
    const std::pair<unsigned,unsigned> GetFramebufferSize() const {
        return framebuffer_size;
    }

    /**
     * Gets window client area width in logical coordinates.
     * @note For high-DPI systems, this is smaller than the framebuffer size.
     * @note This method is thread-safe
     */
    std::pair<unsigned,unsigned> GetClientAreaSize() const {
        return std::make_pair(client_area_width, client_area_height);
    }

protected:
    EmuWindow()
    {
        // TODO: Find a better place to set this.
        config.min_client_area_size = std::make_pair(400u, 480u);
        active_config = config;
    }
    virtual ~EmuWindow() {}

    /**
     * Processes any pending configuration changes from the last SetConfig call.
     * This method invokes OnMinimalClientAreaChangeRequest if the corresponding configuration
     * field changed.
     * @note Implementations will usually want to call this from the GUI thread.
     * @todo Actually call this in existing implementations.
     */
    void ProcessConfigurationChanges() {
        // TODO: For proper thread safety, we should eventually implement a proper
        // multiple-writer/single-reader queue...

        if (config.min_client_area_size != active_config.min_client_area_size) {
            OnMinimalClientAreaChangeRequest(config.min_client_area_size);
            config.min_client_area_size = active_config.min_client_area_size;
        }
    }

    /**
     * Update internal framebuffer size with the given parameter.
     * @note EmuWindow implementations will usually use this in window resize event handlers.
     */
    void NotifyFramebufferSizeChanged(const std::pair<unsigned,unsigned>& size) {
        framebuffer_size = size;
    }

    /**
     * Update internal client area size with the given parameter.
     * @note EmuWindow implementations will usually use this in window resize event handlers.
     */
    void NotifyClientAreaSizeChanged(const std::pair<unsigned,unsigned>& size) {
        client_area_width = size.first;
        client_area_height = size.second;
    }

private:
    /**
     * Handler called when the minimal client area was requested to be changed via SetConfig.
     * For the request to be honored, EmuWindow implementations will usually reimplement this function.
     */
    virtual void OnMinimalClientAreaChangeRequest(const std::pair<unsigned,unsigned>& minimal_size) {
        // By default, ignore this request and do nothing.
    }

    std::pair<unsigned,unsigned> framebuffer_size;

    unsigned client_area_width;    ///< Current client width, should be set by window impl.
    unsigned client_area_height;   ///< Current client height, should be set by window impl.

    WindowConfig config;         ///< Internal configuration (changes pending for being applied in ProcessConfigurationChanges)
    WindowConfig active_config;  ///< Internal active configuration
};
