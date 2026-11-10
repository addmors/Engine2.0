#pragma once

#include <memory>
#include <string_view>

#include <spdlog/spdlog.h>

namespace engine {

// Thin logging facade over spdlog: app-facing API, spdlog stays an
// implementation detail. Call engine::Log::init() once at startup.
class Log final {
public:
    // Creates the default console logger (colored, timestamped).
    static void init(std::string_view loggerName);

    template <typename... Args>
    static void trace(spdlog::format_string_t<Args...> fmt, Args&&... args)
    {
        logger()->trace(fmt, std::forward<Args>(args)...);
    }

    template <typename... Args>
    static void info(spdlog::format_string_t<Args...> fmt, Args&&... args)
    {
        logger()->info(fmt, std::forward<Args>(args)...);
    }

    template <typename... Args>
    static void warn(spdlog::format_string_t<Args...> fmt, Args&&... args)
    {
        logger()->warn(fmt, std::forward<Args>(args)...);
    }

    template <typename... Args>
    static void error(spdlog::format_string_t<Args...> fmt, Args&&... args)
    {
        logger()->error(fmt, std::forward<Args>(args)...);
    }

    // Accesses the current application logger (rebindable via init()).
    static std::shared_ptr<spdlog::logger>& logger()
    {
        static std::shared_ptr<spdlog::logger> instance = spdlog::default_logger();
        return instance;
    }
};

} // namespace engine
