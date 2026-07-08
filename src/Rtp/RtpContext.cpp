#include "RtpContext.h"

#include <cstring>

#include "Util/logger.h"

using namespace std;

namespace rtp {
static constexpr size_t VIDEO_BUF_SIZE = 2 + 12 + 2 + 1400;
static constexpr size_t AUDIO_BUF_SIZE = 2 + 12 + 320;

RtpContext::RtpContext(uint8_t payload_type, uint32_t ssrc)
    : _payload_type(payload_type), _ssrc(ssrc) {
    _video_buf.resize(VIDEO_BUF_SIZE);
    _audio_buf.resize(AUDIO_BUF_SIZE);
}

RtpContext::Ptr RtpContext::create(uint8_t payload_type, uint32_t ssrc) {
    return Ptr(new RtpContext(payload_type, ssrc));
}

// 所有多字节字段按网络字节序 (大端) 写入: seq, timestamp, ssrc
void RtpContext::writeRtpHeader(uint8_t *buf, uint8_t pt, bool marker, uint32_t timestamp) {
    buf[0] = 0x80;
    buf[1] = (marker ? 0x80 : 0) | (pt & 0x7F);
    buf[2] = (_seq >> 8) & 0xFF;
    buf[3] = _seq & 0xFF;
    buf[4] = (timestamp >> 24) & 0xFF;
    buf[5] = (timestamp >> 16) & 0xFF;
    buf[6] = (timestamp >> 8) & 0xFF;
    buf[7] = timestamp & 0xFF;
    buf[8] = (_ssrc >> 24) & 0xFF;
    buf[9] = (_ssrc >> 16) & 0xFF;
    buf[10] = (_ssrc >> 8) & 0xFF;
    buf[11] = _ssrc & 0xFF;
    ++_seq;
}

static void writeTcpPrefix(uint8_t *buf, uint16_t rtp_len) {
    buf[0] = (rtp_len >> 8) & 0xFF;
    buf[1] = rtp_len & 0xFF;
}

void RtpContext::packetizeH264Nalu(const uint8_t *nalu, size_t len, uint32_t timestamp, RtpPacketCallback cb) {
    if (len < 1) {
        return;
    }
    uint8_t nal_type = nalu[0] & 0x1F;
    uint8_t nal_ref = (nalu[0] >> 5) & 0x03;
    const size_t MAX_PAYLOAD = 1400;

    if (len <= MAX_PAYLOAD) {
        size_t total = 2 + 12 + len;
        _video_buf.resize(total);
        uint8_t *buf = _video_buf.data();
        writeTcpPrefix(buf, static_cast<uint16_t>(12 + len));
        writeRtpHeader(buf + 2, _payload_type, true, timestamp);
        memcpy(buf + 14, nalu, len);
        cb(buf, total);
        _rtcp.onSendRtp(timestamp, static_cast<uint32_t>(total > 14 ? total - 14 : 0));
        return;
    }

    const uint8_t *pay = nalu + 1;
    size_t pay_len = len - 1;
    size_t off = 0;
    const size_t fu_max = MAX_PAYLOAD - 2;
    while (off < pay_len) {
        size_t frag = min(fu_max, pay_len - off);
        bool first = (off == 0);
        bool last = (off + frag >= pay_len);

        size_t total = 2 + 12 + 2 + frag;
        _video_buf.resize(total);
        uint8_t *buf = _video_buf.data();
        writeTcpPrefix(buf, static_cast<uint16_t>(12 + 2 + frag));
        writeRtpHeader(buf + 2, _payload_type, last, timestamp);
        buf[14] = 0x7C | (nal_ref << 5);
        buf[15] = (first ? 0x80 : 0) | (last ? 0x40 : 0) | nal_type;
        memcpy(buf + 16, pay + off, frag);
        cb(buf, total);
        _rtcp.onSendRtp(timestamp, static_cast<uint32_t>(total > 14 ? total - 14 : 0));
        off += frag;
    }
}

void RtpContext::inputH264(const uint8_t *data, size_t len, uint32_t timestamp, RtpPacketCallback cb) {
    const uint8_t *nalu = data;
    size_t nlen = len;
    while (nlen >= 4 && nalu[0] == 0 && nalu[1] == 0) {
        if (nalu[2] == 1) {
            nalu += 3;
            nlen -= 3;
            break;
        }
        if (nlen >= 4 && nalu[2] == 0 && nalu[3] == 1) {
            nalu += 4;
            nlen -= 4;
            break;
        }
        break;
    }
    if (nlen < 1) {
        return;
    }
    packetizeH264Nalu(nalu, nlen, timestamp, cb);
}

void RtpContext::packetizeH265Nalu(const uint8_t *nalu, size_t len, uint32_t timestamp, RtpPacketCallback cb) {
    if (len < 2) {
        return;
    }
    uint8_t nal_type = (nalu[0] >> 1) & 0x3F;
    const size_t MAX_PAYLOAD = 1400;

    if (len <= MAX_PAYLOAD) {
        size_t total = 2 + 12 + len;
        _video_buf.resize(total);
        uint8_t *buf = _video_buf.data();
        writeTcpPrefix(buf, static_cast<uint16_t>(12 + len));
        writeRtpHeader(buf + 2, _payload_type, true, timestamp);
        memcpy(buf + 14, nalu, len);
        cb(buf, total);
        _rtcp.onSendRtp(timestamp, static_cast<uint32_t>(total > 14 ? total - 14 : 0));
        return;
    }

    const uint8_t *pay = nalu + 2;
    size_t pay_len = len - 2;
    size_t off = 0;
    const size_t fu_max = MAX_PAYLOAD - 3;
    while (off < pay_len) {
        size_t frag = min(fu_max, pay_len - off);
        bool first = (off == 0);
        bool last = (off + frag >= pay_len);

        size_t total = 2 + 12 + 3 + frag;
        _video_buf.resize(total);
        uint8_t *buf = _video_buf.data();
        writeTcpPrefix(buf, static_cast<uint16_t>(12 + 3 + frag));
        writeRtpHeader(buf + 2, _payload_type, last, timestamp);
        buf[14] = (nalu[0] & 0x80) | (49 << 1) | ((nalu[1] >> 7) & 1);
        buf[15] = (nalu[1] & 0x7F) | 0x80;
        buf[16] = (first ? 0x80 : 0) | (last ? 0x40 : 0) | (nal_type & 0x3F);
        memcpy(buf + 17, pay + off, frag);
        cb(buf, total);
        _rtcp.onSendRtp(timestamp, static_cast<uint32_t>(total > 14 ? total - 14 : 0));
        off += frag;
    }
}

void RtpContext::inputH265(const uint8_t *data, size_t len, uint32_t timestamp, RtpPacketCallback cb) {
    const uint8_t *nalu = data;
    size_t nlen = len;
    while (nlen >= 4 && nalu[0] == 0 && nalu[1] == 0) {
        if (nalu[2] == 1) {
            nalu += 3;
            nlen -= 3;
            break;
        }
        if (nalu[2] == 1) {
            nalu += 3;
            nlen -= 3;
            break;
        }
        if (nlen >= 4 && nalu[2] == 0 && nalu[3] == 1) {
            nalu += 4;
            nlen -= 4;
            break;
        }
        if (nlen >= 4 && nalu[2] == 0 && nalu[3] == 1) {
            nalu += 4;
            nlen -= 4;
            break;
        }
        break;
    }
    if (nlen < 2) {
        return;
    }
    packetizeH265Nalu(nalu, nlen, timestamp, cb);
}

bool RtpContext::isRtp(const uint8_t *data, size_t len) {
    return len >= 12 && (data[0] & 0xC0) == 0x80;
}

void RtpContext::inputPCMA(const uint8_t *data, size_t len, uint32_t timestamp, RtpPacketCallback cb) {
    if (len == 0) {
        return;
    }
    size_t total = 2 + 12 + len;
    _audio_buf.resize(total);
    uint8_t *buf = _audio_buf.data();
    writeTcpPrefix(buf, static_cast<uint16_t>(12 + len));
    writeRtpHeader(buf + 2, _payload_type, true, timestamp);
    memcpy(buf + 14, data, len);
    cb(buf, total);
    _rtcp.onSendRtp(timestamp, static_cast<uint32_t>(total > 14 ? total - 14 : 0));
}

std::vector<uint8_t> RtpContext::getRtcpSr(uint32_t ssrc) {
    return _rtcp.buildSr(ssrc);
}

void RtpContext::onRtcpRr(const uint8_t *data, size_t len) {
    _rtcp.onReceiveRr(data, len);
}

void RtpContext::onRtcp(const uint8_t *data, size_t len) {
    _rtcp.parseCompound(data, len, [this](RtcpPt pt, const uint8_t *pkt, size_t pkt_len) {
        if (pt == RtcpPt::RR) {
            _rtcp.onReceiveRr(pkt, pkt_len);
        }
    });
}

void RtpContext::onRecvRtp(const uint8_t *data, size_t len, OnRecvRtpPayload cb) {
    if (len < 12) {
        return;
    }
    // 解析 RTP 固定头
    uint8_t cc = data[0] & 0x0F; // CSRC 个数
    bool has_ext = (data[0] & 0x10) != 0; // 扩展头标志
    uint16_t seq = (static_cast<uint16_t>(data[2]) << 8) | data[3];
    uint32_t ts = (static_cast<uint32_t>(data[4]) << 24)
                  | (static_cast<uint32_t>(data[5]) << 16)
                  | (static_cast<uint32_t>(data[6]) << 8)
                  | data[7];

    // 计算头部总长度
    size_t header_len = 12 + cc * 4;
    if (has_ext) {
        if (len < header_len + 4) {
            return;
        }
        uint16_t ext_len = (static_cast<uint16_t>(data[header_len + 2]) << 8)
                           | data[header_len + 3];
        header_len += 4 + ext_len * 4;
    }
    if (len < header_len) {
        return;
    }
    size_t payload_len = len - header_len;
    if (payload_len == 0) {
        return;
    }

    if (!_recv_initialized) {
        // 第一个包：直接回调
        cb(data + header_len, payload_len, ts);
        _expected_seq = static_cast<uint16_t>(seq + 1);
        _recv_initialized = true;
        return;
    }

    if (seq == _expected_seq) {
        // 刚好是期望的下一个包
        cb(data + header_len, payload_len, ts);
        ++_expected_seq;

        // 顺带刷出缓冲中连续的数据
        auto it = _recv_buffer.find(_expected_seq);
        while (it != _recv_buffer.end()) {
            auto &pkt = it->second;
            cb(pkt.payload.data(), pkt.payload.size(), pkt.timestamp);
            _recv_buffer.erase(it);
            ++_expected_seq;
            it = _recv_buffer.find(_expected_seq);
        }
        return;
    }

    // 乱序 / 重复包：丢弃太旧的，新到则缓冲
    int16_t diff = static_cast<int16_t>(seq - _expected_seq);
    if (diff < 0 && static_cast<uint16_t>(-diff) > MAX_RECV_BUFFER) {
        return; // 太旧的包，丢弃
    }

    if (_recv_buffer.find(seq) == _recv_buffer.end() && _recv_buffer.size() < MAX_RECV_BUFFER) {
        RtpRecvPacket pkt;
        pkt.payload.assign(data + header_len, data + len);
        pkt.timestamp = ts;
        _recv_buffer[seq] = std::move(pkt);
    }
}
} // namespace rtp
