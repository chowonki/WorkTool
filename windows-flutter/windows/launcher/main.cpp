// 포터블 배포본 맨 위에 놓는 시작 프로그램이에요.
//
//   WorkTool\
//     WorkTool.exe   <- 이 프로그램. App\WorkTool.exe 를 대신 실행해요.
//     App\           <- 앱 파일 (Flutter 엔진, DLL, data)
//     Data\          <- 설정·오늘 기록
//
// 인자가 없으면 앱을 띄우고 바로 끝나요. 인자가 있으면(--smoke-test 등)
// 앱이 끝날 때까지 기다렸다가 종료 코드를 그대로 돌려줘요.
#include <windows.h>

#include <shlwapi.h>

#include <string>

namespace {

const wchar_t kTitle[] = L"WorkTool";

int Fail(const std::wstring& message) {
  ::MessageBoxW(nullptr, message.c_str(), kTitle, MB_OK | MB_ICONERROR);
  return EXIT_FAILURE;
}

// 이 실행 파일이 있는 폴더 (끝에 \ 없음).
std::wstring ExeDir() {
  std::wstring path(MAX_PATH, L'\0');
  for (;;) {
    DWORD n = ::GetModuleFileNameW(nullptr, &path[0], static_cast<DWORD>(path.size()));
    if (n == 0) return L"";
    if (n < path.size()) {
      path.resize(n);
      break;
    }
    path.resize(path.size() * 2);
  }
  size_t slash = path.find_last_of(L"\\/");
  return slash == std::wstring::npos ? L"" : path.substr(0, slash);
}

}  // namespace

int APIENTRY wWinMain(_In_ HINSTANCE, _In_opt_ HINSTANCE, _In_ wchar_t*, _In_ int show_command) {
  const std::wstring dir = ExeDir();
  const std::wstring app = dir + L"\\App\\WorkTool.exe";
  if (dir.empty() || ::GetFileAttributesW(app.c_str()) == INVALID_FILE_ATTRIBUTES) {
    return Fail(L"App 폴더의 WorkTool.exe를 찾지 못했어요.\n"
                L"압축을 푼 폴더 전체(App 폴더 포함)가 그대로 있는지 확인해 주세요.");
  }

  // 받은 인자를 그대로 넘겨요 (첫 토큰인 실행 파일 이름만 바꿔요).
  const wchar_t* args = ::PathGetArgsW(::GetCommandLineW());
  const bool has_args = args != nullptr && *args != L'\0';
  std::wstring command = L"\"" + app + L"\"";
  if (has_args) {
    command += L" ";
    command += args;
    // 명령 창에서 실행했으면 앱의 출력이 그 창에 보이게 해요.
    ::AttachConsole(ATTACH_PARENT_PROCESS);
  }

  STARTUPINFOW si = {};
  si.cb = sizeof(si);
  si.dwFlags = STARTF_USESHOWWINDOW;
  si.wShowWindow = static_cast<WORD>(show_command);
  HANDLE in = ::GetStdHandle(STD_INPUT_HANDLE);
  HANDLE out = ::GetStdHandle(STD_OUTPUT_HANDLE);
  HANDLE err = ::GetStdHandle(STD_ERROR_HANDLE);
  const bool has_std = (out != nullptr && out != INVALID_HANDLE_VALUE) ||
                       (err != nullptr && err != INVALID_HANDLE_VALUE);
  if (has_std) {
    // 출력이 파일·파이프로 연결돼 있으면 앱에도 이어 줘요.
    si.dwFlags |= STARTF_USESTDHANDLES;
    si.hStdInput = in;
    si.hStdOutput = out;
    si.hStdError = err;
  }

  PROCESS_INFORMATION pi = {};
  if (!::CreateProcessW(app.c_str(), &command[0], nullptr, nullptr, has_std ? TRUE : FALSE, 0,
                        nullptr, nullptr, &si, &pi)) {
    return Fail(L"WorkTool을 시작하지 못했어요. (오류 " + std::to_wstring(::GetLastError()) + L")");
  }

  DWORD code = EXIT_SUCCESS;
  if (has_args) {
    ::WaitForSingleObject(pi.hProcess, INFINITE);
    ::GetExitCodeProcess(pi.hProcess, &code);
  }
  ::CloseHandle(pi.hThread);
  ::CloseHandle(pi.hProcess);
  return static_cast<int>(code);
}
