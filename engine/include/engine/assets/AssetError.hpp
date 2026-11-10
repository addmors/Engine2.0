#pragma once

#include <string>

namespace engine {

// Error information for asset loading failures (used with std::expected).
struct AssetError {
    enum class Kind {
        FileNotFound,
        ParseFailed,
        Unsupported,
        Unknown,
    };

    Kind kind = Kind::Unknown;
    std::string message;
};

} // namespace engine
