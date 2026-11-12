#pragma once
#include <cstdint>
#include <vector>
#include <cstddef>
#include <algorithm>

// 只保存CUVA的原始T.35负载，不重新计算或改写动态参数；其余SEI保持原位置。
namespace cuva_packet {
using Bytes = std::vector<uint8_t>;
struct Message { unsigned type; Bytes payload; };
constexpr size_t maxPacket = 64 * 1024 * 1024;
constexpr size_t maxMetadata = 64 * 1024;
inline bool isCuva(const Message &m) {
    const uint8_t signature[] = {0x26, 0x00, 0x04, 0x00, 0x05, 0x01};
    return m.type == 4 && m.payload.size() >= sizeof(signature) &&
           std::equal(signature, signature + sizeof(signature), m.payload.begin());
}
inline bool split(const uint8_t *data, size_t size, int lengthSize, std::vector<Bytes> &nals) {
    if (!data || !size || size > maxPacket) return false;
    nals.clear();
    if (lengthSize) {
        if (lengthSize < 1 || lengthSize > 4) return false;
        size_t pos = 0;
        while (pos < size) {
            if (size - pos < static_cast<size_t>(lengthSize)) return false;
            size_t n = 0;
            for (int i = 0; i < lengthSize; ++i) n = (n << 8) | data[pos++];
            if (n < 2 || n > size - pos) return false;
            nals.emplace_back(data + pos, data + pos + n); pos += n;
        }
    } else {
        auto start = [&](size_t pos, size_t &prefix) {
            if (pos + 3 <= size && data[pos] == 0 && data[pos+1] == 0 && data[pos+2] == 1) {prefix = 3; return true;}
            if (pos + 4 <= size && data[pos] == 0 && data[pos+1] == 0 && data[pos+2] == 0 && data[pos+3] == 1) {prefix = 4; return true;}
            return false;
        };
        size_t pos = 0, prefix = 0;
        // AnnexB可有额外leading_zero_8bits；只允许零，不能扫描跳过损坏内容。
        while (pos < size && !start(pos, prefix)) {if (data[pos++] != 0) return false;}
        while (pos < size) {
            if (!start(pos, prefix)) return false;
            const size_t begin = pos + prefix;
            pos = begin;
            while (pos < size && !start(pos, prefix)) ++pos;
            size_t end = pos;
            while (end > begin && data[end - 1] == 0) --end;
            if (end - begin < 2) return false;
            nals.emplace_back(data + begin, data + end);
        }
    }
    return !nals.empty();
}
inline bool messages(const Bytes &nal, std::vector<Message> &out) {
    if (nal.size() < 3 || ((nal[0] >> 1) & 63) != 39) return false;
    Bytes rbsp;
    unsigned zeros = 0;
    for (size_t i = 2; i < nal.size(); ++i) {
        uint8_t b = nal[i];
        if (zeros >= 2 && b == 3) {
            if (i + 1 >= nal.size() || nal[i+1] > 3) return false;
            zeros = 0; continue;
        }
        rbsp.push_back(b); zeros = b == 0 ? zeros + 1 : 0;
    }
    size_t pos = 0; out.clear();
    while (pos < rbsp.size()) {
        // trailing_bits只有末尾0x80；合法payload_type=128不能被当作结束。
        if (pos + 1 == rbsp.size() && rbsp[pos] == 0x80) return true;
        size_t type = 0, count = 0;
        while (pos < rbsp.size() && rbsp[pos] == 255) {type += 255; ++pos; if (type > maxMetadata) return false;}
        if (pos == rbsp.size()) return false;
        type += rbsp[pos++];
        while (pos < rbsp.size() && rbsp[pos] == 255) {count += 255; ++pos; if (count > maxMetadata) return false;}
        if (pos == rbsp.size()) return false;
        count += rbsp[pos++];
        if (count > maxMetadata || count > rbsp.size() - pos) return false;
        out.push_back({static_cast<unsigned>(type), Bytes(rbsp.begin()+pos, rbsp.begin()+pos+count)});
        pos += count;
    }
    return false;
}
inline Bytes buildSei(uint8_t header0, uint8_t header1, const std::vector<Message> &msgs) {
    Bytes raw;
    for (const auto &m : msgs) {
        size_t n = m.type; while (n >= 255) {raw.push_back(255); n -= 255;} raw.push_back(n);
        n = m.payload.size(); while (n >= 255) {raw.push_back(255); n -= 255;} raw.push_back(n);
        raw.insert(raw.end(), m.payload.begin(), m.payload.end());
    }
    raw.push_back(0x80);
    Bytes nal{header0, header1}; unsigned zeros = 0;
    for (auto b : raw) {
        if (zeros >= 2 && b <= 3) {nal.push_back(3); zeros = 0;}
        nal.push_back(b); zeros = b == 0 ? zeros + 1 : 0;
    }
    return nal;
}
inline bool extract(const uint8_t *data, size_t size, int lengthSize, std::vector<Message> &cuva) {
    std::vector<Bytes> nals; if (!split(data, size, lengthSize, nals)) return false;
    cuva.clear(); size_t total = 0;
    for (const auto &nal : nals) {
        if (((nal[0] >> 1) & 63) != 39) continue;
        std::vector<Message> msgs; if (!messages(nal, msgs)) return false;
        for (const auto &m : msgs) if (isCuva(m)) {
            total += m.payload.size(); if (total > maxMetadata) return false;
            cuva.push_back(m);
        }
    }
    return true;
}
inline bool replace(const uint8_t *data, size_t size, const std::vector<Message> &source, Bytes &output) {
    std::vector<Bytes> nals; if (!split(data, size, 0, nals)) return false;
    size_t metadataSize = 0;
    for (const auto &m : source) {
        if (!isCuva(m) || m.payload.size() > maxMetadata) return false;
        metadataSize += m.payload.size();
        if (metadataSize > maxMetadata) return false;
    }
    output.clear(); bool inserted = false;
    for (const auto &nal : nals) {
        auto type = (nal[0] >> 1) & 63;
        // 只在匹配时间戳的画面之前插入，避免首帧CUVA落入hvcC后被播放器忽略。
        if (type <= 31 && !inserted) {
            if (!source.empty()) {
                auto sei = buildSei(static_cast<uint8_t>(78 | (nal[0] & 1)), nal[1], source);
                output.insert(output.end(), {0,0,0,1}); output.insert(output.end(), sei.begin(), sei.end());
            }
            inserted = true;
        }
        Bytes saved = nal;
        if (type == 39) {
            std::vector<Message> msgs; if (!messages(nal, msgs)) return false;
            const size_t before = msgs.size();
            msgs.erase(std::remove_if(msgs.begin(), msgs.end(), isCuva), msgs.end());
            if (msgs.empty()) continue;
            if (msgs.size() != before) saved = buildSei(nal[0], nal[1], msgs);
        }
        output.insert(output.end(), {0,0,0,1}); output.insert(output.end(), saved.begin(), saved.end());
        if (output.size() > maxPacket) return false;
    }
    return inserted;
}
}
