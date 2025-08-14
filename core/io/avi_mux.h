#ifndef AVI_MUX_H
#define AVI_MUX_H

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

class AVIMux {
public:
    AVIMux(const std::string& filename);
    ~AVIMux();

    bool open();
    void close();
    bool writeVideoFrame(const uint8_t* frameData, size_t frameSize);
    bool writeAudioSamples(const uint8_t* audioData, size_t audioSize);
    void setVideoParameters(uint32_t width, uint32_t height, uint32_t fps);
    void setAudioParameters(uint32_t sampleRate, uint32_t channels, uint16_t blockAlign, uint16_t bitsPerSample);

private:
    struct IndexEntry {
        uint32_t ckid; // FourCC
        uint32_t flags;
        uint32_t offset; // from 'movi' chunk start
        uint32_t size;
    };

    std::string filename_;
    FILE* out_;
    uint32_t width_;
    uint32_t height_;
    uint32_t fps_;
    uint32_t sampleRate_;
    uint16_t channels_;
    uint16_t blockAlign_;
    uint16_t bitsPerSample_;

    long riffSizePos_;
    long hdrlListPos_;
    long moviListPos_; // file offset where 'movi' data begins
    std::vector<IndexEntry> indexEntries_;

    void writeHeadersPlaceholder();
    void finalizeHeaders();
    uint32_t writeChunk(const char fourcc[4], const void* data, uint32_t size);
};

#endif // AVI_MUX_H