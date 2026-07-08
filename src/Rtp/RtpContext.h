#ifndef RTP_RTP_CONTEXT_H
#define RTP_RTP_CONTEXT_H

#include <cstdint>
#include <functional>
#include <memory>
#include <map>
#include <vector>
#include "RtcpContext.h"

namespace rtp {

class RtpContext : public std::enable_shared_from_this<RtpContext> {
public:
    using Ptr = std::shared_ptr<RtpContext>;
    using RtpPacketCallback = std::function<void(const uint8_t* data, size_t len)>;

    static Ptr create(uint8_t payload_type, uint32_t ssrc);

    void inputH264(const uint8_t* data, size_t len, uint32_t timestamp, RtpPacketCallback cb);
    void inputH265(const uint8_t* data, size_t len, uint32_t timestamp, RtpPacketCallback cb);
    void inputPCMA(const uint8_t* data, size_t len, uint32_t timestamp, RtpPacketCallback cb);

    uint8_t payloadType() const {
        return _payload_type;
    }
    // 判断数据是否为 RTP 包 (V=2)
    static bool isRtp(const uint8_t* data, size_t len);
    // 获取当前 RTCP SR (Sender Report)
    std::vector<uint8_t> getRtcpSr(uint32_t ssrc);
    // 处理收到的 RTCP RR
    void onRtcpRr(const uint8_t* data, size_t len);
    // 处理收到的复合 RTCP 包，内部只处理 RR
    void onRtcp(const uint8_t* data, size_t len);
    // 接收 RTP 包：解析头部 + 序列号排序缓冲 + 有序回调 payload
    using OnRecvRtpPayload = std::function<void(const uint8_t* payload, size_t len, uint32_t timestamp)>;
    void onRecvRtp(const uint8_t* data, size_t len, OnRecvRtpPayload cb);

private:
    RtpContext(uint8_t payload_type, uint32_t ssrc);

    void writeRtpHeader(uint8_t* buf, uint8_t pt, bool marker, uint32_t timestamp);

    void packetizeH264Nalu(const uint8_t* nalu, size_t len, uint32_t timestamp, RtpPacketCallback cb);
    void packetizeH265Nalu(const uint8_t* nalu, size_t len, uint32_t timestamp, RtpPacketCallback cb);

    uint8_t _payload_type{0};
    uint16_t _seq{1};
    uint32_t _ssrc{0};

    std::vector<uint8_t> _video_buf;
    std::vector<uint8_t> _audio_buf;
    RtcpContext _rtcp;
    // RTP 接收排序缓冲
    struct RtpRecvPacket {
        std::vector<uint8_t> payload;
        uint32_t timestamp{0};
    };
    std::map<uint16_t, RtpRecvPacket> _recv_buffer;
    uint16_t _expected_seq{0};
    bool _recv_initialized{false};
    static constexpr size_t MAX_RECV_BUFFER = 50;
};

} // namespace rtp

#endif //RTP_RTP_CONTEXT_H
