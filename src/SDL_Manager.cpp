#include "SDL_Manager.h"

#include <stdexcept>
#include <string>

#include <GL/glew.h>

#include "quill/Frontend.h"
#include "quill/LogMacros.h"
#include "quill/sinks/ConsoleSink.h"

/**
 * @file SDL_Manager.cpp
 */
namespace {
/**
 * @brief Returns logger used by SDL manager
 * @return Pointer to logger
 */
quill::Logger *get_logger() {
  // try get logger by name, this might be null if not setup yet
  // if logger already exists just use it
  quill::Logger *logger = quill::Frontend::get_logger("sdl");
  if (!logger) {
    // fallback so we can still log inside init
    // fallback logger path so class can log during startup
    auto console_sink =
        quill::Frontend::create_or_get_sink<quill::ConsoleSink>("console_sink");
    logger = quill::Frontend::create_or_get_logger("sdl", console_sink);
  }
  // return logger pointer (never null now)
  return logger;
}
} // namespace

/**
 * @brief Access the singleton instance.
 */
SDL_Manager &SDL_Manager::sdl() {
  // function-static means this builds once n then reuses
  static SDL_Manager instance;
  // return singleton reference
  return instance;
}

/**
 * @brief Initialize SDL video subsystem.
 */
SDL_Manager::SDL_Manager()
    : windows_{},
      gl_contexts_{},
      first_window_id_(0),
      quit_requested_(false),
      glew_initialized_(false) {
  // Prefer client-side Wayland decorations (title bar + close button) when available.
  // Only applies to Linux/Wayland; harmless elsewhere but we keep it Linux-only for clarity.
#if defined(__linux__)
  // these hints are only for wayland decoration behavior
  SDL_SetHint(SDL_HINT_VIDEO_WAYLAND_ALLOW_LIBDECOR, "1");
  SDL_SetHint(SDL_HINT_VIDEO_WAYLAND_PREFER_LIBDECOR, "1");
#endif

  // If user did not force a backend we try x11 first on Linux for better decorations
  // on some setups. On macOS/Windows, let SDL pick the platform-default driver.
  const char* requested_driver = SDL_getenv("SDL_VIDEODRIVER");
  bool injected_x11_driver = false;
#if defined(__linux__)
  // only set x11 default on linux
  injected_x11_driver = (requested_driver == nullptr);
  if (injected_x11_driver) {
    // On some Hyprland setups, Wayland decorations may be unavailable.
    // Prefer XWayland for decorated title bars and close buttons.
    SDL_setenv("SDL_VIDEODRIVER", "x11", 1);
  }
#else
  // silence unused warning on non linux
  (void)requested_driver;
#endif

  if (SDL_Init(SDL_INIT_VIDEO) != 0) {
    // keep first error text so fallback logs are useful
    const std::string first_error = SDL_GetError();
    if (injected_x11_driver) {
      // fallback to wayland if x11 backend is not available
      // fallback to wayland if x11 backend is not available
      SDL_setenv("SDL_VIDEODRIVER", "wayland", 1);
      if (SDL_Init(SDL_INIT_VIDEO) == 0) {
        LOG_WARNING(get_logger(),
                    "SDL_Init with x11 failed ({}), fell back to wayland",
                    first_error);
      } else {
        // both backends failed, bail
        const std::string second_error = SDL_GetError();
        LOG_ERROR(get_logger(),
                  "SDL_Init failed with x11 ('{}') and wayland ('{}')",
                  first_error, second_error);
        throw std::runtime_error("SDL_Init failed for x11 and wayland");
      }
    } else {
      // no fallback path, just log error
      LOG_ERROR(get_logger(), "SDL_Init failed: {}", first_error);
      throw std::runtime_error("SDL_Init failed");
    }
  }
  // success init
  LOG_INFO(get_logger(), "SDL_Init succeeded");
}

/**
 * @brief Destroy all windows and shut down SDL.
 */
SDL_Manager::~SDL_Manager() {
  // clean windows + contexts first
  destroyAllWindows();
  // shut down SDL
  SDL_Quit();
}

/**
 * @brief Spawn a new SDL window and store its surface.
 */
bool SDL_Manager::spawnWindow(const std::string &title, int width, int height,
                              SDL_bool resizable) {
  // fallback placement if caller does not care about exact screen layout
  const int index = static_cast<int>(windows_.size());
  const int x_gap = 40;
  const int y_gap = 60;
  const int cols = 3;
  // computed x/y so windows dont overlap
  const int x = 80 + (index % cols) * (width + x_gap);
  const int y = 80 + (index / cols) * (height + y_gap);
  // use spawnWindowAt to do actual creation
  return spawnWindowAt(title, width, height, x, y, resizable);
}

bool SDL_Manager::spawnWindowAt(const std::string &title, int width, int height, int x, int y,
                                SDL_bool resizable) {
  // setup GL attrs before each create so new windows match config
  // every mesh window is GL now, so each one can render
  // request core 4.1, matches shader version in code
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,
                      SDL_GL_CONTEXT_PROFILE_CORE);
  SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
  SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);

  // base flags, shown + focusable
  Uint32 flags = SDL_WINDOW_SHOWN | SDL_WINDOW_INPUT_FOCUS;
  // always create GL window for this project
  flags |= SDL_WINDOW_OPENGL;
  if (resizable == SDL_TRUE) {
    // allow resize if caller asked
    flags |= SDL_WINDOW_RESIZABLE;
  }

  SDL_Window *window =
      SDL_CreateWindow(title.c_str(), x, y, width, height, flags);

  if (!window) {
    // creation fail
    LOG_ERROR(get_logger(), "SDL_CreateWindow failed: {}", SDL_GetError());
    return false;
  }

  if (resizable == SDL_TRUE) {
    // keep behavior explicit even if resizable flag already set
    SDL_SetWindowResizable(window, SDL_TRUE);
  }

  // each window gets its own GL context
  SDL_GLContext context = SDL_GL_CreateContext(window);
  if (!context) {
    // if context creation fails, destroy window and return
    LOG_ERROR(get_logger(), "SDL_GL_CreateContext failed: {}", SDL_GetError());
    SDL_DestroyWindow(window);
    return false;
  }
  if (SDL_GL_MakeCurrent(window, context) != 0) {
    // failed to make context current, cleanup
    LOG_ERROR(get_logger(), "SDL_GL_MakeCurrent failed: {}", SDL_GetError());
    SDL_GL_DeleteContext(context);
    SDL_DestroyWindow(window);
    return false;
  }

  if (!glew_initialized_) {
    // glew only needs init once per process
    glewExperimental = GL_TRUE;
    GLenum glew_error = glewInit();
    if (glew_error != GLEW_OK) {
      // log and cleanup if glew fails
      LOG_ERROR(get_logger(), "glewInit failed: {}",
                reinterpret_cast<const char *>(glewGetErrorString(glew_error)));
      SDL_GL_DeleteContext(context);
      SDL_DestroyWindow(window);
      return false;
    }
    // glew sometimes leaves a GL error, clear it
    glGetError();
    glew_initialized_ = true;
  }

  if (windows_.empty()) {
    // first created window id is special, closing it ends app
    first_window_id_ = SDL_GetWindowID(window);
  }

  // toss ptr+context into vectors in same order
  windows_.push_back(window);
  gl_contexts_.push_back(context);

  // log success with count
  LOG_INFO(get_logger(), "Spawned window '{}' ({}x{}), total={}", title, width,
           height, windows_.size());
  return true;
}

/**
 * @brief Close a window by ID using swap-and-pop to keep storage contiguous.
 */
void SDL_Manager::closeWindow(std::uint32_t window_id) {
  // linear scan is fine with tiny fixed window count
  for (std::size_t i = 0; i < windows_.size(); ++i) {
    if (!windows_[i] || SDL_GetWindowID(windows_[i]) != window_id) {
      continue;
    }

    if (window_id != 0 && window_id == first_window_id_) {
      // closing the first window should end the app
      quit_requested_ = true;
      first_window_id_ = 0;
    }

    SDL_GL_DeleteContext(gl_contexts_[i]);
    SDL_DestroyWindow(windows_[i]);
    // erase keeps arrays lined up, no stale indexes
    windows_.erase(windows_.begin() + static_cast<std::ptrdiff_t>(i));
    gl_contexts_.erase(gl_contexts_.begin() + static_cast<std::ptrdiff_t>(i));
    if (windows_.empty()) {
      // if no windows remain, app should quit too
      quit_requested_ = true;
    }
    LOG_INFO(get_logger(), "Closed window id={}, total={}", window_id, windows_.size());
    return;
  }
}

/**
 * @brief Update all active window surfaces.
 */
void SDL_Manager::updateWindows() {
  // present every currently active window
  for (std::size_t i = 0; i < windows_.size(); ++i) {
    SDL_Window* window = windows_[i];
    if (window && i < gl_contexts_.size() && gl_contexts_[i]) {
      // each swap needs its matching context current first
      if (SDL_GL_MakeCurrent(window, gl_contexts_[i]) != 0) {
        LOG_WARNING(get_logger(), "SDL_GL_MakeCurrent failed during swap: {}", SDL_GetError());
        continue;
      }
      // swap buffers so rendered image shows
      SDL_GL_SwapWindow(window);
    }
  }
}

/**
 * @brief Refresh surface after a resize, since the old surface is invalid.
 */
void SDL_Manager::refreshWindowBuffer(std::uint32_t window_id) {
  // no software buffers used now, so this is noop
  (void)window_id;
}

bool SDL_Manager::hasOpenGLContext() const { return !gl_contexts_.empty(); }

/**
 * @brief Makes stored OpenGL context current on the OpenGL window
 * @return True when context is current and ready for GL calls
 */
bool SDL_Manager::makeOpenGLCurrent() const {
  if (windows_.empty() || gl_contexts_.empty()) {
    // no windows means no context
    return false;
  }
  // use the first window/context pair
  return SDL_GL_MakeCurrent(windows_.front(), gl_contexts_.front()) == 0;
}

bool SDL_Manager::makeOpenGLCurrentAt(std::size_t index) const {
  if (index >= windows_.size() || index >= gl_contexts_.size()) {
    // index out of range
    return false;
  }
  // make this window's context current
  return SDL_GL_MakeCurrent(windows_[index], gl_contexts_[index]) == 0;
}

SDL_Window *SDL_Manager::openGLWindow() const {
  // legacy helper; first window is "main" one
  if (windows_.empty()) {
    return nullptr;
  }
  // return the first window
  return windows_.front();
}

/**
 * @brief Return a window pointer by index.
 */
SDL_Window *SDL_Manager::windowAt(std::size_t index) const {
  if (index >= windows_.size()) {
    // bad index
    return nullptr;
  }
  // return raw pointer
  return windows_[index];
}

/**
 * @brief Return a window surface pointer by index.
 */
SDL_Surface *SDL_Manager::bufferAt(std::size_t index) const {
  // no software buffer anymore, so return null
  (void)index;
  return nullptr;
}

/**
 * @brief Return the number of active windows.
 */
std::size_t SDL_Manager::windowCount() const { return windows_.size(); }

bool SDL_Manager::shouldQuit() const { return quit_requested_; }

/**
 * @brief Destroy all windows and clear arrays.
 */
void SDL_Manager::destroyAllWindows() {
  // cleanup all contexts/windows in order
  for (std::size_t i = 0; i < windows_.size(); ++i) {
    if (i < gl_contexts_.size() && gl_contexts_[i]) {
      // delete GL context first
      SDL_GL_DeleteContext(gl_contexts_[i]);
      gl_contexts_[i] = nullptr;
    }
    if (windows_[i]) {
      // destroy window handle
      SDL_DestroyWindow(windows_[i]);
      windows_[i] = nullptr;
    }
  }
  // clear vectors to release memory
  windows_.clear();
  gl_contexts_.clear();
  first_window_id_ = 0;
  // once all windows are gone, app should exit
  quit_requested_ = true;
}
