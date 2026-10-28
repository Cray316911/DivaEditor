// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "media/HwDecoder.h"

#include <QString>

#include <memory>

namespace diva {

// Frame access by source time for one media file: decodes forward for nearby times and
// seeks for jumps. Used by the timeline renderer and by DivaAI's frame sampling.
class FrameSource
{
public:
    FrameSource() = default;
    ~FrameSource();
    FrameSource(const FrameSource &) = delete;
    FrameSource &operator=(const FrameSource &) = delete;

    bool open(const QString &path, std::shared_ptr<GpuDevice> gpu, QString *error);
    // Newest frame at or before source time t (owned by the source, valid until the next call).
    const AVFrame *frameAt(double t);
    // Nearest keyframe at or before t, decoding only keyframes: fast scanning of long videos.
    const AVFrame *keyframeAt(double t);
    double timeOf(const AVFrame *frame) const { return m_decoder.frameSeconds(frame); }
    HwDecoder &decoder() { return m_decoder; }

private:
    void reset();

    HwDecoder m_decoder;
    AVFrame *m_current = nullptr;
    AVFrame *m_next = nullptr;
    bool m_primed = false;
    bool m_keyframesOnly = false;
};

} // namespace diva
