// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <windows.h>

#include <cstdio>
#include <string>

extern "C" {
#include <libavutil/error.h>
}

namespace diva {

inline std::string hrError(const char *what, HRESULT hr)
{
    char buf[256];
    std::snprintf(buf, sizeof buf, "%s failed (HRESULT 0x%08lX)", what, static_cast<unsigned long>(hr));
    return buf;
}

inline std::string avError(const std::string &what, int err)
{
    char buf[AV_ERROR_MAX_STRING_SIZE] = {};
    av_strerror(err, buf, sizeof buf);
    return what + ": " + buf;
}

// Stores message in *error (when given) and returns false, for `return fail(error, ...)`.
inline bool fail(std::string *error, std::string message)
{
    if (error)
        *error = std::move(message);
    return false;
}

} // namespace diva
