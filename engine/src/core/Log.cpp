#include "engine/core/Log.hpp"

#include <utility>

#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

namespace engine {

void Log::init(std::string_view loggerName)
{
    auto consoleSink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
    consoleSink->set_pattern("[%H:%M:%S.%e] [%^%l%$] %v");

    auto appLogger = std::make_shared<spdlog::logger>(std::string{loggerName}, std::move(consoleSink));
    appLogger->set_level(spdlog::level::debug);

    spdlog::set_default_logger(appLogger);
    spdlog::set_level(spdlog::level::debug);

    logger() = std::move(appLogger);
}

} // namespace engine
