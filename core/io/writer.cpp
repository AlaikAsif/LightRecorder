#include "writer.h"
#include <fstream>
#include <vector>
#include <mutex>
#include <cstring>

class DiskWriter {
public:
    DiskWriter(const std::string& filename);
    ~DiskWriter();
    
    bool open();
    void close();
    void writeFrame(const std::vector<uint8_t>& frameData);
    
private:
    std::ofstream fileStream;
    std::string filename;
    std::vector<uint8_t> buffer;
    size_t bufferSize;
    size_t currentSize;

    void flushBuffer();
};

DiskWriter::DiskWriter(const std::string& filename)
    : filename(filename), bufferSize(16 * 1024 * 1024), currentSize(0) {
    buffer.resize(bufferSize);
}

DiskWriter::~DiskWriter() {
    close();
}

bool DiskWriter::open() {
    fileStream.open(filename, std::ios::binary);
    return fileStream.is_open();
}

void DiskWriter::close() {
    flushBuffer();
    if (fileStream.is_open()) {
        fileStream.close();
    }
}

void DiskWriter::writeFrame(const std::vector<uint8_t>& frameData) {
    if (currentSize + frameData.size() > bufferSize) {
        flushBuffer();
    }
    std::copy(frameData.begin(), frameData.end(), buffer.begin() + currentSize);
    currentSize += frameData.size();
}

void DiskWriter::flushBuffer() {
    if (currentSize > 0) {
        fileStream.write(reinterpret_cast<char*>(buffer.data()), currentSize);
        currentSize = 0;
    }
}

Writer::Writer(const std::string& filename)
    : filename(filename), fileHandle(nullptr), bufferSize(8 * 1024 * 1024), buffer(nullptr), bufferPos(0) {
    buffer = (uint8_t*)malloc(bufferSize);
}

Writer::~Writer() {
    close();
    if (buffer) free(buffer);
}

bool Writer::open() {
    fileHandle = fopen(filename.c_str(), "wb");
    return fileHandle != nullptr;
}

void Writer::close() {
    if (!fileHandle) return;
    if (bufferPos > 0) fwrite(buffer, 1, bufferPos, fileHandle);
    fclose(fileHandle);
    fileHandle = nullptr;
    bufferPos = 0;
}

bool Writer::writeFrame(const uint8_t* frameData, size_t size) {
    if (!fileHandle) return false;
    if (size > bufferSize) {
        // write directly if single frame larger than buffer
        if (bufferPos > 0) {
            fwrite(buffer, 1, bufferPos, fileHandle);
            bufferPos = 0;
        }
        fwrite(frameData, 1, size, fileHandle);
        return true;
    }

    if (bufferPos + size > bufferSize) {
        fwrite(buffer, 1, bufferPos, fileHandle);
        bufferPos = 0;
    }

    memcpy(buffer + bufferPos, frameData, size);
    bufferPos += size;
    return true;
}

void Writer::setBufferSize(size_t size) {
    if (buffer) free(buffer);
    buffer = (uint8_t*)malloc(size);
    bufferSize = size;
    bufferPos = 0;
}