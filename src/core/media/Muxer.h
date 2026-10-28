// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <mutex>
#include <string>
#include <vector>

extern "C" {
#include <libavutil/rational.h>
}

struct AVCodecContext;
struct AVFormatContext;
struct AVPacket;

namespace diva {

// Writes encoded streams into a container picked from the file extension (mp4, mkv, mov).
// write() is thread-safe so audio and video encoders can feed it from their own threads.
class Muxer
{
public:
    Muxer();
    ~Muxer();

    Muxer(const Muxer &) = delete;
    Muxer &operator=(const Muxer &) = delete;

    bool open(const std::string &utf8Path, std::string *error);
    // Encoders must be opened with AV_CODEC_FLAG_GLOBAL_HEADER when this is true.
    bool needsGlobalHeader() const;
    // Call after the encoder is open; returns the stream index for write().
    int addStream(const AVCodecContext *encoder);
    void setStreamTitle(int stream, const std::string &title);
    bool start(std::string *error);
    bool write(AVPacket *packet, int stream, std::string *error = nullptr);
    bool finish(std::string *error);

private:
    AVFormatContext *m_format = nullptr;
    std::vector<AVRational> m_encoderTimeBases;
    std::mutex m_mutex;
    bool m_started = false;
    bool m_finished = false;
};

} // namespace diva
