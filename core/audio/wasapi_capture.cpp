#include "wasapi_capture.h"
#include <audioclient.h>
#include <mmdeviceapi.h>
#include <windows.h>
#include <iostream>
#include <chrono>

#pragma comment(lib, "ole32.lib")

static uint64_t now_ms() {
    using namespace std::chrono;
    return (uint64_t)duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

WASAPICapture::WASAPICapture() : waveFormat(nullptr), capturing(false), outRing(nullptr) {}

WASAPICapture::~WASAPICapture() {
    Stop();
    if (waveFormat) CoTaskMemFree(waveFormat);
}

bool WASAPICapture::Initialize() {
    HRESULT hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    if (FAILED(hr)) return false;

    Microsoft::WRL::ComPtr<IMMDeviceEnumerator> enumerator;
    hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), NULL, CLSCTX_ALL, IID_PPV_ARGS(&enumerator));
    if (FAILED(hr)) return false;

    hr = enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &audioDevice);
    if (FAILED(hr)) return false;

    hr = audioDevice->Activate(__uuidof(IAudioClient), CLSCTX_ALL, NULL, reinterpret_cast<void**>(&audioClient));
    if (FAILED(hr)) return false;

    hr = audioClient->GetMixFormat(&waveFormat);
    if (FAILED(hr)) return false;

    // Reinitialize for loopback mode
    REFERENCE_TIME hnsBufferDuration = 10000000; // 1s
    hr = audioClient->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_LOOPBACK, hnsBufferDuration, 0, waveFormat, NULL);
    if (FAILED(hr)) return false;

    hr = audioClient->GetService(IID_PPV_ARGS(&captureClient));
    if (FAILED(hr)) return false;

    return true;
}

bool WASAPICapture::Start(SPSC_Ring<AudioPacket>* outRing) {
    if (!audioClient || !captureClient) return false;
    if (capturing.load()) return false;
    this->outRing = outRing;

    HRESULT hr = audioClient->Start();
    if (FAILED(hr)) return false;

    capturing.store(true);
    worker.reset(new std::thread(&WASAPICapture::CaptureLoop, this));
    return true;
}

void WASAPICapture::Stop() {
    if (!capturing.load()) return;
    capturing.store(false);
    if (worker && worker->joinable()) worker->join();
    if (audioClient) audioClient->Stop();
}

void WASAPICapture::CaptureLoop() {
    using namespace std::chrono;
    UINT32 packetLength = 0;
    while (capturing.load()) {
        HRESULT hr = captureClient->GetNextPacketSize(&packetLength);
        if (FAILED(hr)) break;

        if (packetLength == 0) {
            std::this_thread::sleep_for(milliseconds(10));
            continue;
        }

        BYTE* data;
        UINT32 framesAvailable;
        DWORD flags;
        hr = captureClient->GetBuffer(&data, &framesAvailable, &flags, NULL, NULL);
        if (FAILED(hr)) break;

        // Convert frames to PCM bytes as-is
        size_t bytesPerFrame = waveFormat->nBlockAlign; // e.g., 4 for stereo 16-bit
        size_t bytes = framesAvailable * bytesPerFrame;
        AudioPacket pkt;
        pkt.pts_ms = now_ms();
        pkt.data.resize(bytes);
        memcpy(pkt.data.data(), data, bytes);

        // Push to ring (drop if full)
        outRing->push(pkt);

        captureClient->ReleaseBuffer(framesAvailable);
    }
}

uint32_t WASAPICapture::getSampleRate() const { return waveFormat ? waveFormat->nSamplesPerSec : 0; }
uint16_t WASAPICapture::getChannels() const { return waveFormat ? waveFormat->nChannels : 0; }
uint16_t WASAPICapture::getBlockAlign() const { return waveFormat ? waveFormat->nBlockAlign : 0; }