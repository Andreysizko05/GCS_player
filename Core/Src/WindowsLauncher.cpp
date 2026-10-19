#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0602
#endif

#include <windows.h>
#include <shellapi.h>

#include <cstdio>
#include <string>
#include <vector>

namespace {

bool toUtf8(const wchar_t* value, std::string& result)
{
    const int size = WideCharToMultiByte(CP_UTF8, 0, value, -1, nullptr, 0, nullptr, nullptr);
    if (size == 0) {
        return false;
    }
    result.resize(static_cast<size_t>(size));
    if (WideCharToMultiByte(CP_UTF8, 0, value, -1, result.data(), size, nullptr, nullptr) == 0) {
        return false;
    }
    result.pop_back(); // The std::string supplies its own terminating null.
    return true;
}

int reportError(const std::wstring& message, DWORD errorCode, bool smokeTest)
{
    std::wstring details = message;
    if (errorCode != ERROR_SUCCESS) {
        details += L"\n\nWindows error " + std::to_wstring(errorCode);
        wchar_t* systemMessage = nullptr;
        if (FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM
                | FORMAT_MESSAGE_IGNORE_INSERTS,
                nullptr, errorCode, 0, reinterpret_cast<wchar_t*>(&systemMessage), 0, nullptr)) {
            details += L": ";
            details += systemMessage;
            LocalFree(systemMessage);
        }
    }

    if (smokeTest) {
        std::string utf8;
        if (toUtf8(details.c_str(), utf8)) {
            std::fprintf(stderr, "GCS_player launcher: %s\n", utf8.c_str());
        } else {
            std::fprintf(stderr, "GCS_player launcher failed (Windows error %lu).\n", errorCode);
        }
    } else {
        MessageBoxW(nullptr, details.c_str(), L"GCS_player could not start", MB_OK | MB_ICONERROR);
    }
    return 1;
}

std::wstring executablePath()
{
    std::vector<wchar_t> buffer(512);
    for (;;) {
        const DWORD size = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (size == 0) {
            return {};
        }
        if (size < buffer.size()) {
            return std::wstring(buffer.data(), size);
        }
        if (buffer.size() >= 32768) {
            SetLastError(ERROR_FILENAME_EXCED_RANGE);
            return {};
        }
        buffer.resize(buffer.size() * 2);
    }
}

} // namespace

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int)
{
    const UINT previousErrorMode = SetErrorMode(0);
    SetErrorMode(previousErrorMode | SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX
        | SEM_NOOPENFILEERRORBOX);

    int argc = 0;
    wchar_t** wideArguments = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (wideArguments == nullptr) {
        const DWORD errorCode = GetLastError();
        const bool smokeTest = std::wstring(GetCommandLineW()).find(L"--smoke-test") != std::wstring::npos;
        return reportError(L"Unable to read the application command line.", errorCode, smokeTest);
    }

    bool smokeTest = false;
    for (int index = 1; index < argc; ++index) {
        const std::wstring argument(wideArguments[index]);
        if (argument == L"--smoke-test" || argument == L"-smoke-test") {
            smokeTest = true;
        }
    }

    std::vector<std::string> arguments(static_cast<size_t>(argc));
    for (int index = 0; index < argc; ++index) {
        if (!toUtf8(wideArguments[index], arguments[static_cast<size_t>(index)])) {
            const DWORD errorCode = GetLastError();
            LocalFree(wideArguments);
            return reportError(L"Unable to convert the application command line to UTF-8.",
                errorCode, smokeTest);
        }
    }
    LocalFree(wideArguments);
    std::vector<char*> argv;
    argv.reserve(static_cast<size_t>(argc) + 1);
    for (std::string& argument : arguments) {
        argv.push_back(argument.data());
    }
    argv.push_back(nullptr);

    const std::wstring executable = executablePath();
    if (executable.empty()) {
        return reportError(L"Unable to locate the application executable.", GetLastError(), smokeTest);
    }
    const size_t separator = executable.find_last_of(L"\\/");
    if (separator == std::wstring::npos) {
        return reportError(L"The application executable has no parent directory.",
            ERROR_BAD_PATHNAME, smokeTest);
    }
    const std::wstring runtimeDirectory = executable.substr(0, separator) + L"\\runtime";
    const std::wstring applicationPath = runtimeDirectory + L"\\GCS_player_app.dll";

    // Configure the loader before any Qt or GStreamer DLL enters this process.
    // Excluding PATH and the working directory also prevents installed SDKs
    // from replacing individual libraries in the bundled runtime.
    constexpr DWORD searchDirectories = LOAD_LIBRARY_SEARCH_USER_DIRS | LOAD_LIBRARY_SEARCH_SYSTEM32;
    if (!SetDefaultDllDirectories(searchDirectories)) {
        return reportError(L"Unable to configure the Windows library search directories.",
            GetLastError(), smokeTest);
    }
    if (AddDllDirectory(runtimeDirectory.c_str()) == nullptr) {
        const DWORD errorCode = GetLastError();
        return reportError(L"Unable to register the bundled runtime directory:\n" + runtimeDirectory,
            errorCode, smokeTest);
    }
    HMODULE application = LoadLibraryExW(applicationPath.c_str(), nullptr,
        LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | searchDirectories);
    if (application == nullptr) {
        const DWORD errorCode = GetLastError();
        return reportError(L"Unable to load the application:\n" + applicationPath
                + L"\n\nKeep the runtime folder beside GCS_player.exe and use the complete release package.",
            errorCode, smokeTest);
    }

    using ApplicationEntry = int (__cdecl*)(int, char**);
    const auto applicationMain = reinterpret_cast<ApplicationEntry>(GetProcAddress(application, "gcsPlayerMain"));
    if (applicationMain == nullptr) {
        return reportError(L"The bundled application library is incompatible: gcsPlayerMain is missing.\n"
                L"Replace the application with a complete matching release package.",
            GetLastError(), smokeTest);
    }

    // Keep the module loaded until process teardown: Qt and GStreamer can still
    // own static state or worker threads after their application entry returns.
    return applicationMain(argc, argv.data());
}
