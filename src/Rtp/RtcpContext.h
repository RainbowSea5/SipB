#ifndef RTP_RTCP_CONTEXT_H
#define RTP_RTCP_CONTEXT_H

#include <cstdint>
#include <string>
#include <vector>
#include <functional>

namespace rtp {

enum class RtcpPt : uint8_t {
    SR   = 200,
    RR   = 201,
    SDES = 202,
    BYE  = 203,
    APP  = 204,
};

#pragma pack(push, 1)

struct RtcpHeader {
    uint8_t  first;    // V(2) | P(1) | RC(5)
    uint8_t  pt;       // packet type
    uint16_t length;   // (total/4) - 1, network order
};

struct SenderInfo {
    uint64_t ntp_timestamp;
    uint32_t rtp_timestamp;
    uint32_t packet_count;
    uint32_t octet_count;
};

struct ReportBlock {
    uint32_t ssrc;
    uint8_t  fraction_lost;
    uint32_t lost_packets;
    uint32_t last_seq;
    uint32_t jitter;
    uint32_t last_sr;
    uint32_t delay_last_sr;
};

#pragma pack(pop)

// RTCP 工具 — 非静态，持有发送统计供 SR 使用
class RtcpContext {
public:
    RtcpContext() = default;

    // 记录一个 RTP 包已发送 (每次 cb() 后调用)
    void onSendRtp(uint32_t rtp_timestamp, uint32_t rtp_payload_size);

    // 处理收到的 RTCP RR 包，缓存 report block
    bool onReceiveRr(const uint8_t* data, size_t len);

    //==== 解析 =====
    bool parseSr(const uint8_t* data, size_t len,
                 uint32_t& ssrc, SenderInfo& sender,
                 std::vector<ReportBlock>& blocks);
    bool parseRr(const uint8_t* data, size_t len,
                 uint32_t& ssrc,
                 std::vector<ReportBlock>& blocks);
    bool parseBye(const uint8_t* data, size_t len,
                  std::vector<uint32_t>& ssrcs);

    using OnRtcpPacket = std::function<void(RtcpPt pt, const uint8_t* data, size_t len)>;
    void parseCompound(const uint8_t* data, size_t len, OnRtcpPacket cb);

    static bool isRtcp(const uint8_t* data, size_t len);

    //==== 构建 =====

    // 使用内部发送统计构建 SR
    std::vector<uint8_t> buildSr(uint32_t ssrc);

    std::vector<uint8_t> buildRr(uint32_t ssrc,
                                  const std::vector<ReportBlock>& blocks);
    std::vector<uint8_t> buildBye(const std::vector<uint32_t>& ssrcs, const std::string& reason = "");

    const std::vector<ReportBlock>& lastReportBlocks() const {
        return _rr_blocks;
    }

private:
    static void writeHeader(uint8_t* buf, uint8_t pt, uint8_t rc, uint16_t word_count);
    static void writeBlock(uint8_t* buf, const ReportBlock& block);
    static bool readHeader(const uint8_t* data, size_t len,uint8_t& pt, uint8_t& rc, uint16_t& word_count);
    static bool readBlock(const uint8_t* data, ReportBlock& block);

    // 内部构建 SR (原始数据版本)
    std::vector<uint8_t> buildSrRaw(uint32_t ssrc,const SenderInfo& sender,const std::vector<ReportBlock>& blocks);

    static uint64_t getNtpTimestamp();

    uint32_t _packet_count{0};
    uint32_t _octet_count{0};
    uint32_t _base_rtp_ts{0};
    uint64_t _base_ntp{0};

    std::vector<ReportBlock> _rr_blocks;
};

} // namespace rtp

#endif //RTP_RTCP_CONTEXT_H
