#include "gdi_capture.h"
#include <windows.h>
#include <chrono>
#include <iostream>

GDICapture::GDICapture(int width, int height, int fps, size_t bufferCount)
    : hdcScreen(NULL), hdcMem(NULL), hBitmap(NULL), width(width), height(height), fps(fps), bufferCount(bufferCount), outRing(nullptr), running(false) {
    frameSize = static_cast<size_t>(width) * static_cast<size_t>(height) * 4; // BGRA
}

GDICapture::~GDICapture() {
    Stop();
    if (hBitmap) DeleteObject(hBitmap);
    if (hdcMem) DeleteDC(hdcMem);
    if (hdcScreen) ReleaseDC(NULL, hdcScreen);
}

bool GDICapture::Initialize() {
    hdcScreen = GetDC(NULL);
    if (!hdcScreen) return false;
    hdcMem = CreateCompatibleDC(hdcScreen);
    if (!hdcMem) return false;
    hBitmap = CreateCompatibleBitmap(hdcScreen, width, height);
    if (!hBitmap) return false;
    SelectObject(hdcMem, hBitmap);

    buffers.resize(bufferCount);
    for (size_t i = 0; i < bufferCount; ++i) buffers[i].resize(frameSize);

    return true;
}

bool GDICapture::Start(SPSC_Ring<int>* outRing) {
    if (!hdcScreen || !hdcMem || !hBitmap) return false;
    if (running.load()) return false;
    this->outRing = outRing;
    running.store(true);
    worker.reset(new std::thread(&GDICapture::CaptureLoop, this));
    return true;
}

void GDICapture::Stop() {
    if (!running.load()) return;
    running.store(false);
    if (worker && worker->joinable()) worker->join();
}

const uint8_t* GDICapture::getFrameBuffer(size_t index) const {
    if (index >= buffers.size()) return nullptr;
    return buffers[index].data();
}

size_t GDICapture::getFrameSize() const { return frameSize; }

void GDICapture::setFps(int newFps) {
    if (newFps < 1) newFps = 1;
    fps.store(newFps);
}

int GDICapture::getFps() const { return fps.load(); }

void GDICapture::CaptureLoop() {
    using namespace std::chrono;
    size_t writeIndex = 0;

    while (running.load()) {
        auto start = high_resolution_clock::now();

        int currentFps = fps.load();
        auto frameInterval = milliseconds(1000 / std::max(1, currentFps));

        HDC hdcTarget = GetDC(NULL);
        BitBlt(hdcMem, 0, 0, width, height, hdcTarget, 0, 0, SRCCOPY | CAPTUREBLT);
        // Copy bits from HBITMAP to buffer
        BITMAPINFO bmi;
        ZeroMemory(&bmi, sizeof(bmi));
        bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bmi.bmiHeader.biWidth = width;
        bmi.bmiHeader.biHeight = -height; // top-down
        bmi.bmiHeader.biPlanes = 1;
        bmi.bmiHeader.biBitCount = 32;
        bmi.bmiHeader.biCompression = BI_RGB;

        if (!GetDIBits(hdcMem, hBitmap, 0, height, buffers[writeIndex].data(), &bmi, DIB_RGB_COLORS)) {
            // failed to read bits
        }

        ReleaseDC(NULL, hdcTarget);

        // Push index to ring; if ring full drop this frame (advance writeIndex)
        if (outRing) {
            if (!outRing->push((int)writeIndex)) {
                // buffer full: drop oldest behavior handled by consumer; here we just skip pushing
            }
        }

        writeIndex = (writeIndex + 1) % bufferCount;

        auto elapsed = high_resolution_clock::now() - start;
        if (elapsed < frameInterval) std::this_thread::sleep_for(frameInterval - elapsed);
    }
}