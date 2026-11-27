# worktool_js: quickjs-ng(MIT) + wtjs.c 를 묶은 규칙 엔진 라이브러리
cmake_minimum_required(VERSION 3.18)
project(worktool_js LANGUAGES C)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
include(FetchContent)
FetchContent_Declare(quickjs_ng
  URL https://github.com/quickjs-ng/quickjs/archive/refs/tags/v0.18.0.tar.gz
  URL_HASH SHA256=28bcd89aa3683fd2a946537e5ce3f11cc46b5cbf3383c7561aaef3df5ff9f1dd
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
  # 소스만 받아요. quickjs-ng 자체의 CMake 빌드는 쓰지 않아요 (없는 하위 폴더를 가리켜 건너뛰어요).
  SOURCE_SUBDIR _sources_only)
FetchContent_MakeAvailable(quickjs_ng)
set(Q ${quickjs_ng_SOURCE_DIR})
add_library(worktool_js SHARED wtjs.c ${Q}/quickjs.c ${Q}/libregexp.c ${Q}/libunicode.c ${Q}/dtoa.c)
target_include_directories(worktool_js PRIVATE ${Q})
target_compile_definitions(worktool_js PRIVATE _GNU_SOURCE)
if(WIN32)
  target_compile_definitions(worktool_js PRIVATE WIN32_LEAN_AND_MEAN _WIN32_WINNT=0x0601)
endif()
if(MSVC)
  target_compile_options(worktool_js PRIVATE /O2 /W0 /experimental:c11atomics)
else()
  target_compile_options(worktool_js PRIVATE -O2 -w -funsigned-char)
  target_link_libraries(worktool_js PRIVATE m)
endif()
