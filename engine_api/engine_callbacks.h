#pragma once
#include <functional>
#include <string>

namespace acutesim {

enum class LogLevel { DEBUG, INFO, WARN, ERROR };

struct EngineCallbacks {
    std::function<void(float)>                  progress        = [](float){};
    std::function<bool()>                       cancelRequested = []{ return false; };
    std::function<void(LogLevel, std::string)>  log             = [](LogLevel, std::string){};
};

} // namespace acutesim
