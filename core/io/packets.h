#ifndef PACKETS_H
#define PACKETS_H

#include <vector>
#include <cstdint>

struct VideoPacket {
    std::vector<uint8_t> data;
    uint64_t pts_ms; // presentation timestamp in milliseconds
};

struct AudioPacket {
    std::vector<uint8_t> data;
    uint64_t pts_ms; // timestamp when captured (ms since epoch)
};

#endif // PACKETS_H
