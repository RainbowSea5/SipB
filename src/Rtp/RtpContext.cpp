#include "RtpContext.h"

#include <cstring>
#include <cstdlib>
#include <sstream>

#include "Network/sockutil.h"
#include "Network/UdpClient.h"
#include "Poller/EventPoller.h"
#include "Util/logger.h"

using namespace toolkit;
using namespace std;

namespace sipB {
//==============================================================================
// RTP 固定头工具
//==============================================================================
static void writeRtpHeader(uint8_t *buf, uint8_t pt, bool marker,
                           uint16_t seq, uint32_t ts, uint32_t ssrc) {
    buf[0] = 0x80;
    buf[1] = (marker ? 0x80 : 0) | (pt & 0x7F);
    buf[2] = (seq >> 8) & 0xFF;
    buf[3] = seq & 0xFF;
    buf[4] = (ts >> 24) & 0xFF;
    buf[5] = (ts >> 16) & 0xFF;
    buf[6] = (ts >> 8) & 0xFF;
    buf[7] = ts & 0xFF;
    buf[8] = (ssrc >> 24) & 0xFF;
    buf[9] = (ssrc >> 16) & 0xFF;
    buf[10] = (ssrc >> 8) & 0xFF;
    buf[11] = ssrc & 0xFF;
}

//==============================================================================
// RtpContext
//==============================================================================

RtpContext::RtpContext(shared_ptr<EventPoller> poller)
    : _poller(std::move(poller)) {
}

RtpContext::~RtpContext() {
    if (_udp_client) _udp_client->shutdown();
}

RtpContext::Ptr RtpContext::create(const string &remote_sdp,
                                   const string &local_ip,
                                   uint16_t local_port,
                                   const shared_ptr<EventPoller> &poller) {
    auto ctx = Ptr(new RtpContext(poller));
    if (!ctx->init(remote_sdp, local_port, local_ip)) {
        return nullptr;
    }
    return ctx;
}

//==============================================================================
// SDP 解析
//==============================================================================
bool RtpContext::parseRemoteSdp(const string &sdp) {
    string sdp_text = sdp;
    auto pos = sdp_text.find("v=0");
    if (pos == string::npos) {
        ErrorL << "SDP 中未找到 v=0";
        return false;
    }
    if (pos > 0) sdp_text = sdp_text.substr(pos);

    istringstream stream(sdp_text);
    string line;
    string remote_ip;
    int media_port = 0;
    bool has_rtpmap = false;

    while (getline(stream, line)) {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        if (line.empty())
            continue;

        // c=IN IP4 <ip>
        if (line[0] == 'c' && line.size() > 2 && line[1] == '=') {
            auto val = line.substr(2);
            auto p = val.find("IP4 ");
            if (p != string::npos)
                remote_ip = val.substr(p + 4);
            else if ((p = val.find("IP6 ")) != string::npos)
                remote_ip = val.substr(p + 4);
            remote_ip.erase(0, remote_ip.find_first_not_of(" \t"));
            auto sp = remote_ip.find_first_of(" \t\r");
            if (sp != string::npos)
                remote_ip = remote_ip.substr(0, sp);
        } else if (line[0] == 'm' && line.size() > 2 && line[1] == '=') {
            // m=video|audio <port> <proto> <pt...>
            auto val = line.substr(2);
            istringstream mss(val);
            string media_type, proto;
            mss >> media_type >> media_port >> proto;

            // 检测传输模式: TCP/RTP/AVP → TCP, RTP/AVP → UDP
            _track.mode = (proto.find("TCP") != string::npos)? TransportMode::TCP: TransportMode::UDP;
            // 跳过 payload type 列表
        }else if (line[0] == 'a' && line.size() > 2 && line[1] == '=') {
            auto val = line.substr(2);

            if (val == "sendonly")
                _track.direction = TransportDirection::RECVONLY;
            else if (val == "recvonly")
                _track.direction = TransportDirection::SENDONLY;
            else if (val == "sendrecv")
                _track.direction = TransportDirection::SENDRECV;

            if (val.find("rtpmap:") == 0) {
                auto sub = val.substr(7);
                auto ci = sub.find(' ');
                if (ci == string::npos)
                    continue;
                int pt = atoi(sub.substr(0, ci).c_str());
                auto codec_part = sub.substr(ci + 1);
                auto slash = codec_part.find('/');
                if (slash == string::npos) continue;
                string name = codec_part.substr(0, slash);
                uint32_t clock = atoi(codec_part.substr(slash + 1).c_str());

                if (!has_rtpmap) {
                    auto upper = name;
                    for (auto &c: upper) c = toupper(c);
                    if (upper == "H264") {
                        _track = {
                            MediaCodec::H264, (uint8_t) pt, clock,
                            remote_ip, (uint16_t) media_port, _track.mode, _track.direction
                        };
                        has_rtpmap = true;
                    } else if (upper == "H265") {
                        _track = {
                            MediaCodec::H265, (uint8_t) pt, clock,
                            remote_ip, (uint16_t) media_port, _track.mode, _track.direction
                        };
                        has_rtpmap = true;
                    } else if (upper == "PCMA" || upper == "G711A" || pt == 8) {
                        _track = {
                            MediaCodec::PCMA, (uint8_t) pt, clock > 0 ? clock : 8000,
                            remote_ip, (uint16_t) media_port, _track.mode, _track.direction
                        };
                        has_rtpmap = true;
                    }
                }
            }
        }
    }

    if (media_port <= 0 || remote_ip.empty()) {
        ErrorL << "SDP 解析失败: 未找到有效的媒体地址或端口";
        return false;
    }
    _track.remote_ip = remote_ip;
    _track.remote_port = (uint16_t) media_port;

    InfoL << "SDP 解析完成: codec=" << (int) _track.codec
            << " pt=" << (int) _track.payload_type
            << " clock=" << _track.clock_rate
            << " mode=" << (_track.mode == TransportMode::UDP ? "UDP" : "TCP")
            << " ip=" << _track.remote_ip << ":" << _track.remote_port;
    return has_rtpmap;
}

bool RtpContext::init(const string &remote_sdp, uint16_t local_port, const string &local_ip) {
    _local_ip = local_ip;
    _local_port = local_port;
    if (!parseRemoteSdp(remote_sdp)) {
        ErrorL << "SDP 解析失败";
        return false;
    }

    srand((unsigned) time(nullptr));
    _ssrc = (uint32_t) rand();
    _seq = (uint16_t) rand();

    return _track.mode == TransportMode::TCP ? setupTcpTransport() : setupUdpTransport();
}

//==============================================================================
// UDP 传输 (ZLToolKit UdpClient)
//==============================================================================
bool RtpContext::setupUdpTransport() {
    _udp_client = make_shared<UdpClient>(_poller);
    _udp_client->setNetAdapter(_local_ip);
    _udp_client->startConnect(_track.remote_ip, _track.remote_port, _local_port);
    if (!_udp_client->alive()) {
        ErrorL << "UDP Client 启动失败";
        return false;
    }

    // RECVONLY / SENDRECV 时需要接收
    if (_track.direction == TransportDirection::RECVONLY ||
        _track.direction == TransportDirection::SENDRECV) {
        auto weak_self = weak_from_this();
        _udp_client->setOnRecvFrom([weak_self](const Buffer::Ptr &buf,
                                               struct sockaddr *, int) {
            auto self = weak_self.lock();
            if (!self) return;
            const uint8_t *d = (const uint8_t *) buf->data();
            size_t n = buf->size();
            if (n < 12) return;
            if (self->_track.codec == MediaCodec::PCMA && self->_on_pcm_data) {
                self->_on_pcm_data(d + 12, n - 12);
            }
        });
    }

    InfoL << "UDP 传输就绪, local=" << _local_ip << ":" << _local_port
            << " remote=" << _track.remote_ip << ":" << _track.remote_port;
    return true;
}

//==============================================================================
// TCP 传输 (RFC 4571: 2 字节长度前缀 + RTP)
//==============================================================================
bool RtpContext::setupTcpTransport() {
    WarnL << "TCP RTP 暂未完整实现, 回退 UDP 模式";
    _track.mode = TransportMode::UDP;
    return setupUdpTransport();
}

//==============================================================================
// RTP 发包
//==============================================================================
void RtpContext::buildRtpHeader(uint8_t *buf, uint8_t pt, bool marker, uint32_t timestamp) {
    writeRtpHeader(buf, pt, marker, _seq, timestamp, _ssrc);
    ++_seq;
}

void RtpContext::sendRtpPacket(const uint8_t *data, size_t len) {
    if (_track.mode == TransportMode::UDP && _udp_client) {
        _udp_client->send((const char *) data, len);
    }
}

void RtpContext::sendTcpRtpPacket(const uint8_t *data, size_t len) {
    // RFC 4571: 2 字节大端长度 + RTP 包
    uint8_t hdr[2] = {(uint8_t) (len >> 8), (uint8_t) len};
    sendRtpPacket(hdr, 2);
    sendRtpPacket(data, len);
}

//==============================================================================
// H264 打包
//==============================================================================
void RtpContext::packetizeH264Nalu(const uint8_t *nalu, size_t len, uint32_t timestamp) {
    if (len < 1) return;
    uint8_t nal_type = nalu[0] & 0x1F;
    uint8_t nal_ref = (nalu[0] >> 5) & 0x03;
    const size_t MAX = 1400;

    if (len <= MAX) {
        uint8_t pkt[12 + len];
        buildRtpHeader(pkt, _track.payload_type, true, timestamp);
        memcpy(pkt + 12, nalu, len);
        sendRtpPacket(pkt, 12 + len);
        return;
    }

    // FU-A
    const uint8_t *pay = nalu + 1;
    size_t pay_len = len - 1, off = 0;
    const size_t fu_max = MAX - 2;
    while (off < pay_len) {
        size_t frag = min(fu_max, pay_len - off);
        bool first = off == 0, last = off + frag >= pay_len;
        uint8_t pkt[12 + 2 + frag];
        buildRtpHeader(pkt, _track.payload_type, last, timestamp);
        pkt[12] = 0x7C | (nal_ref << 5);
        pkt[13] = (first ? 0x80 : 0) | (last ? 0x40 : 0) | nal_type;
        memcpy(pkt + 14, pay + off, frag);
        sendRtpPacket(pkt, 12 + 2 + frag);
        off += frag;
    }
}

void RtpContext::inputH264(const uint8_t *data, size_t len, uint32_t ts) {
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
    if (nlen < 1) return;
    packetizeH264Nalu(nalu, nlen, ts);
}

//==============================================================================
// H265 打包
//==============================================================================
void RtpContext::packetizeH265Nalu(const uint8_t *nalu, size_t len, uint32_t timestamp) {
    if (len < 2) return;
    uint8_t nal_type = (nalu[0] >> 1) & 0x3F;
    const size_t MAX = 1400;

    if (len <= MAX) {
        uint8_t pkt[12 + len];
        buildRtpHeader(pkt, _track.payload_type, true, timestamp);
        memcpy(pkt + 12, nalu, len);
        sendRtpPacket(pkt, 12 + len);
        return;
    }

    const uint8_t *pay = nalu + 2;
    size_t pay_len = len - 2, off = 0;
    const size_t fu_max = MAX - 3;
    while (off < pay_len) {
        size_t frag = min(fu_max, pay_len - off);
        bool first = off == 0, last = off + frag >= pay_len;
        uint8_t pkt[12 + 3 + frag];
        buildRtpHeader(pkt, _track.payload_type, last, timestamp);
        pkt[12] = (nalu[0] & 0x80) | (49 << 1) | ((nalu[1] >> 7) & 1);
        pkt[13] = (nalu[1] & 0x7F) | 0x80;
        pkt[14] = (first ? 0x80 : 0) | (last ? 0x40 : 0) | (nal_type & 0x3F);
        memcpy(pkt + 15, pay + off, frag);
        sendRtpPacket(pkt, 12 + 3 + frag);
        off += frag;
    }
}

void RtpContext::inputH265(const uint8_t *data, size_t len, uint32_t ts) {
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
    if (nlen < 2) return;
    packetizeH265Nalu(nalu, nlen, ts);
}

//==============================================================================
// PCMA
//==============================================================================
void RtpContext::inputPCMA(const uint8_t *data, size_t len, uint32_t timestamp) {
    if (!len) return;
    uint8_t pkt[12 + len];
    buildRtpHeader(pkt, _track.payload_type, true, timestamp);
    memcpy(pkt + 12, data, len);
    sendRtpPacket(pkt, 12 + len);
}

//==============================================================================
// 响应 SDP
//==============================================================================
string RtpContext::makeResponseSdp() const {
    const char *dir;
    switch (_track.direction) {
        case TransportDirection::SENDONLY: dir = "recvonly";
            break;
        case TransportDirection::RECVONLY: dir = "sendonly";
            break;
        case TransportDirection::SENDRECV: dir = "sendrecv";
            break;
        default: dir = "sendonly";
            break;
    }

    const char *codec_name;
    switch (_track.codec) {
        case MediaCodec::H264: codec_name = "H264";
            break;
        case MediaCodec::H265: codec_name = "H265";
            break;
        case MediaCodec::PCMA: codec_name = "PCMA";
            break;
        default: codec_name = "H264";
            break;
    }

    const char *proto = (_track.mode == TransportMode::TCP) ? "TCP/RTP/AVP" : "RTP/AVP";

    ostringstream oss;
    oss << "v=0\r\n"
            << "o=- 0 0 IN IP4 " << _local_ip << "\r\n"
            << "s=Session SDP\r\n"
            << "c=IN IP4 " << _local_ip << "\r\n"
            << "t=0 0\r\n"
            << "m=video " << _local_port << " " << proto << " "
            << (int) _track.payload_type << "\r\n"
            << "a=" << dir << "\r\n"
            << "a=rtpmap:" << (int) _track.payload_type
            << " " << codec_name << "/" << _track.clock_rate << "\r\n";
    return oss.str();
}

//==============================================================================
// 回调
//==============================================================================
void RtpContext::setOnPcmData(function<void(const uint8_t *, size_t)> cb) { _on_pcm_data = move(cb); }
void RtpContext::setOnRequestKeyFrame(function<void()> cb) { _on_request_key_frame = move(cb); }
void RtpContext::setOnRequestAudioFrame(function<void()> cb) { _on_request_audio_frame = move(cb); }
} // namespace sipB
