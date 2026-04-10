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
 * @brief returns the logger used by this module
 * @return logger pointer
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
 * @brief returns the singleton instance
 */
SDL_Manager &SDL_Manager::sdl() {
  // function-static means this builds once n then reuses
  static SDL_Manager instance;
  // return singleton reference
  return instance;
}

/**
 * @brief starts sdl video and audio and applies platform hints
 */
SDL_Manager::SDL_Manager()
    : windows_{},
      gl_contexts_{},
      first_window_id_(0),
      quit_requested_(false),
      glew_initialized_(false) {
  // prefer decorated windows on linux wayland when possible
#if defined(__linux__)
  // these hints try to keep title bars and close buttons available
  SDL_SetHint(SDL_HINT_VIDEO_WAYLAND_ALLOW_LIBDECOR, "1");
  SDL_SetHint(SDL_HINT_VIDEO_WAYLAND_PREFER_LIBDECOR, "1");
#endif

  // if the user did not force a backend we try x11 first on linux
  // this project had better window decorations there on some systems
  const char* requested_driver = SDL_getenv("SDL_VIDEODRIVER");
  bool injected_x11_driver = false;
#if defined(__linux__)
  // only override backend defaults on linux
  injected_x11_driver = (requested_driver == nullptr);
  if (injected_x11_driver) {
    // fall back toward x11 first if the app did not request anything else
    SDL_setenv("SDL_VIDEODRIVER", "x11", 1);
  }
#else
  // silence unused warning on non linux
  (void)requested_driver;
#endif

  if (SDL_Init(SDL_INIT_VIDEO) != 0) {
    // keep the first failure text around because we may try a second backend next
    const std::string first_error = SDL_GetError();
    if (injected_x11_driver) {
      // if x11 was not available try wayland before giving up
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
  if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
    // audio is helpful but not fatal for the rest of the app
    LOG_WARNING(get_logger(), "SDL audio init failed: {}", SDL_GetError());
  }
  // at this point the core sdl setup is live
  LOG_INFO(get_logger(), "SDL_Init succeeded");
}

/**
 * @brief shuts down windows first then quits sdl
 */
SDL_Manager::~SDL_Manager() {
  // clean windows + contexts first
  destroyAllWindows();
  // shut down SDL
  SDL_Quit();
}

/**
 * @brief creates a window using a simple staggered placement grid
 */
bool SDL_Manager::spawnWindow(const std::string &title, int width, int height,
                              SDL_bool resizable) {
  // if the caller does not care about exact coordinates
  // spread windows out a bit so they do not stack directly on top of each other
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
  // set gl attributes before creation so the new context matches renderer expectations
  // every window in this project is a gl window
  // request core 4 1 to match the renderer setup
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,
                      SDL_GL_CONTEXT_PROFILE_CORE);
  SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
  SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);

  // base flags make the window visible and able to receive input
  Uint32 flags = SDL_WINDOW_SHOWN | SDL_WINDOW_INPUT_FOCUS;
  // renderer expects opengl so every window gets a gl context
  flags |= SDL_WINDOW_OPENGL;
  if (resizable == SDL_TRUE) {
    // opt in to resize support when requested
    flags |= SDL_WINDOW_RESIZABLE;
  }

  SDL_Window *window =
      SDL_CreateWindow(title.c_str(), x, y, width, height, flags);

  if (!window) {
    // window allocation failed before any context existed
    LOG_ERROR(get_logger(), "SDL_CreateWindow failed: {}", SDL_GetError());
    return false;
  }

  if (resizable == SDL_TRUE) {
    // keep resize behavior explicit
    SDL_SetWindowResizable(window, SDL_TRUE);
  }

  // each stored window gets its own matching gl context
  SDL_GLContext context = SDL_GL_CreateContext(window);
  if (!context) {
    // without a context the window is useless for this renderer
    LOG_ERROR(get_logger(), "SDL_GL_CreateContext failed: {}", SDL_GetError());
    SDL_DestroyWindow(window);
    return false;
  }
  if (SDL_GL_MakeCurrent(window, context) != 0) {
    // glew and later gl calls need the new context to be current first
    LOG_ERROR(get_logger(), "SDL_GL_MakeCurrent failed: {}", SDL_GetError());
    SDL_GL_DeleteContext(context);
    SDL_DestroyWindow(window);
    return false;
  }

  if (!glew_initialized_) {
    // glew loads function pointers after at least one real context exists
    // once loaded it does not need to run again for every window
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
    // glew can leave a harmless gl error flag behind so clear it once here
    glGetError();
    glew_initialized_ = true;
  }

  if (windows_.empty()) {
    // the first window acts like the main application window
    first_window_id_ = SDL_GetWindowID(window);
  }

  // store window and context at matching indexes
  windows_.push_back(window);
  gl_contexts_.push_back(context);

  // log success with count
  LOG_INFO(get_logger(), "Spawned window '{}' ({}x{}), total={}", title, width,
           height, windows_.size());
  return true;
}

/**
 * @brief closes one tracked window and its matching context
 */
void SDL_Manager::closeWindow(std::uint32_t window_id) {
  // a linear scan is fine because window counts stay very small here
  for (std::size_t i = 0; i < windows_.size(); ++i) {
    if (!windows_[i] || SDL_GetWindowID(windows_[i]) != window_id) {
      continue;
    }

    if (window_id != 0 && window_id == first_window_id_) {
      // closing the first created window is treated as a quit request
      quit_requested_ = true;
      first_window_id_ = 0;
    }

    // destroy the context before the window that owns it
    SDL_GL_DeleteContext(gl_contexts_[i]);
    SDL_DestroyWindow(windows_[i]);
    // erase both arrays at the same index so window and context pairing stays valid
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
 * @brief swaps buffers on every active window
 */
void SDL_Manager::updateWindows() {
  // present every tracked window once per frame
  for (std::size_t i = 0; i < windows_.size(); ++i) {
    SDL_Window* window = windows_[i];
    if (window && i < gl_contexts_.size() && gl_contexts_[i]) {
      // each window must have its own context current before swapping
      if (SDL_GL_MakeCurrent(window, gl_contexts_[i]) != 0) {
        LOG_WARNING(get_logger(), "SDL_GL_MakeCurrent failed during swap: {}", SDL_GetError());
        continue;
      }
      // swap front and back buffers so the newly rendered frame becomes visible
      SDL_GL_SwapWindow(window);
    }
  }
}

/**
 * @brief legacy noop because this project no longer uses software surfaces
 */
void SDL_Manager::refreshWindowBuffer(std::uint32_t window_id) {
  // no software buffers are tracked anymore
  (void)window_id;
}

bool SDL_Manager::hasOpenGLContext() const { return !gl_contexts_.empty(); }

/**
 * @brief makes the first window context current
 * @return true when gl calls can target that context
 */
bool SDL_Manager::makeOpenGLCurrent() const {
  if (windows_.empty() || gl_contexts_.empty()) {
    // no windows means no context
    return false;
  }
  // first window acts as the default current context target
  return SDL_GL_MakeCurrent(windows_.front(), gl_contexts_.front()) == 0;
}

bool SDL_Manager::makeOpenGLCurrentAt(std::size_t index) const {
  if (index >= windows_.size() || index >= gl_contexts_.size()) {
    // index out of range
    return false;
  }
  // switch current gl state to the selected window
  return SDL_GL_MakeCurrent(windows_[index], gl_contexts_[index]) == 0;
}

SDL_Window *SDL_Manager::openGLWindow() const {
  // compatibility helper
  // first window is treated as the main one
  if (windows_.empty()) {
    return nullptr;
  }
  // return the first window
  return windows_.front();
}

/**
 * @brief returns a raw window pointer by index
 */
SDL_Window *SDL_Manager::windowAt(std::size_t index) const {
  if (index >= windows_.size()) {
    // bad index
    return nullptr;
  }
  // caller only borrows the pointer
  return windows_[index];
}

/**
 * @brief returns a software surface pointer for old code paths
 */
SDL_Surface *SDL_Manager::bufferAt(std::size_t index) const {
  // software surfaces are not used anymore so this always returns null
  (void)index;
  return nullptr;
}

/**
 * @brief returns how many windows are alive right now
 */
std::size_t SDL_Manager::windowCount() const { return windows_.size(); }

bool SDL_Manager::shouldQuit() const { return quit_requested_; }

/**
 * @brief destroys every tracked context and window during shutdown
 */
void SDL_Manager::destroyAllWindows() {
  // walk the arrays in order and tear down context first then window
  for (std::size_t i = 0; i < windows_.size(); ++i) {
    if (i < gl_contexts_.size() && gl_contexts_[i]) {
      // context must go first because it belongs to the window
      SDL_GL_DeleteContext(gl_contexts_[i]);
      gl_contexts_[i] = nullptr;
    }
    if (windows_[i]) {
      // then destroy the native window handle
      SDL_DestroyWindow(windows_[i]);
      windows_[i] = nullptr;
    }
  }
  // clear arrays so the manager is fully empty again
  windows_.clear();
  gl_contexts_.clear();
  first_window_id_ = 0;
  // no windows left means the app should be considered done
  quit_requested_ = true;
}
