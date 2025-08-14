#include "mjpeg.h"
#include <windows.h>
#include <gdiplus.h>
#include <vector>
#include <memory>
#include <mutex>

#pragma comment(lib, "gdiplus.lib")

using namespace Gdiplus;

static std::once_flag gdiPlusInitFlag;
static ULONG_PTR gdiPlusToken = 0;

static void ensureGdiplusInit() {
    std::call_once(gdiPlusInitFlag, []() {
        GdiplusStartupInput gdiplusStartupInput;
        GdiplusStartup(&gdiPlusToken, &gdiplusStartupInput, NULL);
    });
}

#ifdef HAVE_TURBOJPEG
#include <turbojpeg.h>
#endif

// Helper to find encoder CLSID for JPEG
static int getEncoderClsid(const WCHAR* format, CLSID* pClsid) {
    UINT num = 0;          // number of image encoders
    UINT size = 0;         // size of the image encoder array in bytes

    ImageCodecInfo* pImageCodecInfo = NULL;
    GetImageEncodersSize(&num, &size);
    if (size == 0) return -1;  // Failure

    pImageCodecInfo = (ImageCodecInfo*)(malloc(size));
    if (!pImageCodecInfo) return -1;

    GetImageEncoders(num, size, pImageCodecInfo);

    for (UINT j = 0; j < num; ++j) {
        if (wcscmp(pImageCodecInfo[j].MimeType, format) == 0 || wcscmp(pImageCodecInfo[j].FormatDescription, format) == 0) {
            *pClsid = pImageCodecInfo[j].Clsid;
            free(pImageCodecInfo);
            return j; // success
        }
    }

    free(pImageCodecInfo);
    return -1; // not found
}

MJPEGEncoder::MJPEGEncoder(int width, int height)
    : width(width), height(height), quality(75)
#ifdef HAVE_TURBOJPEG
    , turboHandle(nullptr)
#endif
{
#ifdef HAVE_TURBOJPEG
    // initialize turbojpeg handle
    turboHandle = tjInitCompress();
    if (!turboHandle) {
        // fallback to GDI+ if init fails
        turboHandle = nullptr;
    }
#endif
    ensureGdiplusInit();
}

MJPEGEncoder::~MJPEGEncoder() {
#ifdef HAVE_TURBOJPEG
    if (turboHandle) {
        tjDestroy((tjhandle)turboHandle);
        turboHandle = nullptr;
    }
#endif
    // Intentionally do not shutdown GDI+ here; let process exit handle it
}

void MJPEGEncoder::setQuality(int q) {
    if (q < 1) q = 1;
    if (q > 100) q = 100;
    quality = q;
}

void MJPEGEncoder::encodeFrame(const uint8_t* frameData, std::vector<uint8_t>& outputBuffer) {
#ifdef HAVE_TURBOJPEG
    if (turboHandle) {
        // TurboJPEG expects RGB or BGR input. Our frames are BGRA (32bpp), so provide pitch and pixel format.
        int pixelSize = 3; // we'll pass BGRX by using TJPF_BGRX when available
        unsigned char* compressedBuf = nullptr;
        unsigned long compressedSize = 0;

        // Use tjCompress2 with TJPF_BGRX if available; else convert to BGR buffer
#ifdef TJPF_BGRX
        int pixelFormat = TJPF_BGRX;
        // tjCompress2 supports padded formats; pass width*4 as pitch
        int pad = 0; // let lib handle
        int flags = 0;
        int subsamp = TJSAMP_420; // MJPEG typical subsampling
        int err = tjCompress2((tjhandle)turboHandle,
                              frameData, // srcBuf
                              width,
                              width * 4, // pitch
                              height,
                              pixelFormat,
                              &compressedBuf,
                              &compressedSize,
                              subsamp,
                              quality,
                              flags);
        if (err == 0 && compressedBuf && compressedSize > 0) {
            outputBuffer.resize(compressedSize);
            memcpy(outputBuffer.data(), compressedBuf, compressedSize);
            tjFree(compressedBuf);
            return;
        }
        // else fall through to GDI+ fallback
#else
        // No TJPF_BGRX defined; convert BGRA->BGR temporary buffer
        std::vector<uint8_t> bgrbuf(width * height * 3);
        uint8_t* dst = bgrbuf.data();
        const uint8_t* src = frameData;
        for (int y = 0; y < height; ++y) {
            const uint8_t* row = src + y * width * 4;
            for (int x = 0; x < width; ++x) {
                // BGRA -> BGR
                dst[0] = row[0];
                dst[1] = row[1];
                dst[2] = row[2];
                dst += 3;
                row += 4;
            }
        }
        int err = tjCompress2((tjhandle)turboHandle,
                              bgrbuf.data(),
                              width,
                              0,
                              height,
                              TJPF_BGR,
                              &compressedBuf,
                              &compressedSize,
                              TJSAMP_420,
                              quality,
                              0);
        if (err == 0 && compressedBuf && compressedSize > 0) {
            outputBuffer.resize(compressedSize);
            memcpy(outputBuffer.data(), compressedBuf, compressedSize);
            tjFree(compressedBuf);
            return;
        }
        // else fall through
#endif
    }
#endif

    // Fallback to GDI+ encoder if turbojpeg not present or failed
    ensureGdiplusInit();

    // Create Gdiplus Bitmap from raw BGRA data
    Bitmap bitmap(width, height, PixelFormat32bppARGB);

    BitmapData bd;
    Rect lockRect(0, 0, width, height);
    if (bitmap.LockBits(&lockRect, ImageLockModeWrite, PixelFormat32bppARGB, &bd) != Ok) {
        return;
    }

    // bd.Stride may be padded; copy per-line
    uint8_t* dest = static_cast<uint8_t*>(bd.Scan0);
    int destStride = bd.Stride;
    const uint8_t* src = frameData;
    int srcStride = width * 4;
    for (int y = 0; y < height; ++y) {
        memcpy(dest + y * destStride, src + y * srcStride, srcStride);
    }

    bitmap.UnlockBits(&bd);

    // Prepare encoder CLSID for JPEG
    CLSID clsid;
    // Use MIME-type "image/jpeg" for lookup
    if (getEncoderClsid(L"image/jpeg", &clsid) < 0) return;

    // Create an in-memory IStream
    IStream* stream = nullptr;
    if (CreateStreamOnHGlobal(NULL, TRUE, &stream) != S_OK) return;

    // Set quality parameter
    ULONG qualityVal = (ULONG)quality;
    EncoderParameters encoderParams;
    encoderParams.Count = 1;
    EncoderParameter param;
    param.Guid = EncoderQuality;
    param.NumberOfValues = 1;
    param.Type = EncoderParameterValueTypeLong;
    param.Value = &qualityVal;
    encoderParams.Parameter[0] = param;

    Status st = bitmap.Save(stream, &clsid, &encoderParams);
    if (st != Ok) {
        stream->Release();
        return;
    }

    // Get HGLOBAL and size
    HGLOBAL hGlobal = NULL;
    if (GetHGlobalFromStream(stream, &hGlobal) != S_OK) {
        stream->Release();
        return;
    }

    SIZE_T size = GlobalSize(hGlobal);
    void* data = GlobalLock(hGlobal);
    if (!data) {
        GlobalUnlock(hGlobal);
        stream->Release();
        return;
    }

    outputBuffer.resize(size);
    memcpy(outputBuffer.data(), data, size);

    GlobalUnlock(hGlobal);
    stream->Release();
}