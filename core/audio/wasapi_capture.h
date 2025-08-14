#ifndef WASAPI_CAPTURE_H
#define WASAPI_CAPTURE_H

#include <windows.h>
#include <audioclient.h>
#include <mmdeviceapi.h>
#include <wrl.h>
#include <vector>
#include <thread>
#include <atomic>

#include "../util/spsc_ring.h"
#include "../io/packets.h"

class WASAPICapture {
public:
    WASAPICapture();
    ~WASAPICapture();

    // Initialize COM and audio device
    bool Initialize();

    // Start loopback capture and push AudioPacket into outRing
    bool Start(SPSC_Ring<AudioPacket>* outRing);
    void Stop();

    // Audio format info (valid after Initialize)
    uint32_t getSampleRate() const;
    uint16_t getChannels() const;
    uint16_t getBlockAlign() const;

private:
    Microsoft::WRL::ComPtr<IMMDevice> audioDevice;
    Microsoft::WRL::ComPtr<IAudioClient> audioClient;
    Microsoft::WRL::ComPtr<IAudioCaptureClient> captureClient;
    WAVEFORMATEX* waveFormat;
    std::atomic<bool> capturing;
    std::unique_ptr<std::thread> worker;
    SPSC_Ring<AudioPacket>* outRing;

    void CaptureLoop();
};

#endif // WASAPI_CAPTURE_H