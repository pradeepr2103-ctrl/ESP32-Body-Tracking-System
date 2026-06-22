// MiniCsv.h
//
// CSV read/write for MotionRecorder recordings. Simpler and more portable
// than JSON for this use case -- opens directly in Excel/Sheets/Numbers,
// trivial to diff two takes by eye, and trivial to hand-edit a glitchy
// frame.
//
// File layout:
//   Row 1: header
//     t,s0_w,s0_x,s0_y,s0_z,s1_w,s1_x,s1_y,s1_z,...,s9_w,s9_x,s9_y,s9_z
//   Row 2+: one frame per row
//     0.000000,1.000000,0.000000,0.000000,0.000000,...
//
// numSensors is fixed at kNumSensors (10) for this project, so the column
// count is always 1 + 10*4 = 41.

#pragma once

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "MotionRecorder.h"

namespace mocap::csv {

inline bool writeRecording(const std::string& filepath,
                            const std::vector<Frame>& frames,
                            int numSensors) {
    std::ofstream out(filepath, std::ios::out | std::ios::trunc);
    if (!out.is_open()) return false;

    // Header
    out << "t";
    for (int s = 0; s < numSensors; ++s) {
        out << ",s" << s << "_w,s" << s << "_x,s" << s << "_y,s" << s << "_z";
    }
    out << "\n";

    char buf[96];
    for (const Frame& f : frames) {
        out << f.timestamp;
        for (int s = 0; s < numSensors; ++s) {
            const Quat& q = f.q[s];
            std::snprintf(buf, sizeof(buf), ",%.6f,%.6f,%.6f,%.6f", q.w, q.x, q.y, q.z);
            out << buf;
        }
        out << "\n";
    }

    return true;
}

// Splits a CSV line on commas. No quoted-field handling -- not needed
// since every field here is a plain number.
inline std::vector<std::string> splitLine(const std::string& line) {
    std::vector<std::string> fields;
    std::stringstream ss(line);
    std::string item;
    while (std::getline(ss, item, ',')) {
        fields.push_back(item);
    }
    return fields;
}

inline bool readRecording(const std::string& filepath,
                           std::vector<Frame>& outFrames,
                           int expectedNumSensors) {
    std::ifstream in(filepath);
    if (!in.is_open()) return false;

    outFrames.clear();

    std::string line;
    if (!std::getline(in, line)) return false; // header line (discarded;
                                                 // column order is fixed
                                                 // by writeRecording())

    const size_t expectedCols = 1 + static_cast<size_t>(expectedNumSensors) * 4;

    while (std::getline(in, line)) {
        if (line.empty()) continue;
        std::vector<std::string> fields = splitLine(line);
        if (fields.size() != expectedCols) {
            // Skip malformed/truncated rows (e.g. a half-written last line)
            // rather than failing the whole load.
            continue;
        }

        Frame frame;
        try {
            frame.timestamp = std::stod(fields[0]);
            for (int s = 0; s < expectedNumSensors; ++s) {
                size_t base = 1 + static_cast<size_t>(s) * 4;
                Quat q;
                q.w = std::stof(fields[base + 0]);
                q.x = std::stof(fields[base + 1]);
                q.y = std::stof(fields[base + 2]);
                q.z = std::stof(fields[base + 3]);
                // Normalize on read: protects against hand-edited CSVs
                // (the format is intentionally editable in Excel/Sheets)
                // where a manually typed value won't be exactly
                // unit-length, and against accumulated float rounding
                // from the fixed 6-decimal text representation.
                q = q.normalized();
                frame.q[s] = q;
            }
        } catch (const std::exception&) {
            continue; // skip rows with unparseable numbers
        }

        outFrames.push_back(frame);
    }

    return !outFrames.empty();
}

} // namespace mocap::csv
