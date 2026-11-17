# The declarative face/widget renderer (src/inkdesk/Decl.cpp) on a computer, drawn with the X4's own
# fonts: FaceRenderTest (gtest) and face_render, a tool that turns a package into a PNG.
# Needs zlib (PNG output) and ArduinoJson (the copy PlatformIO fetches for the firmware, or one installed on
# the computer). Without them only these targets are skipped; the other suites still configure.
find_package(ZLIB QUIET)
find_path(FACE_ARDUINOJSON ArduinoJson.h PATHS ${REPO_ROOT}/.pio/libdeps/default/ArduinoJson/src NO_DEFAULT_PATH)
if(NOT FACE_ARDUINOJSON)
  find_path(FACE_ARDUINOJSON ArduinoJson.h)
endif()
if(NOT ZLIB_FOUND OR NOT FACE_ARDUINOJSON)
  message(STATUS "face_render: skipped (needs zlib and ArduinoJson; run `pio run` once to fetch ArduinoJson)")
  return()
endif()

add_library(face_render_core STATIC
  HostCanvas.cpp
  stubs/FontAlloc.cpp
  stubs/uzlib_checksums.c
  ${REPO_ROOT}/src/inkdesk/Decl.cpp
  ${REPO_ROOT}/lib/Utf8/Utf8.cpp
  ${REPO_ROOT}/lib/EpdFont/EpdFont.cpp
  ${REPO_ROOT}/lib/EpdFont/FontDecompressor.cpp
  ${REPO_ROOT}/lib/InflateReader/InflateReader.cpp
  ${REPO_ROOT}/lib/uzlib/src/tinflate.c
)
target_include_directories(face_render_core PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/stubs
  ${REPO_ROOT}/src
  ${REPO_ROOT}/lib
  ${REPO_ROOT}/lib/EpdFont
  ${REPO_ROOT}/lib/Utf8
  ${REPO_ROOT}/lib/InflateReader
  ${REPO_ROOT}/lib/uzlib/src
  ${REPO_ROOT}/freeink-sdk/libs/font/FreeInkFont/include
  ${FACE_ARDUINOJSON}
)
target_link_libraries(face_render_core PUBLIC ZLIB::ZLIB)

add_executable(face_render face_render.cpp)
target_link_libraries(face_render PRIVATE face_render_core)

add_executable(FaceRenderTest FaceRenderTest.cpp)
target_compile_definitions(FaceRenderTest PRIVATE FACE_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")
target_link_libraries(FaceRenderTest PRIVATE face_render_core GTest::gtest_main)
gtest_discover_tests(FaceRenderTest)
