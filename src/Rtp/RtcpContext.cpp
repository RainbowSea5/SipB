#include "RtcpContext.h"

#include <cstring>
#include <sys/time.h>

#include "Network/sockutil.h"
#include "Util/logger.h"

using namespace std;

namespace rtp {

//==============================================================================
// NTP 时间戳
//==============================================================================

uint64_t RtcpContext::getNtpTimestamp() {
    struct timeval tv;
    gettimeofday(&tv, nullptr);
    uint64_t sec = (uint64_t)tv.tv_sec + 2208988800ULL;
    uint32_t frac = (uint32_t)(((double)tv.tv_usec / 1000000.0) * 4294967296.0);
    return (sec << 32) | frac;
}

//==============================================================================
// 发送统计
//==============================================================================

void RtcpContext::onSendRtp(uint32_t rtp_timestamp, uint32_t rtp_payload_size) {
    if (_packet_count == 0) {
        _base_ntp = getNtpTimestamp();
        _base_rtp_ts = rtp_timestamp;
    }
    _packet_count++;
    _octet_count += rtp_payload_size;
}

bool RtcpContext::onReceiveRr(const uint8_t* data, size_t len) {
    uint32_t ssrc;
    vector<ReportBlock> blocks;
    if (!parseRr(data, len, ssrc, blocks)) {
        return false;
    }
    _rr_blocks = blocks;
    return true;
}

//==============================================================================
// 读写工具
//==============================================================================

void RtcpContext::writeHeader(uint8_t* buf, uint8_t pt, uint8_t rc, uint16_t word_count) {
    buf[0] = 0x80 | (rc & 0x1F);
    buf[1] = pt;
    buf[2] = (word_count >> 8) & 0xFF;
    buf[3] = word_count & 0xFF;
}

void RtcpContext::writeBlock(uint8_t* buf, const ReportBlock& block) {
    auto* w = reinterpret_cast<uint32_t*>(buf);
    w[0] = htonl(block.ssrc);
    buf[4] = block.fraction_lost;
    w[1] = htonl(block.lost_packets & 0x00FFFFFF);
    w[2] = htonl(block.last_seq);
    w[3] = htonl(block.jitter);
    w[4] = htonl(block.last_sr);
    w[5] = htonl(block.delay_last_sr);
}

bool RtcpContext::readHeader(const uint8_t* data, size_t len, uint8_t& pt, uint8_t& rc, uint16_t& word_count) {
    if (len < 4) {
        return false;
    }
    if (((data[0] >> 6) & 0x03) != 2) {
        return false;
    }
    rc = data[0] & 0x1F;
    pt = data[1];
    word_count = ntohs(*reinterpret_cast<const uint16_t*>(data + 2));
    return true;
}

bool RtcpContext::readBlock(const uint8_t* data, ReportBlock& block) {
    auto* r = reinterpret_cast<const uint32_t*>(data);
    block.ssrc          = ntohl(r[0]);
    block.fraction_lost = data[4];
    block.lost_packets  = ntohl(r[1]) & 0x00FFFFFF;
    block.last_seq      = ntohl(r[2]);
    block.jitter        = ntohl(r[3]);
    block.last_sr       = ntohl(r[4]);
    block.delay_last_sr = ntohl(r[5]);
    return true;
}

//==============================================================================
// 解析
//==============================================================================

bool RtcpContext::parseSr(const uint8_t* data, size_t len,
                           uint32_t& ssrc, SenderInfo& sender,
                           vector<ReportBlock>& blocks) {
    uint8_t pt, rc;
    uint16_t word_count;
    if (!readHeader(data, len, pt, rc, word_count)) {
        return false;
    }
    if (pt != static_cast<uint8_t>(RtcpPt::SR)) {
        return false;
    }
    size_t hdr_len = 4 + 4 + 20 + rc * 24;
    size_t pkt_len = (word_count + 1) * 4;
    if (pkt_len < hdr_len || len < hdr_len) {
        return false;
    }

    const uint8_t* p = data + 4;
    ssrc = ntohl(*reinterpret_cast<const uint32_t*>(p));
    p += 4;

    auto* si = reinterpret_cast<const uint32_t*>(p);
    sender.ntp_timestamp = (static_cast<uint64_t>(ntohl(si[0])) << 32) | ntohl(si[1]);
    sender.rtp_timestamp  = ntohl(si[2]);
    sender.packet_count   = ntohl(si[3]);
    sender.octet_count    = ntohl(si[4]);
    p += 20;

    blocks.clear();
    blocks.reserve(rc);
    for (uint8_t i = 0; i < rc; ++i) {
        ReportBlock rb{};
        readBlock(p, rb);
        blocks.push_back(rb);
        p += 24;
    }
    return true;
}

bool RtcpContext::parseRr(const uint8_t* data, size_t len, uint32_t& ssrc, vector<ReportBlock>& blocks) {
    uint8_t pt, rc;
    uint16_t word_count;
    if (!readHeader(data, len, pt, rc, word_count)) {
        return false;
    }
    if (pt != static_cast<uint8_t>(RtcpPt::RR)) {
        return false;
    }
    size_t hdr_len = 4 + 4 + rc * 24;
    size_t pkt_len = (word_count + 1) * 4;
    if (pkt_len < hdr_len || len < hdr_len) {
        return false;
    }

    const uint8_t* p = data + 4;
    ssrc = ntohl(*reinterpret_cast<const uint32_t*>(p));
    p += 4;

    blocks.clear();
    blocks.reserve(rc);
    for (uint8_t i = 0; i < rc; ++i) {
        ReportBlock rb{};
        readBlock(p, rb);
        blocks.push_back(rb);
        p += 24;
    }
    return true;
}

bool RtcpContext::parseBye(const uint8_t* data, size_t len, vector<uint32_t>& ssrcs) {
    uint8_t pt, rc;
    uint16_t word_count;
    if (!readHeader(data, len, pt, rc, word_count)) {
        return false;
    }
    if (pt != static_cast<uint8_t>(RtcpPt::BYE)) {
        return false;
    }
    size_t pkt_len = (word_count + 1) * 4;
    if (pkt_len < 4 || len < pkt_len) {
        return false;
    }

    const uint8_t* p = data + 4;
    ssrcs.clear();
    ssrcs.reserve(rc);
    for (uint8_t i = 0; i < rc; ++i) {
        if (static_cast<size_t>(p - data) + 4 > len) {
            break;
        }
        ssrcs.push_back(ntohl(*reinterpret_cast<const uint32_t*>(p)));
        p += 4;
    }
    return !ssrcs.empty();
}

void RtcpContext::parseCompound(const uint8_t* data, size_t len, OnRtcpPacket cb) {
    size_t offset = 0;
    while (offset + 4 <= len) {
        uint8_t pt = data[offset + 1];
        uint16_t word_count = ntohs(*reinterpret_cast<const uint16_t*>(data + offset + 2));
        size_t pkt_len = (word_count + 1) * 4;

        if (offset + pkt_len > len) {
            break;
        }

        cb(static_cast<RtcpPt>(pt), data + offset, pkt_len);
        offset += pkt_len;

        size_t aligned = (pkt_len + 3) & ~3;
        if (offset < len && aligned != pkt_len) {
            offset = (offset + 3) & ~3;
        }
    }
}

bool RtcpContext::isRtcp(const uint8_t* data, size_t len) {
    if (len < 4) {
        return false;
    }
    if ((data[0] & 0xC0) != 0x80) {
        return false;
    }
    uint8_t pt = data[1];
    return pt >= 200 && pt <= 204;
}

//==============================================================================
// 构建
//==============================================================================

vector<uint8_t> RtcpContext::buildSr(uint32_t ssrc) {
    SenderInfo sender;
    sender.ntp_timestamp = getNtpTimestamp();

    if (_packet_count > 0) {
        uint64_t ntp_diff = sender.ntp_timestamp - _base_ntp;
        uint32_t sec_diff  = static_cast<uint32_t>(ntp_diff >> 32);
        uint32_t frac_diff = static_cast<uint32_t>(ntp_diff & 0xFFFFFFFF);
        uint32_t rtp_diff  = sec_diff * 90000
                           + static_cast<uint32_t>((uint64_t)frac_diff * 90000 >> 32);
        sender.rtp_timestamp = _base_rtp_ts + rtp_diff;
    } else {
        sender.rtp_timestamp = _base_rtp_ts;
    }

    sender.packet_count = _packet_count;
    sender.octet_count  = _octet_count;
    return buildSrRaw(ssrc, sender, _rr_blocks);
}

vector<uint8_t> RtcpContext::buildSrRaw(uint32_t ssrc,
                                         const SenderInfo& sender,
                                         const vector<ReportBlock>& blocks) {
    uint8_t rc = static_cast<uint8_t>(blocks.size());
    uint16_t word_count = static_cast<uint16_t>((4 + 4 + 20 + rc * 24) / 4 - 1);
    size_t total = (word_count + 1) * 4;

    vector<uint8_t> buf(total);
    writeHeader(buf.data(), static_cast<uint8_t>(RtcpPt::SR), rc, word_count);

    auto* w = reinterpret_cast<uint32_t*>(buf.data() + 4);
    w[0] = htonl(ssrc);

    w[1] = htonl(static_cast<uint32_t>(sender.ntp_timestamp >> 32));
    w[2] = htonl(static_cast<uint32_t>(sender.ntp_timestamp & 0xFFFFFFFF));
    w[3] = htonl(sender.rtp_timestamp);
    w[4] = htonl(sender.packet_count);
    w[5] = htonl(sender.octet_count);

    for (uint8_t i = 0; i < rc; ++i) {
        writeBlock(buf.data() + 4 + 4 + 20 + i * 24, blocks[i]);
    }
    return buf;
}

vector<uint8_t> RtcpContext::buildRr(uint32_t ssrc, const vector<ReportBlock>& blocks) {
    uint8_t rc = static_cast<uint8_t>(blocks.size());
    uint16_t word_count = static_cast<uint16_t>((4 + 4 + rc * 24) / 4 - 1);
    size_t total = (word_count + 1) * 4;

    vector<uint8_t> buf(total);
    writeHeader(buf.data(), static_cast<uint8_t>(RtcpPt::RR), rc, word_count);

    auto* w = reinterpret_cast<uint32_t*>(buf.data() + 4);
    w[0] = htonl(ssrc);

    for (uint8_t i = 0; i < rc; ++i) {
        writeBlock(buf.data() + 4 + 4 + i * 24, blocks[i]);
    }
    return buf;
}

vector<uint8_t> RtcpContext::buildBye(const vector<uint32_t>& ssrcs, const string& reason) {
    uint8_t rc = static_cast<uint8_t>(ssrcs.size());
    size_t reason_len = reason.size();
    if (reason_len > 255) {
        reason_len = 255;
    }
    size_t body = rc * 4 + (reason_len > 0 ? 1 + reason_len : 0);
    size_t total = 4 + body;
    size_t padded = (total + 3) & ~3;
    uint16_t word_count = static_cast<uint16_t>(padded / 4 - 1);

    vector<uint8_t> buf(padded, 0);
    writeHeader(buf.data(), static_cast<uint8_t>(RtcpPt::BYE), rc, word_count);

    auto* w = reinterpret_cast<uint32_t*>(buf.data() + 4);
    for (uint8_t i = 0; i < rc; ++i) {
        w[i] = htonl(ssrcs[i]);
    }

    if (reason_len > 0) {
        size_t off = 4 + rc * 4;
        buf[off] = static_cast<uint8_t>(reason_len);
        memcpy(buf.data() + off + 1, reason.data(), reason_len);
    }
    return buf;
}

} // namespace rtp
