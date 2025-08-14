#ifndef WRITER_H
#define WRITER_H

#include <cstdint>
#include <cstdio>
#include <string>

class Writer {
public:
    Writer(const std::string& filename);
    ~Writer();

    bool open();
    void close();
    bool writeFrame(const uint8_t* frameData, size_t size);
    void setBufferSize(size_t size);

private:
    std::string filename;
    FILE* fileHandle;
    size_t bufferSize;
    uint8_t* buffer;
    size_t bufferPos;
};

#endif // WRITER_H