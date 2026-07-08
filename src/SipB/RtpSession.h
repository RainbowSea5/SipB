#ifndef SIPB_RTP_SESSION_H
#define SIPB_RTP_SESSION_H

#include <cstdint>
#include <string>
#include <functional>
#include <memory>
#include <vector>
#include <utility>
#include "Network/UdpClient.h"
#include "Poller/EventPoller.h"
#include "Rtp/RtpContext.h"
#include "Util/logger.h"

namespace sipB {
// 传输模式
enum class TransportMode : uint8_t {
    UDP,
    TCP,
};

// 传输方向
enum class TransportDirection : uint8_t {
    SENDONLY,
    RECVONLY,
    SENDRECV,
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
    // from m= line
    std::string media_type;
};

// RTP 会话 - 解析 SDP、收发 RTP、支持双向
class RtpSession : public std::enable_shared_from_this<RtpSession> {
public:
    using Ptr = std::shared_ptr<RtpSession>;

    static Ptr create(const std::string &offer_sdp);

    ~RtpSession();

    // 生成 answer SDP
    std::string makeAnswerSdp(const std::string& local_ip, uint16_t local_port);

    // 发送 RTP (时间戳单位: H264/H265=90000Hz, PCMA=8000Hz)
    void inputH264(const uint8_t *data, size_t len, uint32_t timestamp);

    void inputH265(const uint8_t *data, size_t len, uint32_t timestamp);

    void inputPCMA(const uint8_t *data, size_t len, uint32_t timestamp);

    // PCMA 接收回调
    void setOnPcmData(std::function<void(const uint8_t *data, size_t len)> cb);

    // 预留: 通知上层需要关键帧
    void setOnRequestKeyFrame(std::function<void()> cb);

    // 预留: 通知上层需要编码音频数据
    void setOnRequestAudioFrame(std::function<void()> cb);

    const MediaTrackInfo &trackInfo() const { return _selected_track; }
    const std::string &sessionName() const { return _session_name; }

    // 获取所有解析出的轨道
    const std::vector<MediaTrackInfo> &offerTracks() const { return _offer_tracks; }

    int getIPProto() const;

    void onStart(const std::function<void(const toolkit::SockException& ex)>& on_error);

    bool isUdp() const;
    void setCid(int cid){_cid = cid;}
    int getCid() const {return _cid;}
private:
    RtpSession();

    bool init(const std::string &offer_sdp);

    bool parseOfferSdp(const std::string &offer_sdp);
    bool resolveTrack();

    void sendRtpPacket(const uint8_t* data, size_t len);

    void onRecv(char* data, size_t len);
    void onRecvRtp(char* data, size_t len);
    void onRecvRtcp(char* data, size_t len);

    std::vector<MediaTrackInfo> _offer_tracks;
    MediaTrackInfo _selected_track;
    std::shared_ptr<rtp::RtpContext> _packetizer;
    // std::shared_ptr<toolkit::UdpClient> _udp_client;
    toolkit::Socket::Ptr _client;

    // session 级属性
    std::string _session_name;
    uint32_t _ssrc{0};
    std::string _remote_ip;
    TransportDirection _direction{TransportDirection::RECVONLY};
    TransportMode _mode{TransportMode::UDP};

    std::string _local_ip;
    uint16_t _local_port{0};
    // answer SDP 缓存（仅生成一次）
    std::string _answer_sdp;
    bool _answer_generated{false};

    // 接收回调
    std::function<void(const uint8_t* data, size_t len)> _on_pcm_data;
    std::function<void()> _on_request_key_frame;
    std::function<void()> _on_request_audio_frame;
    int _cid;
};
} // namespace sipB

#endif //SIPB_RTP_SESSION_H
