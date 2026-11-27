# 포터블 배포본 맨 위의 WorkTool.exe (App\WorkTool.exe 를 실행하는 작은 시작 프로그램).
# 결과: build/windows/x64/launcher/<Config>/WorkTool.exe
cmake_minimum_required(VERSION 3.15)
project(worktool_launcher LANGUAGES CXX)

# App 폴더 밖에서 혼자 실행되므로 C 런타임을 안에 넣어요 (옆에 DLL이 없어도 돼요).
# 이 폴더에서만 /MD → /MT 로 바꿔요. (위쪽 project()가 CMP0091 이전 방식이라
# MSVC_RUNTIME_LIBRARY 속성은 듣지 않아요.)
foreach(config DEBUG PROFILE RELEASE)
  string(REPLACE "/MD" "/MT" CMAKE_CXX_FLAGS_${config} "${CMAKE_CXX_FLAGS_${config}}")
endforeach()

add_executable(worktool_launcher WIN32
  "main.cpp"
  # 아이콘·버전 정보는 앱과 같은 것을 써요.
  "../runner/Runner.rc"
)
set_target_properties(worktool_launcher PROPERTIES OUTPUT_NAME "${BINARY_NAME}")
target_compile_features(worktool_launcher PRIVATE cxx_std_17)
target_compile_options(worktool_launcher PRIVATE /W4 /WX /utf-8 /EHsc)
target_compile_definitions(worktool_launcher PRIVATE
  "NOMINMAX"
  "FLUTTER_VERSION=\"${FLUTTER_VERSION}\""
  "FLUTTER_VERSION_MAJOR=${FLUTTER_VERSION_MAJOR}"
  "FLUTTER_VERSION_MINOR=${FLUTTER_VERSION_MINOR}"
  "FLUTTER_VERSION_PATCH=${FLUTTER_VERSION_PATCH}"
  "FLUTTER_VERSION_BUILD=${FLUTTER_VERSION_BUILD}"
)
target_link_libraries(worktool_launcher PRIVATE "shlwapi.lib")
