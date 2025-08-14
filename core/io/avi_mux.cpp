#include "avi_mux.h"
#include <cstdio>
#include <cstring>
#include <vector>
#include <iostream>

static inline void write_u32_le(FILE* f, uint32_t v) {
    uint8_t b[4];
    b[0] = v & 0xFF;
    b[1] = (v >> 8) & 0xFF;
    b[2] = (v >> 16) & 0xFF;
    b[3] = (v >> 24) & 0xFF;
    fwrite(b, 1, 4, f);
}

static inline void write_u16_le(FILE* f, uint16_t v) {
    uint8_t b[2];
    b[0] = v & 0xFF;
    b[1] = (v >> 8) & 0xFF;
    fwrite(b, 1, 2, f);
}

AVIMux::AVIMux(const std::string& filename)
    : filename_(filename), out_(nullptr), width_(0), height_(0), fps_(30),
      sampleRate_(0), channels_(0), blockAlign_(0), bitsPerSample_(16),
      riffSizePos_(0), hdrlListPos_(0), moviListPos_(0) {
}

AVIMux::~AVIMux() {
    close();
}

bool AVIMux::open() {
    out_ = fopen(filename_.c_str(), "wb");
    if (!out_) return false;
    writeHeadersPlaceholder();
    return true;
}

void AVIMux::close() {
    if (!out_) return;
    finalizeHeaders();
    fclose(out_);
    out_ = nullptr;
}

void AVIMux::setVideoParameters(uint32_t width, uint32_t height, uint32_t fps) {
    width_ = width; height_ = height; fps_ = fps;
}

void AVIMux::setAudioParameters(uint32_t sampleRate, uint32_t channels, uint16_t blockAlign, uint16_t bitsPerSample) {
    sampleRate_ = sampleRate; channels_ = channels; blockAlign_ = blockAlign; bitsPerSample_ = bitsPerSample;
}

uint32_t AVIMux::writeChunk(const char fourcc[4], const void* data, uint32_t size) {
    // Align to WORD boundary
    uint32_t start = (uint32_t)ftell(out_);
    fwrite(fourcc, 1, 4, out_);
    write_u32_le(out_, size);
    if (size > 0 && data) fwrite(data, 1, size, out_);
    if (size % 2 == 1) fputc(0, out_);
    return start;
}

void AVIMux::writeHeadersPlaceholder() {
    // RIFF header
    fwrite("RIFF", 1, 4, out_);
    riffSizePos_ = ftell(out_);
    write_u32_le(out_, 0); // placeholder for RIFF size
    fwrite("AVI ", 1, 4, out_);

    // LIST hdrl
    fwrite("LIST", 1, 4, out_);
    hdrlListPos_ = ftell(out_);
    write_u32_le(out_, 0); // hdrl size placeholder
    fwrite("hdrl", 1, 4, out_);

    // avih: main AVI header (56 bytes)
    uint8_t avih[56]; memset(avih, 0, sizeof(avih));
    uint32_t microSecPerFrame = (fps_ > 0) ? (1000000u / fps_) : 33333u; // default ~30fps
    uint32_t streams = (sampleRate_ > 0) ? 2u : 1u;
    uint32_t suggestedBuf = (width_ && height_) ? (width_ * height_ * 3 / 2) : 0u;

    // dwMicroSecPerFrame
    memcpy(avih + 0, &microSecPerFrame, 4);
    // dwMaxBytesPerSec (leave 0)
    // dwPaddingGranularity (leave 0)
    // dwFlags (leave 0)
    // dwTotalFrames (0 - unknown)
    // dwInitialFrames (0)
    memcpy(avih + 24, &streams, 4); // dwStreams
    memcpy(avih + 28, &suggestedBuf, 4); // dwSuggestedBufferSize
    if (width_) memcpy(avih + 32, &width_, 4);
    if (height_) memcpy(avih + 36, &height_, 4);

    const char avihFourcc[4] = {'a','v','i','h'};
    writeChunk(avihFourcc, avih, sizeof(avih));

    // Write video stream LIST 'strl'
    // We'll create strl, write strh and strf, then backpatch the strl size immediately.
    long strlPos = ftell(out_);
    fwrite("LIST", 1, 4, out_);
    long strlSizePos = ftell(out_);
    write_u32_le(out_, 0);
    fwrite("strl", 1, 4, out_);

    // strh for video
    uint8_t strh_vid[56]; memset(strh_vid, 0, sizeof(strh_vid));
    // fccType 'vids'
    strh_vid[0] = 'v'; strh_vid[1] = 'i'; strh_vid[2] = 'd'; strh_vid[3] = 's';
    // fccHandler 'MJPG'
    strh_vid[4] = 'M'; strh_vid[5] = 'J'; strh_vid[6] = 'P'; strh_vid[7] = 'G';
    // dwFlags = 0
    uint16_t wPriority = 0; memcpy(strh_vid + 12, &wPriority, 2);
    uint16_t wLanguage = 0; memcpy(strh_vid + 14, &wLanguage, 2);
    uint32_t dwInitialFrames = 0; memcpy(strh_vid + 16, &dwInitialFrames, 4);
    uint32_t dwScale = 1; memcpy(strh_vid + 20, &dwScale, 4);
    uint32_t dwRate = fps_; memcpy(strh_vid + 24, &dwRate, 4);
    uint32_t dwStart = 0; memcpy(strh_vid + 28, &dwStart, 4);
    uint32_t dwLength = 0; memcpy(strh_vid + 32, &dwLength, 4);
    uint32_t dwSuggested = suggestedBuf; memcpy(strh_vid + 36, &dwSuggested, 4);
    uint32_t dwQuality = 0xFFFFFFFF; memcpy(strh_vid + 40, &dwQuality, 4);
    uint32_t dwSampleSize = 0; memcpy(strh_vid + 44, &dwSampleSize, 4);
    // rcFrame (left, top, right, bottom)
    int16_t left = 0, top = 0, right = (int16_t)width_, bottom = (int16_t)height_;
    memcpy(strh_vid + 48, &left, 2); memcpy(strh_vid + 50, &top, 2);
    memcpy(strh_vid + 52, &right, 2); memcpy(strh_vid + 54, &bottom, 2);

    const char strhFourcc[4] = {'s','t','r','h'};
    writeChunk(strhFourcc, strh_vid, sizeof(strh_vid));

    // strf for video (BITMAPINFOHEADER)
    uint8_t bi[40]; memset(bi, 0, sizeof(bi));
    uint32_t biSize = 40; memcpy(bi + 0, &biSize, 4);
    uint32_t biWidth = width_; memcpy(bi + 4, &biWidth, 4);
    uint32_t biHeight = height_; memcpy(bi + 8, &biHeight, 4);
    uint16_t biPlanes = 1; memcpy(bi + 12, &biPlanes, 2);
    uint16_t biBitCount = 24; memcpy(bi + 14, &biBitCount, 2);
    // biCompression 'MJPG'
    bi[16] = 'M'; bi[17] = 'J'; bi[18] = 'P'; bi[19] = 'G';
    uint32_t biSizeImage = 0; memcpy(bi + 20, &biSizeImage, 4);
    uint32_t biXPelsPerMeter = 0; memcpy(bi + 24, &biXPelsPerMeter, 4);
    uint32_t biYPelsPerMeter = 0; memcpy(bi + 28, &biYPelsPerMeter, 4);
    uint32_t biClrUsed = 0; memcpy(bi + 32, &biClrUsed, 4);
    uint32_t biClrImportant = 0; memcpy(bi + 36, &biClrImportant, 4);

    const char strfFourcc[4] = {'s','t','r','f'};
    writeChunk(strfFourcc, bi, sizeof(bi));

    // Backpatch this strl size
    long afterStrl = ftell(out_);
    fseek(out_, strlSizePos, SEEK_SET);
    write_u32_le(out_, (uint32_t)((afterStrl - strlPos) - 8));
    fseek(out_, afterStrl, SEEK_SET);

    // If audio parameters are set, write audio stream
    if (sampleRate_ > 0 && channels_ > 0 && blockAlign_ > 0) {
        long astrlPos = ftell(out_);
        fwrite("LIST", 1, 4, out_);
        long astrlSizePos = ftell(out_);
        write_u32_le(out_, 0);
        fwrite("strl", 1, 4, out_);

        // strh for audio
        uint8_t strh_aud[56]; memset(strh_aud, 0, sizeof(strh_aud));
        // fccType 'auds'
        strh_aud[0] = 'a'; strh_aud[1] = 'u'; strh_aud[2] = 'd'; strh_aud[3] = 's';
        // fccHandler zeros
        uint32_t aud_dwFlags = 0; memcpy(strh_aud + 8, &aud_dwFlags, 4);
        uint16_t aud_wPriority = 0; memcpy(strh_aud + 12, &aud_wPriority, 2);
        uint16_t aud_wLanguage = 0; memcpy(strh_aud + 14, &aud_wLanguage, 2);
        uint32_t aud_dwInitialFrames = 0; memcpy(strh_aud + 16, &aud_dwInitialFrames, 4);
        uint32_t aud_dwScale = blockAlign_; memcpy(strh_aud + 20, &aud_dwScale, 4);
        uint32_t aud_dwRate = (uint32_t)sampleRate_ * (uint32_t)blockAlign_; memcpy(strh_aud + 24, &aud_dwRate, 4);
        uint32_t aud_dwStart = 0; memcpy(strh_aud + 28, &aud_dwStart, 4);
        uint32_t aud_dwLength = 0; memcpy(strh_aud + 32, &aud_dwLength, 4);
        uint32_t aud_dwSuggested = (sampleRate_ * blockAlign_) / 10; memcpy(strh_aud + 36, &aud_dwSuggested, 4);
        uint32_t aud_dwQuality = 0xFFFFFFFF; memcpy(strh_aud + 40, &aud_dwQuality, 4);
        uint32_t aud_dwSampleSize = blockAlign_; memcpy(strh_aud + 44, &aud_dwSampleSize, 4);
        // rcFrame unused for audio

        writeChunk(strhFourcc, strh_aud, sizeof(strh_aud));

        // strf for audio (WAVEFORMATEX)
        // wFormatTag(2), nChannels(2), nSamplesPerSec(4), nAvgBytesPerSec(4), nBlockAlign(2), wBitsPerSample(2), cbSize(2)
        uint8_t wf[18]; memset(wf, 0, sizeof(wf));
        uint16_t wFormatTag = 1; // PCM
        memcpy(wf + 0, &wFormatTag, 2);
        memcpy(wf + 2, &channels_, 2);
        memcpy(wf + 4, &sampleRate_, 4);
        uint32_t avgBytes = sampleRate_ * blockAlign_; memcpy(wf + 8, &avgBytes, 4);
        memcpy(wf + 12, &blockAlign_, 2);
        memcpy(wf + 14, &bitsPerSample_, 2);
        uint16_t cbSize = 0; memcpy(wf + 16, &cbSize, 2);

        writeChunk(strfFourcc, wf, sizeof(wf));

        long afterAStrl = ftell(out_);
        fseek(out_, astrlSizePos, SEEK_SET);
        write_u32_le(out_, (uint32_t)((afterAStrl - astrlPos) - 8));
        fseek(out_, afterAStrl, SEEK_SET);
    }

    // Start movi list
    fwrite("LIST", 1, 4, out_);
    moviListPos_ = ftell(out_);
    write_u32_le(out_, 0); // movi size placeholder
    fwrite("movi", 1, 4, out_);
}

void AVIMux::finalizeHeaders() {
    long fileEnd = ftell(out_);

    // write idx1
    long idx1Pos = ftell(out_);
    fwrite("idx1", 1, 4, out_);
    uint32_t idx1Size = (uint32_t)(indexEntries_.size() * 16);
    write_u32_le(out_, idx1Size);
    for (auto &e : indexEntries_) {
        write_u32_le(out_, e.ckid);
        write_u32_le(out_, e.flags);
        write_u32_le(out_, e.offset);
        write_u32_le(out_, e.size);
    }

    long finalPos = ftell(out_);

    // Backpatch RIFF size
    fseek(out_, riffSizePos_, SEEK_SET);
    write_u32_le(out_, (uint32_t)(finalPos - 8));

    // Backpatch hdrl list size
    fseek(out_, hdrlListPos_, SEEK_SET);
    write_u32_le(out_, (uint32_t)((moviListPos_ - hdrlListPos_) - 8));

    // Backpatch movi list size
    uint32_t moviSize = (uint32_t)(idx1Pos - (moviListPos_));
    fseek(out_, moviListPos_ - 4, SEEK_SET);
    write_u32_le(out_, moviSize);

    fseek(out_, finalPos, SEEK_SET);
}

bool AVIMux::writeVideoFrame(const uint8_t* frameData, size_t frameSize) {
    if (!out_) return false;
    const char fourcc[4] = {'0','0','d','c'};
    uint32_t pos = writeChunk(fourcc, frameData, (uint32_t)frameSize);

    IndexEntry ie;
    ie.ckid = 0x63646F30; // '00dc' little-endian
    ie.flags = 0x10; // keyframe
    ie.offset = pos - moviListPos_;
    ie.size = (uint32_t)frameSize;
    indexEntries_.push_back(ie);
    return true;
}

bool AVIMux::writeAudioSamples(const uint8_t* audioData, size_t audioSize) {
    if (!out_) return false;
    const char fourcc[4] = {'0','1','w','b'};
    uint32_t pos = writeChunk(fourcc, audioData, (uint32_t)audioSize);

    IndexEntry ie;
    ie.ckid = 0x62773130; // '01wb'
    ie.flags = 0;
    ie.offset = pos - moviListPos_;
    ie.size = (uint32_t)audioSize;
    indexEntries_.push_back(ie);
    return true;
}