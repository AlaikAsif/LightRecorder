#ifndef TIMING_H
#define TIMING_H

#include <chrono>

class Timer {
public:
    Timer() : start_time(std::chrono::high_resolution_clock::now()) {}

    void reset() {
        start_time = std::chrono::high_resolution_clock::now();
    }

    double elapsed() const {
        return std::chrono::duration<double>(std::chrono::high_resolution_clock::now() - start_time).count();
    }

private:
    std::chrono::high_resolution_clock::time_point start_time;
};

class FramePacer {
public:
    FramePacer(double target_fps) : target_time(1.0 / target_fps) {}

    void wait() {
        auto now = std::chrono::high_resolution_clock::now();
        double elapsed_time = std::chrono::duration<double>(now - last_time).count();
        if (elapsed_time < target_time) {
            std::this_thread::sleep_for(std::chrono::duration<double>(target_time - elapsed_time));
        }
        last_time = std::chrono::high_resolution_clock::now();
    }

private:
    double target_time;
    std::chrono::high_resolution_clock::time_point last_time;
};

#endif // TIMING_H