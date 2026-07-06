#ifndef SIPB_RTP_CONTEXT_H
#define SIPB_RTP_CONTEXT_H

#include <cstdint>
#include <string>
#include <functional>
#include <memory>
#include <vector>

#include "RtpSession.h"

namespace sipB {

// RTP 打包器 — 将编码数据打包成 RTP 包，不负责发送
class RtpContext : public std::enable_shared_from_this<RtpContext> {
public:
    using Ptr = std::shared_ptr<RtpContext>;
    using RtpPacketCallback = std::function<void(const uint8_t* data, size_t len)>;

    static Ptr create(const MediaTrackInfo& track, uint32_t ssrc);

    // 打包并逐包回调 (单个 NAL 或 FU-A 分片各触发一次)
    void inputH264(const uint8_t* data, size_t len, uint32_t timestamp, RtpPacketCallback cb);
    void inputH265(const uint8_t* data, size_t len, uint32_t timestamp, RtpPacketCallback cb);
    void inputPCMA(const uint8_t* data, size_t len, uint32_t timestamp, RtpPacketCallback cb);

    const MediaTrackInfo& trackInfo() const { return _track; }

private:
    RtpContext(const MediaTrackInfo& track, uint32_t ssrc);

    void writeRtpHeader(uint8_t* buf, uint8_t pt, bool marker, uint32_t timestamp);

    void packetizeH264Nalu(const uint8_t* nalu, size_t len, uint32_t timestamp, RtpPacketCallback cb);
    void packetizeH265Nalu(const uint8_t* nalu, size_t len, uint32_t timestamp, RtpPacketCallback cb);

    MediaTrackInfo _track;
    uint16_t _seq{1};
    uint32_t _ssrc{0};

    // 缓存缓冲区（TCP 长度前缀 + RTP 头 + 负载）
    std::vector<uint8_t> _video_buf;
    std::vector<uint8_t> _audio_buf;
};

} // namespace sipB

#endif //SIPB_RTP_CONTEXT_H
