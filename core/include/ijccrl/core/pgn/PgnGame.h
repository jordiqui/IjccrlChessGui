#pragma once

#include <string>
#include <utility>
#include <vector>

namespace ijccrl::core::pgn {

struct PgnTag {
    std::string key;
    std::string value;
};

struct PgnGame {
    std::vector<PgnTag> tags;
    std::vector<std::string> moves;
    std::string result = "*";

    void SetTag(const std::string& key, const std::string& value) {
        for (auto& tag : tags) {
            if (tag.key == key) {
                tag.value = value;
                return;
            }
        }
        tags.push_back({key, value});
    }
};

}  // namespace ijccrl::core::pgn
