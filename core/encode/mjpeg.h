#ifndef MJPEG_H
#define MJPEG_H

#include <cstdint>
#include <vector>

class MJPEGEncoder {
public:
    MJPEGEncoder(int width, int height);
    ~MJPEGEncoder();

    void encodeFrame(const uint8_t* frameData, std::vector<uint8_t>& outputBuffer);
    void setQuality(int quality);
    
private:
    void initialize();
    void cleanup();
    void convertToYUV420(const uint8_t* bgra, uint8_t* yuv);
    void performDCT(const uint8_t* block, uint8_t* output);
    void quantize(const uint8_t* dctBlock, uint8_t* output);
    void huffmanEncode(const uint8_t* quantizedBlock, std::vector<uint8_t>& outputBuffer);

    int width;
    int height;
    int quality;

#ifdef HAVE_TURBOJPEG
    // turbojpeg handle for fast encoding
    struct tjhandle_struct; // forward decl (opaque)
    void* turboHandle;
#endif
    // Additional private members for internal state management
};

#endif // MJPEG_H