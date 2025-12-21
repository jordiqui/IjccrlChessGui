#pragma once

#include <string>

namespace ijccrl::core::util {

class AtomicFileWriter {
public:
    static bool Write(const std::string& path, const std::string& contents);
};

}  // namespace ijccrl::core::util
