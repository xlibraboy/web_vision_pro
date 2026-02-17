#pragma once

#include <cstdint>

/**
 * RawFormat - Binary file format for Raw Chunk Recording (.bin)
 * Optimized for maximum write speed (direct memory dump) and data integrity.
 */

// Magic number to identify our raw files: "PAPR" (PaperVision)
constexpr char RAW_FILE_MAGIC[4] = {'P', 'A', 'P', 'R'};

// Current format version
constexpr uint32_t RAW_FILE_VERSION = 1;

/**
 * File Header (1024 bytes)
 * Located at the very beginning of the .bin file.
 */
struct RawFileHeader {
    char magic[4];          // "PAPR"
    uint32_t version;       // 1
    uint32_t width;         // Image width
    uint32_t height;        // Image height
    uint32_t pixelFormat;   // 0 = Mono8, 1 = BGR8, 2 = RGB8
    double fps;             // Recording FPS
    uint32_t totalFrames;   // Total frames in this file
    uint32_t triggerIndex;  // Index of the trigger frame (t=0)
    char reserved[984];     // Padding to exactly 1024 bytes (40 bytes used + 4 padding for double alignment)
};

/**
 * Frame Metadata Chunk (64 bytes)
 * Appended immediately after each raw image frame.
 */
struct FrameMetadata {
    uint64_t timestamp;     // Unix timestamp (microseconds)
    uint64_t frameId;       // Frame counter
    uint32_t flags;         // 0 = Normal, 1 = Trigger Frame
    char reserved[44];      // Padding to exactly 64 bytes
};

// Compile-time size checks
static_assert(sizeof(RawFileHeader) == 1024, "RawFileHeader must be 1024 bytes");
static_assert(sizeof(FrameMetadata) == 64, "FrameMetadata must be 64 bytes");
