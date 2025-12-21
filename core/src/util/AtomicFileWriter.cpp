#include "ijccrl/core/util/AtomicFileWriter.h"

#include <fstream>
#include <iostream>

#ifdef _WIN32
#include <windows.h>
#endif

namespace ijccrl::core::util {

namespace {
#ifdef _WIN32
std::wstring ToWide(const std::string& value) {
    if (value.empty()) {
        return {};
    }
    const int size_needed = MultiByteToWideChar(CP_UTF8, 0, value.c_str(), -1, nullptr, 0);
    std::wstring result(size_needed - 1, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, value.c_str(), -1, result.data(), size_needed);
    return result;
}
#endif
}  // namespace

bool AtomicFileWriter::Write(const std::string& path, const std::string& contents) {
    const std::string temp_path = path + ".tmp";

#ifdef _WIN32
    const std::wstring temp_path_w = ToWide(temp_path);
    const std::wstring path_w = ToWide(path);

    HANDLE file = CreateFileW(temp_path_w.c_str(),
                              GENERIC_WRITE,
                              0,
                              nullptr,
                              CREATE_ALWAYS,
                              FILE_ATTRIBUTE_NORMAL,
                              nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        std::cerr << "[atomic] Failed to open temp file: " << temp_path << '\n';
        return false;
    }

    DWORD written = 0;
    if (!WriteFile(file, contents.data(), static_cast<DWORD>(contents.size()), &written, nullptr)) {
        std::cerr << "[atomic] Failed to write temp file: " << temp_path << '\n';
        CloseHandle(file);
        return false;
    }

    if (!FlushFileBuffers(file)) {
        std::cerr << "[atomic] Failed to flush temp file: " << temp_path << '\n';
        CloseHandle(file);
        return false;
    }

    CloseHandle(file);

    if (!MoveFileExW(temp_path_w.c_str(),
                     path_w.c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        std::cerr << "[atomic] MoveFileExW failed for " << path << '\n';
        return false;
    }
#else
    std::ofstream output(temp_path, std::ios::binary | std::ios::trunc);
    if (!output) {
        std::cerr << "[atomic] Failed to open temp file: " << temp_path << '\n';
        return false;
    }
    output.write(contents.data(), static_cast<std::streamsize>(contents.size()));
    output.flush();
    if (!output) {
        std::cerr << "[atomic] Failed to write temp file: " << temp_path << '\n';
        return false;
    }
    std::remove(path.c_str());
    if (std::rename(temp_path.c_str(), path.c_str()) != 0) {
        std::cerr << "[atomic] rename failed for " << path << '\n';
        return false;
    }
#endif

    return true;
}

}  // namespace ijccrl::core::util
