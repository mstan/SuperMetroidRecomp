cmake_minimum_required(VERSION 3.16)

project(SuperMetroidSNESRecompAndroid)

# SDL2 source lives in ./SDL (pinned copy fetched by android/fetch_sdl.sh; it
# is not committed). Builds libSDL2.so and exports the SDL2::SDL2 target the
# game links against.
add_subdirectory(SDL)

# The game itself: the repo-root CMakeLists builds the runner + generated
# banks + SM host as libmain.so when ANDROID is set (SDLActivity loads
# "libmain.so" and calls its SDL_main).
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../../.. sm)
