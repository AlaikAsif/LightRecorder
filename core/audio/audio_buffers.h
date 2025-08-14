#ifndef AUDIO_BUFFERS_H
#define AUDIO_BUFFERS_H

#include <cstdint>
#include <vector>

class AudioBuffer {
public:
    AudioBuffer(size_t size);
    ~AudioBuffer();

    void write(const uint8_t* data, size_t size);
    void read(uint8_t* data, size_t size);
    size_t available() const;

private:
    std::vector<uint8_t> buffer;
    size_t writeIndex;
    size_t readIndex;
    size_t bufferSize;
};

#endif // AUDIO_BUFFERS_H