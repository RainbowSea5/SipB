#ifndef RTP_RTP_CONTEXT_H
#define RTP_RTP_CONTEXT_H

#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

namespace rtp {

class RtpContext : public std::enable_shared_from_this<RtpContext> {
public:
    using Ptr = std::shared_ptr<RtpContext>;
    using RtpPacketCallback = std::function<void(const uint8_t* data, size_t len)>;

    static Ptr create(uint8_t payload_type, uint32_t ssrc);

    void inputH264(const uint8_t* data, size_t len, uint32_t timestamp, RtpPacketCallback cb);
    void inputH265(const uint8_t* data, size_t len, uint32_t timestamp, RtpPacketCallback cb);
    void inputPCMA(const uint8_t* data, size_t len, uint32_t timestamp, RtpPacketCallback cb);

    uint8_t payloadType() const { return _payload_type; }

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
};

} // namespace rtp

#endif //RTP_RTP_CONTEXT_H
