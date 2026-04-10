/**
 * @file SDL_Manager.h
 * @brief singleton wrapper around sdl startup window creation and gl context ownership
 */
#ifndef SDL_MANAGER_H
#define SDL_MANAGER_H

#include <SDL.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

/**
 * @brief global sdl manager for this project
 *
 * this class centralizes the messy platform setup work
 * it starts sdl once
 * creates windows
 * creates one gl context per window
 * swaps buffers every frame
 * and shuts everything down in one place
 */
class SDL_Manager {
public:
    // singleton accessor, only one SDL manager allowed
    // singleton accessor, we dont wanna copy SDL state around
    /**
     * @brief returns the one global sdl manager
     * @return reference to the singleton instance
     */
    static SDL_Manager& sdl();

    /**
     * @brief creates a window using automatic staggered placement
     * @param title window title
     * @param width requested width in pixels
     * @param height requested height in pixels
     * @param resizable whether resizing is allowed
     * @return true when the window and context were created
     */
    bool spawnWindow(const std::string& title, int width, int height,
                     SDL_bool resizable = SDL_FALSE);

    /**
     * @brief creates a window at exact screen coordinates
     * @param title window title
     * @param width requested width in pixels
     * @param height requested height in pixels
     * @param x top left x position
     * @param y top left y position
     * @param resizable whether resizing is allowed
     * @return true when creation succeeded
     */
    bool spawnWindowAt(const std::string& title, int width, int height, int x, int y,
                       SDL_bool resizable = SDL_FALSE);

    /**
     * @brief closes one window by sdl window id
     * @param window_id sdl window id
     */
    void closeWindow(std::uint32_t window_id);

    /**
     * @brief presents every active gl window by swapping its buffers
     */
    void updateWindows();

    /**
     * @brief legacy resize hook kept for compatibility
     * @param window_id sdl window id that changed size
     */
    void refreshWindowBuffer(std::uint32_t window_id);

    /**
     * @brief reports whether at least one gl context exists
     * @return true when gl is available
     */
    bool hasOpenGLContext() const;

    /**
     * @brief makes the first stored window context current
     * @return true when sdl accepts the switch
     */
    bool makeOpenGLCurrent() const;

    /**
     * @brief makes the context for one indexed window current
     * @param index internal window index
     * @return true when the switch succeeds
     */
    bool makeOpenGLCurrentAt(std::size_t index) const;

    /**
     * @brief returns the first window which acts as the main gl window
     * @return window pointer or nullptr
     */
    SDL_Window* openGLWindow() const;

    /**
     * @brief returns one raw window pointer by internal index
     * @param index internal window array index
     * @return sdl window pointer or nullptr
     */
    SDL_Window* windowAt(std::size_t index) const;

    /**
     * @brief compatibility getter for old software surface based code
     * @param index internal window array index
     * @return software surface pointer or nullptr
     */
    SDL_Surface* bufferAt(std::size_t index) const;

    /**
     * @brief returns how many windows are currently alive
     * @return active window count
     */
    std::size_t windowCount() const;

    /**
     * @brief tells the app when closing windows should end the main loop
     * @return true when quit was requested
     */
    bool shouldQuit() const;

    // copying this thing would be a mess
    // delete copy so no accidental duplicate SDL state
    SDL_Manager(const SDL_Manager&) = delete;
    SDL_Manager& operator=(const SDL_Manager&) = delete;

private:
    /**
     * @brief starts sdl and configures platform hints
     */
    SDL_Manager();

    /**
     * @brief destroys windows and shuts down sdl
     */
    ~SDL_Manager();

    /**
     * @brief destroys every tracked window and context
     */
    void destroyAllWindows();

    // windows_ and gl_contexts_ stay aligned by index
    std::vector<SDL_Window*> windows_;      // same index as gl_contexts_
    std::vector<SDL_GLContext> gl_contexts_; // per window gl context
    // remember the first created window because closing it is treated like app exit
    std::uint32_t first_window_id_;         // id of the very first window created
    // simple main loop quit flag
    bool quit_requested_;                   // set when app should close
    // glew only needs one successful init after the first current context exists
    bool glew_initialized_;                 // one time glew boot flag
};

#endif
