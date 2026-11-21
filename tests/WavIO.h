// Minimal WAV reader / writer for the developer tools (no JUCE)
//   read : PCM 16 / 24 / 32 bit, IEEE float 32 / 64 bit, WAVE_FORMAT_EXTENSIBLE, mono or stereo
//   write: stereo PCM 24 bit or IEEE float 32 bit
#pragma once

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace wav
{
struct Audio
{
    std::vector<float> l, r;
    double sampleRate = 0.0;
    int bitsPerSample = 0;
    bool isFloat = false;
    int channels = 0;
    size_t size() const noexcept { return l.size(); }
};

namespace detail
{
    inline uint32_t u32 (const unsigned char* p) noexcept { return (uint32_t) p[0] | ((uint32_t) p[1] << 8) | ((uint32_t) p[2] << 16) | ((uint32_t) p[3] << 24); }
    inline uint16_t u16 (const unsigned char* p) noexcept { return (uint16_t) (p[0] | (p[1] << 8)); }
}

// Returns false and sets 'error' if the file cannot be read.
inline bool read (const std::string& path, Audio& out, std::string& error)
{
    FILE* f = std::fopen (path.c_str(), "rb");
    if (f == nullptr)
    {
        error = "cannot open file";
        return false;
    }
    std::vector<unsigned char> data;
    {
        unsigned char buf[65536];
        size_t n;
        while ((n = std::fread (buf, 1, sizeof (buf), f)) > 0)
            data.insert (data.end(), buf, buf + n);
        std::fclose (f);
    }
    if (data.size() < 12 || std::memcmp (data.data(), "RIFF", 4) != 0 || std::memcmp (data.data() + 8, "WAVE", 4) != 0)
    {
        error = "not a RIFF/WAVE file";
        return false;
    }
    int format = 0, channels = 0, bits = 0;
    uint32_t rate = 0;
    const unsigned char* pcm = nullptr;
    size_t pcmBytes = 0;
    size_t pos = 12;
    while (pos + 8 <= data.size())
    {
        const unsigned char* c = data.data() + pos;
        const uint32_t len = detail::u32 (c + 4);
        const size_t body = pos + 8;
        const size_t avail = std::min ((size_t) len, data.size() - body);
        if (std::memcmp (c, "fmt ", 4) == 0 && avail >= 16)
        {
            format = detail::u16 (data.data() + body);
            channels = detail::u16 (data.data() + body + 2);
            rate = detail::u32 (data.data() + body + 4);
            bits = detail::u16 (data.data() + body + 14);
            if (format == 0xFFFE && avail >= 26)
                format = detail::u16 (data.data() + body + 24); // sub format GUID starts with the format code
        }
        else if (std::memcmp (c, "data", 4) == 0)
        {
            pcm = data.data() + body;
            pcmBytes = avail;
        }
        pos = body + len + (len & 1u);
    }
    if (pcm == nullptr || channels < 1 || rate == 0)
    {
        error = "no audio data";
        return false;
    }
    const bool isFloat = format == 3;
    if (! ((format == 1 && (bits == 16 || bits == 24 || bits == 32)) || (isFloat && (bits == 32 || bits == 64))))
    {
        error = "unsupported format (" + std::to_string (format) + ", " + std::to_string (bits) + " bit)";
        return false;
    }
    const size_t bytesPerSample = (size_t) bits / 8;
    const size_t frames = pcmBytes / (bytesPerSample * (size_t) channels);
    out.l.assign (frames, 0.0f);
    out.r.assign (frames, 0.0f);
    out.sampleRate = rate;
    out.bitsPerSample = bits;
    out.isFloat = isFloat;
    out.channels = channels;
    auto sample = [&] (const unsigned char* p) -> float
    {
        if (isFloat)
        {
            if (bits == 32)
            {
                float v;
                std::memcpy (&v, p, 4);
                return v;
            }
            double v;
            std::memcpy (&v, p, 8);
            return (float) v;
        }
        if (bits == 16)
            return (float) (int16_t) detail::u16 (p) / 32768.0f;
        if (bits == 24)
        {
            int32_t v = (int32_t) ((uint32_t) p[0] << 8 | (uint32_t) p[1] << 16 | (uint32_t) p[2] << 24) >> 8;
            return (float) v / 8388608.0f;
        }
        return (float) ((double) (int32_t) detail::u32 (p) / 2147483648.0);
    };
    for (size_t i = 0; i < frames; ++i)
    {
        const unsigned char* p = pcm + i * bytesPerSample * (size_t) channels;
        out.l[i] = sample (p);
        out.r[i] = channels > 1 ? sample (p + bytesPerSample) : out.l[i];
    }
    return true;
}

inline bool write (const std::string& path, const std::vector<float>& l, const std::vector<float>& r, int sampleRate, bool pcm24)
{
    FILE* f = std::fopen (path.c_str(), "wb");
    if (f == nullptr)
        return false;
    const uint32_t frames = (uint32_t) l.size();
    const uint16_t bits = pcm24 ? 24 : 32;
    const uint32_t blockAlign = 2u * bits / 8u;
    const uint32_t dataBytes = frames * blockAlign;
    auto w32 = [f] (uint32_t v) { const unsigned char b[4] = { (unsigned char) v, (unsigned char) (v >> 8), (unsigned char) (v >> 16), (unsigned char) (v >> 24) }; std::fwrite (b, 1, 4, f); };
    auto w16 = [f] (uint16_t v) { const unsigned char b[2] = { (unsigned char) v, (unsigned char) (v >> 8) }; std::fwrite (b, 1, 2, f); };
    std::fwrite ("RIFF", 1, 4, f);
    w32 (36 + dataBytes);
    std::fwrite ("WAVEfmt ", 1, 8, f);
    w32 (16);
    w16 (pcm24 ? 1 : 3);
    w16 (2);
    w32 ((uint32_t) sampleRate);
    w32 ((uint32_t) sampleRate * blockAlign);
    w16 ((uint16_t) blockAlign);
    w16 (bits);
    std::fwrite ("data", 1, 4, f);
    w32 (dataBytes);
    std::vector<unsigned char> buf;
    buf.reserve ((size_t) dataBytes);
    auto put = [&buf, pcm24] (float x)
    {
        if (pcm24)
        {
            const double c = std::fmax (-1.0, std::fmin (1.0, (double) x));
            const int32_t v = (int32_t) std::lround (c * 8388607.0);
            buf.push_back ((unsigned char) (v & 0xff));
            buf.push_back ((unsigned char) ((v >> 8) & 0xff));
            buf.push_back ((unsigned char) ((v >> 16) & 0xff));
        }
        else
        {
            unsigned char b[4];
            std::memcpy (b, &x, 4);
            buf.insert (buf.end(), b, b + 4);
        }
    };
    for (uint32_t i = 0; i < frames; ++i)
    {
        put (l[i]);
        put (r[i]);
    }
    std::fwrite (buf.data(), 1, buf.size(), f);
    const bool ok = std::ferror (f) == 0;
    std::fclose (f);
    return ok;
}

} // namespace wav
