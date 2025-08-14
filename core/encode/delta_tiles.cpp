#include "delta_tiles.h"

DeltaTilesEncoder::DeltaTilesEncoder(int width, int height) : width(width), height(height) {
}

DeltaTilesEncoder::~DeltaTilesEncoder() {}

void DeltaTilesEncoder::encodeFrame(const uint8_t* currentFrame, const uint8_t* previousFrame) {
    // simple placeholder: compute deltas and store raw differences (not optimized)
    computeDeltas(currentFrame, previousFrame);
    compressDeltas();
}

std::vector<uint8_t> DeltaTilesEncoder::getEncodedData() const {
    return encodedData;
}

void DeltaTilesEncoder::computeDeltas(const uint8_t* currentFrame, const uint8_t* previousFrame) {
    encodedData.clear();
    if (!currentFrame || !previousFrame) return;
    size_t pixels = static_cast<size_t>(width) * static_cast<size_t>(height) * 4;
    encodedData.reserve(pixels / 8);
    for (size_t i = 0; i < pixels; ++i) {
        uint8_t d = currentFrame[i] - previousFrame[i];
        if (d != 0) encodedData.push_back((uint8_t)i & 0xFF), encodedData.push_back(d);
    }
}

void DeltaTilesEncoder::compressDeltas() {
    // no-op placeholder; in production replace with real compression
}