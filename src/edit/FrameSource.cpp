// SPDX-License-Identifier: GPL-3.0-or-later
#include "FrameSource.h"

extern "C" {
#include <libavutil/frame.h>
}

namespace diva {

namespace {
constexpr double kEpsilon = 0.001;
// Decoding forward is cheaper than seeking for jumps shorter than this.
constexpr double kMaxForwardDecode = 3.0;
} // namespace

FrameSource::~FrameSource()
{
    reset();
}

bool FrameSource::open(const QString &path, std::shared_ptr<GpuDevice> gpu, QString *error)
{
    std::string err;
    if (!m_decoder.open(path.toStdString(), std::move(gpu), &err)) {
        if (error)
            *error = QString::fromStdString(err);
        return false;
    }
    return true;
}

void FrameSource::reset()
{
    av_frame_free(&m_current);
    av_frame_free(&m_next);
    m_primed = false;
}

const AVFrame *FrameSource::frameAt(double t)
{
    if (m_keyframesOnly) {
        m_decoder.setKeyframesOnly(false);
        m_keyframesOnly = false;
        reset();
    }
    const bool backwards = m_current && t < timeOf(m_current) - kEpsilon;
    const bool farAhead = m_next && t > timeOf(m_next) + kMaxForwardDecode;
    if (!m_primed || backwards || farAhead) {
        reset();
        m_decoder.seekTo(t);
        m_next = m_decoder.decodeNext();
        m_primed = true;
    }
    while (m_next && (!m_current || timeOf(m_next) <= t + kEpsilon)) {
        av_frame_free(&m_current);
        m_current = m_next;
        m_next = m_decoder.decodeNext();
    }
    return m_current;
}

const AVFrame *FrameSource::keyframeAt(double t)
{
    if (!m_keyframesOnly) {
        m_decoder.setKeyframesOnly(true);
        m_keyframesOnly = true;
    }
    reset();
    m_decoder.seekTo(t);
    m_current = m_decoder.decodeNext();
    return m_current;
}

} // namespace diva
