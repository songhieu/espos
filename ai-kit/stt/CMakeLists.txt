# stt-server: OpenAI-style speech-to-text on the whisper.cpp library (Metal on Apple Silicon).
#   cmake -S stt -B build/stt -DCMAKE_BUILD_TYPE=Release && cmake --build build/stt -j
# install.sh does this and copies the binary to bin/.
# It is a separate build from llm/: llama.cpp and whisper.cpp each bring their own copy of ggml.
cmake_minimum_required(VERSION 3.16)
project(stt-server C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if (NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

set(WHISPER_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../third_party/whisper.cpp" CACHE PATH "whisper.cpp source tree")

set(BUILD_SHARED_LIBS      OFF CACHE BOOL "" FORCE)
set(WHISPER_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
set(WHISPER_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(WHISPER_BUILD_SERVER   OFF CACHE BOOL "" FORCE)
set(WHISPER_CURL           OFF CACHE BOOL "" FORCE)
set(WHISPER_SDL2           OFF CACHE BOOL "" FORCE)
set(GGML_METAL_EMBED_LIBRARY ON CACHE BOOL "" FORCE) # one self-contained binary

add_subdirectory(${WHISPER_DIR} whisper EXCLUDE_FROM_ALL)

add_executable(stt-server stt-server.cpp)
# cpp-httplib and nlohmann/json, as vendored by whisper.cpp's own examples
target_include_directories(stt-server PRIVATE ${WHISPER_DIR}/examples/server ${WHISPER_DIR}/examples)
find_package(Threads REQUIRED)
target_link_libraries(stt-server PRIVATE whisper Threads::Threads)
