#pragma once

#include <spdlog/spdlog.h>
#include <spdlog/fmt/fmt.h>
#include <memory>
#include <string>

namespace kv {
   class Logger {
   public:
    static void init(const std::string& level_str);

    static void shutdown();

    static std::shared_ptr<spdlog::logger>& get_instance() noexcept;

   private:
    static std::shared_ptr<spdlog::logger> s_logger;
};

}

#define KV_LOG_TRACE(...)    ::kv::Logger::get_instance()->trace(__VA_ARGS__)
#define KV_LOG_DEBUG(...)    ::kv::Logger::get_instance()->debug(__VA_ARGS__)
#define KV_LOG_INFO(...)     ::kv::Logger::get_instance()->info(__VA_ARGS__)
#define KV_LOG_WARN(...)     ::kv::Logger::get_instance()->warn(__VA_ARGS__)
#define KV_LOG_ERROR(...)    ::kv::Logger::get_instance()->error(__VA_ARGS__)
#define KV_LOG_CRITICAL(...) ::kv::Logger::get_instance()->critical(__VA_ARGS__)