#ifndef DELTA_TILES_H
#define DELTA_TILES_H

#include <cstdint>
#include <vector>

class DeltaTilesEncoder {
public:
    DeltaTilesEncoder(int width, int height);
    ~DeltaTilesEncoder();

    void encodeFrame(const uint8_t* currentFrame, const uint8_t* previousFrame);
    std::vector<uint8_t> getEncodedData() const;

private:
    int width;
    int height;
    std::vector<uint8_t> encodedData;

    void computeDeltas(const uint8_t* currentFrame, const uint8_t* previousFrame);
    void compressDeltas();
};

#endif // DELTA_TILES_H