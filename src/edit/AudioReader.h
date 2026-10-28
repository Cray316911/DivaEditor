// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QString>
#include <QVector>

struct AVAudioFifo;
struct AVCodecContext;
struct AVFormatContext;
struct AVFrame;
struct AVPacket;
struct SwrContext;

namespace diva {

// Decodes every audio stream of a file (a DivaStudio recording has one for system sound and
// one for the microphone), resamples them to 48 kHz stereo float and mixes them together.
// Times are in the same base as the video (seconds from the video stream start).
class AudioReader
{
public:
    static constexpr int kRate = 48000;

    AudioReader() = default;
    ~AudioReader();
    AudioReader(const AudioReader &) = delete;
    AudioReader &operator=(const AudioReader &) = delete;

    bool open(const QString &path, QString *error);
    bool hasAudio() const { return !m_streams.isEmpty(); }
    // The next read() starts at `seconds`.
    void seek(double seconds);
    // Reads `count` samples per channel; silence where there is no audio.
    void read(float *left, float *right, int count);

private:
    struct Stream
    {
        int index = -1;
        AVCodecContext *codec = nullptr;
        SwrContext *swr = nullptr;
        AVAudioFifo *fifo = nullptr;
        double skipUntil = 0; // drop decoded audio before this time (after a seek)
        bool done = false;
    };
    void decodeFrames(Stream &s);
    bool fill(int count);

    AVFormatContext *m_format = nullptr;
    QVector<Stream> m_streams;
    AVPacket *m_packet = nullptr;
    AVFrame *m_frame = nullptr;
    double m_origin = 0; // video stream start, in seconds
    bool m_demuxDone = false;
    QVector<float> m_tmpL, m_tmpR;
};

} // namespace diva
