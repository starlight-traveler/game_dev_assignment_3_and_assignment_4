/**
 * @file SDL_Manager.h
 * @brief Singleton wrapper around SDL setup/teardown and window handling
 */
#ifndef SDL_MANAGER_H
#define SDL_MANAGER_H

#include <SDL.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

/**
 * @brief RAII-managed SDL singleton that owns windows, surfaces, and optional GL context
 */
class SDL_Manager {
public:
    // singleton accessor, only one SDL manager allowed
    // singleton accessor, we dont wanna copy SDL state around
    /**
     * @brief Returns the singleton SDL manager instance
     * @return Reference to the global SDL manager
     */
    static SDL_Manager& sdl();

    /**
     * @brief Creates a window and stores it in internal storage
     * @param title Desired window title text
     * @param width Requested width in pixels
     * @param height Requested height in pixels
     * @param resizable SDL boolean controlling resizable behavior
     * @return True when the window was created successfully
     */
    bool spawnWindow(const std::string& title, int width, int height,
                     SDL_bool resizable = SDL_FALSE);

    /**
     * @brief Creates a window at explicit coordinates
     * @param title Desired window title text
     * @param width Requested width in pixels
     * @param height Requested height in pixels
     * @param x Desired top-left x position in pixels
     * @param y Desired top-left y position in pixels
     * @param resizable SDL boolean controlling resizable behavior
     * @return True when the window was created successfully
     */
    bool spawnWindowAt(const std::string& title, int width, int height, int x, int y,
                       SDL_bool resizable = SDL_FALSE);

    /**
     * @brief Closes a window identified by SDL window id
     * @param window_id SDL window id to close
     */
    void closeWindow(std::uint32_t window_id);

    /**
     * @brief Presents all active windows
     * @details Swaps the OpenGL window and updates software surfaces for non-GL windows
     */
    void updateWindows();

    /**
     * @brief Refreshes the stored software surface after resize
     * @param window_id SDL window id that was resized
     */
    void refreshWindowBuffer(std::uint32_t window_id);

    /**
     * @brief Reports whether a valid OpenGL context currently exists
     * @return True when OpenGL context is present
     */
    bool hasOpenGLContext() const;

    /**
     * @brief Makes the stored OpenGL context current on the OpenGL window
     * @return True when SDL_GL_MakeCurrent succeeds
     */
    bool makeOpenGLCurrent() const;

    /**
     * @brief Makes the OpenGL context current for a specific window index
     * @param index Window index
     * @return True when SDL_GL_MakeCurrent succeeds
     */
    bool makeOpenGLCurrentAt(std::size_t index) const;

    /**
     * @brief Returns the OpenGL window pointer when available
     * @return OpenGL window pointer or nullptr
     */
    SDL_Window* openGLWindow() const;

    /**
     * @brief Returns a window pointer by internal index
     * @param index Internal window array index
     * @return SDL window pointer or nullptr when index is invalid
     */
    SDL_Window* windowAt(std::size_t index) const;

    /**
     * @brief Returns a software surface pointer by internal index
     * @param index Internal window array index
     * @return SDL surface pointer or nullptr when index is invalid
     */
    SDL_Surface* bufferAt(std::size_t index) const;

    /**
     * @brief Returns number of currently active windows
     * @return Active window count
     */
    std::size_t windowCount() const;

    /**
     * @brief Signals whether shutdown was requested by closing the GL window
     * @return True when app should exit main loop
     */
    bool shouldQuit() const;

    // copying this thing would be a mess
    // delete copy so no accidental duplicate SDL state
    SDL_Manager(const SDL_Manager&) = delete;
    SDL_Manager& operator=(const SDL_Manager&) = delete;

private:
    /**
     * @brief Initializes SDL video subsystem and hint configuration
     */
    SDL_Manager();

    /**
     * @brief Destroys windows and shuts down SDL subsystem
     */
    ~SDL_Manager();

    /**
     * @brief Destroys all active windows and resets internal storage
     */
    void destroyAllWindows();

    // store window ptrs and GL contexts in same index order
    std::vector<SDL_Window*> windows_;      // same index as gl_contexts_
    std::vector<SDL_GLContext> gl_contexts_; // per-window GL context
    // remember the first window id so closing it ends app
    std::uint32_t first_window_id_;         // id of the very first window created
    // quit flag used by main loop
    bool quit_requested_;                   // set when app should close
    // GLEW init flag, only once
    bool glew_initialized_;                 // one-time glew boot flag
};

#endif
