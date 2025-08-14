#include "core.h"
#include "capture/gdi_capture.h"
#include "capture/hook_present.h"
#include "encode/mjpeg.h"
#include "audio/wasapi_capture.h"
#include "io/writer.h"
#include "io/avi_mux.h"
#include "util/timing.h"
#include "util/arena_alloc.h"
#include <chrono>
#include <iostream>

static uint64_t now_ms() {
    using namespace std::chrono;
    return (uint64_t)duration_cast<milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

// Implementation of Core (was previously ScreenRecorder)
Core::Core()
    : gdiCapture(nullptr), hookPresent(nullptr), mjpegEncoder(nullptr), aviMux(nullptr), audioCapture(nullptr),
      captureToEncodeRing(nullptr), encodeToWriterRing(nullptr), audioRing(nullptr), running(false),
      cfgWidth(1280), cfgHeight(720), cfgFps(30), cfgBufferCount(4) {}

Core::~Core() {
    stop();
}

bool Core::initialize(int width, int height, int fps) {
    cfgWidth = width;
    cfgHeight = height;
    cfgFps = fps;

    // allocate rings and components
    captureToEncodeRing = new SPSC_Ring<int>(cfgBufferCount * 2);
    encodeToWriterRing = new SPSC_Ring<VideoPacket>(32);
    audioRing = new SPSC_Ring<AudioPacket>(64);

    gdiCapture = new GDICapture(cfgWidth, cfgHeight, cfgFps, cfgBufferCount);
    if (!gdiCapture->Initialize()) return false;

    mjpegEncoder = new MJPEGEncoder(cfgWidth, cfgHeight);

    audioCapture = new WASAPICapture();
    if (!audioCapture->Initialize()) {
        std::cerr << "Failed to initialize WASAPI capture" << std::endl;
        // audio optional; continue without audio
        delete audioCapture; audioCapture = nullptr;
    }

    // Do not open AVI mux here; open when start() is called with filename

    return true;
}

bool Core::start(const std::string& outFilename) {
    if (running.load()) return false;

    aviMux = new AVIMux(outFilename.c_str());
    if (!aviMux->open()) {
        std::cerr << "Failed to open AVI mux output file" << std::endl;
        delete aviMux; aviMux = nullptr;
        return false;
    }

    if (audioCapture) {
        aviMux->setAudioParameters(audioCapture->getSampleRate(), audioCapture->getChannels(), audioCapture->getBlockAlign(), 16);
    }
    aviMux->setVideoParameters(cfgWidth, cfgHeight, cfgFps);

    running.store(true);

    // Start capturing frames and audio
    if (!gdiCapture->Start(captureToEncodeRing)) {
        std::cerr << "Failed to start GDI capture" << std::endl;
        running.store(false);
        return false;
    }

    if (audioCapture) {
        audioCapture->Start(audioRing);
    }

    // start encoder and writer threads
    encoderThread = std::thread(&Core::encoderLoop, this);
    writerThread = std::thread(&Core::writerLoop, this);

    // Start a monitor thread to observe queue fill and perform fallback logic
    std::thread([this]() {
        using namespace std::chrono;
        const double highThreshold = 0.75; // 75%
        const double lowThreshold = 0.25;  // 25%
        milliseconds highDuration(800);
        milliseconds lowDuration(5000);

        auto highStart = steady_clock::time_point();
        auto lowStart = steady_clock::time_point();

        bool currentlyLowered = (cfgFps <= 30);

        while (running.load()) {
            double fill = captureToEncodeRing->fillFactor();

            auto now = steady_clock::now();
            if (fill >= highThreshold) {
                if (highStart == steady_clock::time_point()) highStart = now;
                if (!currentlyLowered && now - highStart >= highDuration) {
                    std::cout << "Queue >75% for 800ms, lowering FPS to 30" << std::endl;
                    gdiCapture->setFps(30);
                    currentlyLowered = true;
                }
            } else {
                highStart = steady_clock::time_point();
            }

            if (fill <= lowThreshold) {
                if (lowStart == steady_clock::time_point()) lowStart = now;
                if (currentlyLowered && now - lowStart >= lowDuration) {
                    // Only recover to original higher fps if original cfgFps was higher
                    if (cfgFps > 30) {
                        std::cout << "Queue <25% for 5s, restoring FPS to " << cfgFps << std::endl;
                        gdiCapture->setFps(cfgFps);
                        currentlyLowered = false;
                    }
                }
            } else {
                lowStart = steady_clock::time_point();
            }

            std::this_thread::sleep_for(milliseconds(100));
        }
    }).detach();

    return true;
}

void Core::stop() {
    if (!running.load()) return;
    running.store(false);

    if (gdiCapture) gdiCapture->Stop();
    if (audioCapture) audioCapture->Stop();
    if (encoderThread.joinable()) encoderThread.join();
    if (writerThread.joinable()) writerThread.join();

    if (aviMux) {
        aviMux->close();
        delete aviMux; aviMux = nullptr;
    }

    if (mjpegEncoder) { delete mjpegEncoder; mjpegEncoder = nullptr; }
    if (gdiCapture) { delete gdiCapture; gdiCapture = nullptr; }
    if (captureToEncodeRing) { delete captureToEncodeRing; captureToEncodeRing = nullptr; }
    if (encodeToWriterRing) { delete encodeToWriterRing; encodeToWriterRing = nullptr; }
    if (audioRing) { delete audioRing; audioRing = nullptr; }
    if (audioCapture) { delete audioCapture; audioCapture = nullptr; }
}

void Core::encoderLoop() {
    int index;
    while (running.load()) {
        if (captureToEncodeRing->pop(index)) {
            const uint8_t* frame = gdiCapture->getFrameBuffer((size_t)index);
            if (frame) {
                VideoPacket pkt;
                pkt.pts_ms = now_ms();
                mjpegEncoder->encodeFrame(frame, pkt.data);
                // push to writer ring (try until space or shutdown)
                while (running.load() && !encodeToWriterRing->push(pkt)) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                }
            }
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
}

void Core::writerLoop() {
    VideoPacket v;
    AudioPacket a;

    // simple interleave based on pts_ms
    while (running.load()) {
        bool haveV = encodeToWriterRing->pop(v);
        bool haveA = audioRing && audioRing->pop(a);

        if (haveV && haveA) {
            if (v.pts_ms <= a.pts_ms) {
                aviMux->writeVideoFrame(v.data.data(), v.data.size());
                // push back audio to ring for next iteration
                audioRing->push(a);
            } else {
                aviMux->writeAudioSamples(a.data.data(), a.data.size());
                // push back video
                encodeToWriterRing->push(v);
            }
            continue;
        }

        if (haveV) {
            aviMux->writeVideoFrame(v.data.data(), v.data.size());
            continue;
        }
        if (haveA) {
            aviMux->writeAudioSamples(a.data.data(), a.data.size());
            continue;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    while (audioRing && audioRing->pop(a)) {
        aviMux->writeAudioSamples(a.data.data(), a.data.size());
    }
    while (encodeToWriterRing->pop(v)) {
        aviMux->writeVideoFrame(v.data.data(), v.data.size());
    }
}