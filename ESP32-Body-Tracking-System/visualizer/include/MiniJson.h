// MiniJson.h
//
// Tiny, schema-specific JSON read/write for MotionRecorder's recording
// format. Not a general-purpose JSON library on purpose -- keeps the
// whole project buildable with zero extra dependencies beyond OpenGL/
// GLFW/Assimp. Swap for nlohmann/json if you'd prefer; the format is
// plain enough that either round-trips identically.

#pragma once

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "MotionRecorder.h"

namespace mocap::json {

inline bool writeRecording(const std::string& filepath,
                            const std::vector<Frame>& frames,
                            int numSensors) {
    std::ofstream out(filepath, std::ios::out | std::ios::trunc);
    if (!out.is_open()) return false;

    out << "{\n";
    out << "  \"format\": \"esp32_mocap_recording\",\n";
    out << "  \"version\": 1,\n";
    out << "  \"numSensors\": " << numSensors << ",\n";
    out << "  \"frameCount\": " << frames.size() << ",\n";
    out << "  \"frames\": [\n";

    char buf[256];
    for (size_t i = 0; i < frames.size(); ++i) {
        const Frame& f = frames[i];
        out << "    {\"t\": " << f.timestamp << ", \"q\": [";
        for (int s = 0; s < numSensors; ++s) {
            const Quat& q = f.q[s];
            std::snprintf(buf, sizeof(buf), "[%.6f,%.6f,%.6f,%.6f]",
                          q.w, q.x, q.y, q.z);
            out << buf;
            if (s + 1 < numSensors) out << ",";
        }
        out << "]}";
        if (i + 1 < frames.size()) out << ",";
        out << "\n";
    }

    out << "  ]\n";
    out << "}\n";
    return true;
}

inline bool readRecording(const std::string& filepath,
                           std::vector<Frame>& outFrames,
                           int expectedNumSensors) {
    std::ifstream in(filepath);
    if (!in.is_open()) return false;

    std::stringstream ss;
    ss << in.rdbuf();
    std::string text = ss.str();

    outFrames.clear();

    size_t pos = text.find("\"frames\"");
    if (pos == std::string::npos) return false;

    size_t cursor = pos;
    const std::string frameKey = "\"t\":";

    while (true) {
        size_t tPos = text.find(frameKey, cursor);
        if (tPos == std::string::npos) break;

        Frame frame;
        size_t numStart = tPos + frameKey.size();
        size_t numEnd = text.find_first_of(",}", numStart);
        frame.timestamp = std::stod(text.substr(numStart, numEnd - numStart));

        size_t qArrayStart = text.find('[', numEnd);
        if (qArrayStart == std::string::npos) break;

        size_t searchPos = qArrayStart + 1;
        for (int s = 0; s < expectedNumSensors; ++s) {
            size_t quadStart = text.find('[', searchPos);
            size_t quadEnd = text.find(']', quadStart);
            if (quadStart == std::string::npos || quadEnd == std::string::npos)
                break;

            std::string inner = text.substr(quadStart + 1, quadEnd - quadStart - 1);
            std::stringstream is(inner);
            std::string tok;
            Quat q;
            float vals[4] = {1.0f, 0.0f, 0.0f, 0.0f};
            int idx = 0;
            while (std::getline(is, tok, ',') && idx < 4) {
                vals[idx++] = std::stof(tok);
            }
            q.w = vals[0]; q.x = vals[1]; q.y = vals[2]; q.z = vals[3];
            frame.q[s] = q;

            searchPos = quadEnd + 1;
        }

        outFrames.push_back(frame);
        cursor = searchPos;
    }

    return !outFrames.empty();
}

} // namespace mocap::json
