# llm-server: a small OpenAI-style chat server on the llama.cpp library (Metal on Apple Silicon).
#   cmake -S llm -B build/llm -DCMAKE_BUILD_TYPE=Release && cmake --build build/llm -j
# install.sh does this and copies the binary to bin/.
cmake_minimum_required(VERSION 3.16)
project(llm-server C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if (NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

set(LLAMA_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../third_party/llama.cpp" CACHE PATH "llama.cpp source tree")

# Only the llama + ggml libraries: no tools, examples, tests, server, curl or OpenSSL.
set(BUILD_SHARED_LIBS     OFF CACHE BOOL "" FORCE)
set(LLAMA_BUILD_COMMON    OFF CACHE BOOL "" FORCE)
set(LLAMA_BUILD_TESTS     OFF CACHE BOOL "" FORCE)
set(LLAMA_BUILD_TOOLS     OFF CACHE BOOL "" FORCE)
set(LLAMA_BUILD_EXAMPLES  OFF CACHE BOOL "" FORCE)
set(LLAMA_BUILD_SERVER    OFF CACHE BOOL "" FORCE)
set(LLAMA_BUILD_APP       OFF CACHE BOOL "" FORCE)
set(LLAMA_OPENSSL         OFF CACHE BOOL "" FORCE)
set(LLAMA_CURL            OFF CACHE BOOL "" FORCE)
set(GGML_METAL_EMBED_LIBRARY ON CACHE BOOL "" FORCE) # one self-contained binary

add_subdirectory(${LLAMA_DIR} llama EXCLUDE_FROM_ALL)

add_executable(llm-server llm-server.cpp ${LLAMA_DIR}/vendor/cpp-httplib/httplib.cpp)
target_include_directories(llm-server PRIVATE ${LLAMA_DIR}/vendor)
target_compile_definitions(llm-server PRIVATE CPPHTTPLIB_FORM_URL_ENCODED_PAYLOAD_MAX_LENGTH=1048576)
find_package(Threads REQUIRED)
target_link_libraries(llm-server PRIVATE llama Threads::Threads)
