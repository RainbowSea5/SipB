#ifndef SIPB_RTP_CONTEXT_H
#define SIPB_RTP_CONTEXT_H

#include <cstdint>
#include <string>
#include <functional>
#include <memory>
#include <vector>

namespace toolkit {
    class EventPoller;
    class UdpClient;
    class TcpClient;
    class Buffer;
}

namespace sipB {

// 传输模式
enum class TransportMode : uint8_t {
    UDP,
    TCP,  // RFC 4571: 2 字节长度前缀 + RTP 包
};

// 传输方向
enum class TransportDirection : uint8_t {
    SENDONLY,  // 仅发送
    RECVONLY,  // 仅接收
    SENDRECV,  // 双向（对讲）
};

// 媒体编码类型
enum class MediaCodec : uint8_t {
    H264,
    H265,
    PCMA,
};

// 媒体轨道信息
struct MediaTrackInfo {
    MediaCodec codec{MediaCodec::H264};
    uint8_t payload_type{0};
    uint32_t clock_rate{0};
    std::string remote_ip;
    uint16_t remote_port{0};
    TransportMode mode{TransportMode::UDP};
    TransportDirection direction{TransportDirection::RECVONLY};
};

// 单路 RTP 媒体流上下文，管理 SDP 协商和 RTP 收发
class RtpContext : public std::enable_shared_from_this<RtpContext> {
public:
    using Ptr = std::shared_ptr<RtpContext>;

    static Ptr create(const std::string& remote_sdp,
                      const std::string& local_ip,
                      uint16_t local_port,
                      const std::shared_ptr<toolkit::EventPoller>& poller);

    ~RtpContext();

    std::string makeResponseSdp() const;

    // 输入编码后数据打包 RTP 发送 (时间戳单位: H264/H265=90000Hz, PCMA=8000Hz)
    void inputH264(const uint8_t* data, size_t len, uint32_t timestamp);
    void inputH265(const uint8_t* data, size_t len, uint32_t timestamp);
    void inputPCMA(const uint8_t* data, size_t len, uint32_t timestamp);

    // PCMA 接收回调
    void setOnPcmData(std::function<void(const uint8_t* data, size_t len)> cb);

    // 预留：通知上层需要关键帧
    void setOnRequestKeyFrame(std::function<void()> cb);

    // 预留：通知上层需要编码音频数据
    void setOnRequestAudioFrame(std::function<void()> cb);

    const MediaTrackInfo& trackInfo() const { return _track; }

private:
    explicit RtpContext(std::shared_ptr<toolkit::EventPoller> poller);

    bool init(const std::string& remote_sdp, uint16_t local_port, const std::string& local_ip);
    bool parseRemoteSdp(const std::string& sdp);

    bool setupUdpTransport();
    bool setupTcpTransport();

    void buildRtpHeader(uint8_t* buf, uint8_t pt, bool marker, uint32_t timestamp);
    void sendRtpPacket(const uint8_t* data, size_t len);
    void sendTcpRtpPacket(const uint8_t* data, size_t len);

    void packetizeH264Nalu(const uint8_t* nalu, size_t len, uint32_t timestamp);
    void packetizeH265Nalu(const uint8_t* nalu, size_t len, uint32_t timestamp);

    // TCP 接收缓冲 (RFC 4571: 2 字节长度 + RTP 包)
    std::vector<uint8_t> _tcp_recv_buf;
    bool _tcp_reading_len{true};
    uint16_t _tcp_pending_len{0};

    MediaTrackInfo _track;
    std::string _local_ip;
    uint16_t _local_port{0};
    uint16_t _seq{0};
    uint32_t _ssrc{0};

    std::shared_ptr<toolkit::EventPoller> _poller;
    std::shared_ptr<toolkit::UdpClient> _udp_client;
    std::shared_ptr<toolkit::TcpClient> _tcp_client;

    std::function<void(const uint8_t* data, size_t len)> _on_pcm_data;
    std::function<void()> _on_request_key_frame;
    std::function<void()> _on_request_audio_frame;
};

} // namespace sipB

#endif //SIPB_RTP_CONTEXT_H
