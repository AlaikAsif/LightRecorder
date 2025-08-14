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

    // avih: main AVI header (fixed 56 bytes)
    // We'll write a zero-filled avih block for now and backpatch later if needed
    const char avihFourcc[4] = {'a','v','i','h'};
    uint8_t avih[56]; memset(avih, 0, sizeof(avih));
    writeChunk(avihFourcc, avih, sizeof(avih));

    // TODO: write strl for video and audio (stream headers and format blocks)

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