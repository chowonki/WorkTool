#include <windows.h>
#include <strsafe.h>
#include <wchar.h>

static int show_error(const wchar_t *message)
{
    MessageBoxW(NULL, message, L"WorkTool", MB_OK | MB_ICONERROR);
    return 1;
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE previous, PWSTR arguments, int show)
{
    wchar_t root[32768];
    wchar_t app_directory[32768];
    wchar_t app_path[32768];
    wchar_t data_path[32768];
    wchar_t command_line[32768];
    DWORD length = GetModuleFileNameW(NULL, root, ARRAYSIZE(root));
    if (length == 0 || length >= ARRAYSIZE(root))
        return show_error(L"WorkTool 실행 파일의 위치를 확인할 수 없습니다.");

    wchar_t *separator = wcsrchr(root, L'\\');
    if (separator == NULL)
        return show_error(L"WorkTool 실행 파일의 위치를 확인할 수 없습니다.");
    separator[1] = L'\0';

    if (FAILED(StringCchPrintfW(app_directory, ARRAYSIZE(app_directory), L"%lsApp", root)) ||
        FAILED(StringCchPrintfW(app_path, ARRAYSIZE(app_path), L"%ls\\WorkTool.WinUI.exe", app_directory)) ||
        FAILED(StringCchPrintfW(data_path, ARRAYSIZE(data_path), L"%lsData", root)))
        return show_error(L"경로가 너무 깁니다. ZIP을 더 짧은 경로에 풀어 주세요.");

    if (!CreateDirectoryW(data_path, NULL)) {
        DWORD attributes = GetFileAttributesW(data_path);
        if (attributes == INVALID_FILE_ATTRIBUTES || !(attributes & FILE_ATTRIBUTE_DIRECTORY))
            return show_error(L"Data 폴더를 만들 수 없습니다. ZIP을 쓰기 가능한 폴더에 풀어 주세요.");
    }

    BOOL wait_for_app = arguments != NULL && *arguments != L'\0';
    if (wait_for_app && FAILED(StringCchPrintfW(command_line, ARRAYSIZE(command_line), L"\"%ls\" %ls", app_path, arguments)))
        return show_error(L"실행 인자가 너무 깁니다.");

    STARTUPINFOW startup = { sizeof(startup) };
    PROCESS_INFORMATION process;
    if (!CreateProcessW(app_path, wait_for_app ? command_line : NULL, NULL, NULL, FALSE, 0, NULL, app_directory, &startup, &process))
        return show_error(L"WorkTool을 시작할 수 없습니다. ZIP 전체를 푼 뒤 다시 실행해 주세요.");

    DWORD result = 0;
    if (wait_for_app && (WaitForSingleObject(process.hProcess, INFINITE) != WAIT_OBJECT_0 ||
        !GetExitCodeProcess(process.hProcess, &result)))
        result = 1;
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return (int)result;
}
