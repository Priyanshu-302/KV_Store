#include "common/logger.hpp"
#include <spdlog/sinks/stdout_color_sinks.h>
#include <algorithm>
#include <iostream>

namespace kv {
  // Define the static logger instance
  std::shared_ptr<spdlog::logger> Logger::s_logger;

  void Logger::init(const std::string& level_str) {
    try {
      std::string lower_level = level_str;
      std::transform(lower_level.begin(), lower_level.end(), lower_level.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
      });

      spdlog::level::level_enum level = spdlog::level::info;
      if (lower_level == "trace")         level = spdlog::level::trace;
        else if (lower_level == "debug")    level = spdlog::level::debug;
        else if (lower_level == "info")     level = spdlog::level::info;
        else if (lower_level == "warn")     level = spdlog::level::warn;
        else if (lower_level == "err" || lower_level == "error") level = spdlog::level::err;
        else if (lower_level == "critical") level = spdlog::level::critical;
        else if (lower_level == "off")      level = spdlog::level::off;

        // Create coloured stdout sink
        auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();

        // Custom pattern with microseconds
        console_sink->set_pattern("[%Y-%m-%d %H:%M:%S.%f] [tid: %t] [%^%l%$] %v");

        s_logger = std::make_shared<spdlog::logger>("kv_server", console_sink);
        s_logger->set_level(level);
        s_logger->flush_on(spdlog::level::warn);

        spdlog::set_default_logger(s_logger);
    } catch (const spdlog::spdlog_ex& ex) {
        std::cerr << "Fatal: Logger initialization failed: " << ex.what() << std::endl;
        std::exit(EXIT_FAILURE);
    }
  }

  void Logger::shutdown() {
    if (s_logger) {
        s_logger->flush();
    }

    spdlog::shutdown();
  }

  std::shared_ptr<spdlog::logger>& Logger::get_instance() noexcept {
    return s_logger;
}

}