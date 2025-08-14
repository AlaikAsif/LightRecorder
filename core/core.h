#ifndef CORE_H
#define CORE_H

#include "capture/gdi_capture.h"
#include "capture/hook_present.h"
#include "encode/mjpeg.h"
#include "encode/delta_tiles.h"
#include "io/avi_mux.h"
#include "io/writer.h"
#include "audio/wasapi_capture.h"
#include "util/spsc_ring.h"
#include "util/timing.h"
#include "util/arena_alloc.h"
#include "io/packets.h"

#include <thread>
#include <atomic>
#include <vector>

class Core {
public:
    Core();
    ~Core();

    // Initialize core subsystems. width/height in pixels, fps 30/60
    bool initialize(int width, int height, int fps = 30);

    // Start capture/encode/write pipeline, provide output filename for AVI
    bool start(const std::string& outFilename);

    // Stop pipeline and flush
    void stop();

private:
    // pipeline components
    GDICapture* gdiCapture;
    HookPresent* hookPresent; // optional high-end path (may be null)
    MJPEGEncoder* mjpegEncoder;
    AVIMux* aviMux;
    WASAPICapture* audioCapture;

    // Lock-free rings
    SPSC_Ring<int>* captureToEncodeRing;                // indices of frame buffers
    SPSC_Ring<VideoPacket>* encodeToWriterRing;         // encoded JPEG frames with pts
    SPSC_Ring<AudioPacket>* audioRing;                  // raw PCM audio chunks with pts

    // Threads
    std::thread encoderThread;
    std::thread writerThread;

    std::atomic<bool> running;

    // Internal thread funcs
    void encoderLoop();
    void writerLoop();

    // configuration
    int cfgWidth;
    int cfgHeight;
    int cfgFps;
    size_t cfgBufferCount;
};

#endif // CORE_H