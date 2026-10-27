#pragma once

#include <chrono>

struct Timer final {
    void start() { m_startTime = std::chrono::high_resolution_clock::now(); }
    void end() { m_endTime = std::chrono::high_resolution_clock::now(); }
    int64_t ms() { return std::chrono::duration_cast<std::chrono::milliseconds>(m_endTime - m_startTime).count(); }

    std::chrono::time_point<std::chrono::high_resolution_clock> m_startTime{};
    std::chrono::time_point<std::chrono::high_resolution_clock> m_endTime{};
};
