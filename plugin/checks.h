// What the checks share: base64 both ways, which is how a session keeps its module, and the notes in an XM file,
// which is how a check reads back what was typed into the tracker.
#pragma once
#include <algorithm>
#include <cstring>
#include <string>

inline std::string base64(const std::string& in) {
    static const char* t = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    for (size_t i = 0; i < in.size(); i += 3) {
        const size_t n = std::min<size_t>(3, in.size() - i);
        const unsigned v = (unsigned char)in[i] << 16 | (n > 1 ? (unsigned char)in[i + 1] << 8 : 0) | (n > 2 ? (unsigned char)in[i + 2] : 0);
        out += t[v >> 18 & 63];
        out += t[v >> 12 & 63];
        out += n > 1 ? t[v >> 6 & 63] : '=';
        out += n > 2 ? t[v & 63] : '=';
    }
    return out;
}

inline std::string unbase64(const std::string& in) {
    static const char* t = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    unsigned v = 0;
    int bits = 0;
    for (char c : in) {
        const char* p = c ? std::strchr(t, c) : nullptr;
        if (!p) continue;
        v = v << 6 | (unsigned)(p - t);
        if ((bits += 6) >= 8) out += (char)(v >> (bits -= 8) & 255);
    }
    return out;
}

// The notes in every pattern of an XM file, -1 if it is not one
inline int xmNotes(const std::string& xm) {
    if (xm.size() < 80 || xm.compare(0, 17, "Extended Module: ") != 0) return -1;
    auto u8 = [&](size_t o) { return o < xm.size() ? (unsigned)(unsigned char)xm[o] : 0u; };
    auto u16 = [&](size_t o) { return u8(o) | u8(o + 1) << 8; };
    auto u32 = [&](size_t o) { return u16(o) | u16(o + 2) << 16; };
    const unsigned patterns = u16(70);
    size_t p = 60 + u32(60);
    int notes = 0;
    for (unsigned k = 0; k < patterns && p + 9 <= xm.size(); ++k) {
        const size_t head = u32(p), packed = u16(p + 7);
        for (size_t i = p + head, end = std::min(xm.size(), p + head + packed); i < end;) {
            const unsigned b = u8(i++);
            if (b & 0x80) {
                if (b & 1) notes += u8(i) >= 1 && u8(i) <= 96, ++i;
                for (int bit = 1; bit < 5; ++bit) i += b >> bit & 1;
            } else {
                notes += b >= 1 && b <= 96;
                i += 4;
            }
        }
        p += head + packed;
    }
    return notes;
}
