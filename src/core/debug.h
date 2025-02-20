#pragma once

#include <optional>
//#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

namespace wb {
class Log {
  public:
    std::optional<spdlog::logger> logger;

    Log(const std::string& logger_name) {
        /*auto file_sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
            "logs/imdev-log.txt", 1048576, 3, false, spdlog::file_event_handlers{});*/
        auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
        logger.emplace(logger_name, spdlog::sinks_init_list{console_sink});
        logger->set_level(spdlog::level::trace);
    }

    ~Log() {}

    template <typename... Args>
    static void trace(spdlog::format_string_t<Args...> str, Args&&... args) {
        g_main_logger.logger->trace(str, std::forward<Args>(args)...);
    }

    template <typename... Args>
    static void debug(spdlog::format_string_t<Args...> str, Args&&... args) {
        g_main_logger.logger->debug(str, std::forward<Args>(args)...);
    }

    template <typename... Args>
    static void info(spdlog::format_string_t<Args...> str, Args&&... args) {
        g_main_logger.logger->info(str, std::forward<Args>(args)...);
    }

    template <typename... Args>
    static void warn(spdlog::format_string_t<Args...> str, Args&&... args) {
        g_main_logger.logger->warn(str, std::forward<Args>(args)...);
    }

    template <typename... Args>
    static void error(spdlog::format_string_t<Args...> str, Args&&... args) {
        g_main_logger.logger->error(str, std::forward<Args>(args)...);
    }

    template <typename... Args>
    static void critical(spdlog::format_string_t<Args...> str, Args&&... args) {
        g_main_logger.logger->critical(str, std::forward<Args>(args)...);
    }

  private:
    static Log g_main_logger;
};

static void report_check(const char* expr_str, const char* file, const char* func,
                         uint32_t line_number) {
    Log::error("Check failed in {} at {} function {}: ", file, line_number, func, expr_str);
    std::abort();
}
} // namespace wb

#define WB_CHECK(x) (void)((!!(x)) || (report_check(#x, __FILE__, __func__, __LINE__), 0))
