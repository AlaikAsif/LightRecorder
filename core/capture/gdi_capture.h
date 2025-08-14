#ifndef GDI_CAPTURE_H
#define GDI_CAPTURE_H

#include <windows.h>
#include <cstdint>
#include <vector>
#include <atomic>
#include <memory>
#include <thread>

#include "../util/spsc_ring.h"

class GDICapture {
public:
    // width/height in pixels, fps default 30, bufferCount default 4 (power of two recommended)
    GDICapture(int width, int height, int fps = 30, size_t bufferCount = 4);
    ~GDICapture();

    // Initialize resources (DCs, bitmaps)
    bool Initialize();

    // Start capture thread; outRing receives indices of filled buffers (indices are 0..bufferCount-1)
    bool Start(SPSC_Ring<int>* outRing);

    // Stop capture thread and return when complete
    void Stop();

    // Access buffer by index (read-only consumer view)
    const uint8_t* getFrameBuffer(size_t index) const;
    size_t getFrameSize() const;

    // Runtime FPS control (safe to call from other threads)
    void setFps(int newFps);
    int getFps() const;

private:
    void CaptureLoop();

    HDC hdcScreen;
    HDC hdcMem;
    HBITMAP hBitmap;

    int width;
    int height;
    std::atomic<int> fps;
    size_t bufferCount;
    size_t frameSize;

    std::vector<std::vector<uint8_t>> buffers;
    SPSC_Ring<int>* outRing;

    std::atomic<bool> running;
    std::unique_ptr<std::thread> worker;
};

#endif // GDI_CAPTURE_H