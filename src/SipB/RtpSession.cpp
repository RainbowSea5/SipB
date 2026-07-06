#include "RtpSession.h"
#include "Rtp/RtpContext.h"

#include <cstdlib>
#include <sstream>

using namespace toolkit;
using namespace std;

namespace sipB {

RtpSession::RtpSession(shared_ptr<EventPoller> poller)
    : _poller(std::move(poller)) {}

RtpSession::~RtpSession() {
    if (_udp_client) {
        _udp_client->shutdown();
    }
}

RtpSession::Ptr RtpSession::create(const string& offer_sdp,
                                    const string& local_ip,
                                    uint16_t local_port,
                                    const shared_ptr<EventPoller>& poller) {
    auto session = Ptr(new RtpSession(poller));
    if (!session->init(offer_sdp, local_port, local_ip)) {
        return nullptr;
    }
    return session;
}

bool RtpSession::init(const string& offer_sdp, uint16_t local_port, const string& local_ip) {
    _local_ip = local_ip;
    _local_port = local_port;

    if (!parseOfferSdp(offer_sdp)) {
        ErrorL << "解析 offer SDP 失败";
        return false;
    }

    if (!resolveTrack()) {
        ErrorL << "未找到支持的编码 (H264 > H265, PCMA)";
        return false;
    }

    // 创建打包器（纯打包，不负责发送）
    _packetizer = ::rtp::RtpContext::create(_selected_track.payload_type, _ssrc);
    if (!_packetizer) {
        ErrorL << "创建打包器失败";
        return false;
    }

    // 建立传输通道
    setupUdpTransport();

    return true;
}

//==============================================================================
// UDP 传输
//==============================================================================
void RtpSession::setupUdpTransport() {
    _udp_client = make_shared<UdpClient>(_poller);
    _udp_client->setNetAdapter(_local_ip);
    _udp_client->startConnect(_remote_ip, _selected_track.remote_port, _local_port);
    if (!_udp_client->alive()) {
        ErrorL << "UDP 连接失败 remote=" << _remote_ip << ":" << _selected_track.remote_port;
        return;
    }

    // 需要接收时注册回调
    if (_direction == TransportDirection::RECVONLY ||_direction == TransportDirection::SENDRECV) {
        auto weak_self = weak_from_this();
        _udp_client->setOnRecvFrom([weak_self](const Buffer::Ptr& buf,
                                                struct sockaddr*, int) {
            auto self = weak_self.lock();
            if (!self) {
                return;
            }
            const uint8_t* d = (const uint8_t*)buf->data();
            size_t n = buf->size();
            if (n < 12) {
                return;
            }
            if (self->_selected_track.codec == MediaCodec::PCMA && self->_on_pcm_data) {
                self->_on_pcm_data(d + 12, n - 12);
            }
        });
    }

    InfoL << "UDP 传输就绪, local=" << _local_ip << ":" << _local_port
          << " remote=" << _remote_ip << ":" << _selected_track.remote_port;
}

void RtpSession::sendRtpPacket(const uint8_t* data, size_t len) {
    if (!_udp_client) {
        return;
    }
    if (_mode == TransportMode::UDP) {
        // UDP: 跳过前 2 字节 TCP 长度前缀
        if (len > 2) {
            _udp_client->send(reinterpret_cast<const char*>(data + 2), len - 2);
        }
    } else {
        // TCP: 整包发送（含 2 字节 TCP 长度前缀）
        _udp_client->send(reinterpret_cast<const char*>(data), len);
    }
}

void RtpSession::onRecvRtp(const uint8_t* data, size_t len) {
    if (len < 12) {
        return;
    }
    if (_selected_track.codec == MediaCodec::PCMA && _on_pcm_data) {
        _on_pcm_data(data + 12, len - 12);
    }
}

//==============================================================================
// 发送 API — 打包后自动发送
//==============================================================================
void RtpSession::inputH264(const uint8_t* data, size_t len, uint32_t timestamp) {
    if (_packetizer) {
        _packetizer->inputH264(data, len, timestamp, [this](const uint8_t* pkt, size_t n) {
            sendRtpPacket(pkt, n);
        });
    }
}

void RtpSession::inputH265(const uint8_t* data, size_t len, uint32_t timestamp) {
    if (_packetizer) {
        _packetizer->inputH265(data, len, timestamp, [this](const uint8_t* pkt, size_t n) {
            sendRtpPacket(pkt, n);
        });
    }
}

void RtpSession::inputPCMA(const uint8_t* data, size_t len, uint32_t timestamp) {
    if (_packetizer) {
        _packetizer->inputPCMA(data, len, timestamp, [this](const uint8_t* pkt, size_t n) {
            sendRtpPacket(pkt, n);
        });
    }
}

//==============================================================================
// 接收回调
//==============================================================================
void RtpSession::setOnPcmData(function<void(const uint8_t*, size_t)> cb) {
    _on_pcm_data = move(cb);
}

void RtpSession::setOnRequestKeyFrame(function<void()> cb) {
    _on_request_key_frame = move(cb);
}

void RtpSession::setOnRequestAudioFrame(function<void()> cb) {
    _on_request_audio_frame = move(cb);
}

//==============================================================================
// 轨道选择
//==============================================================================
bool RtpSession::resolveTrack() {
    MediaTrackInfo* best = nullptr;

    for (auto& t : _offer_tracks) {
        if (t.media_type == "video") {
            if (!best) {
                best = &t;
            } else if (t.codec == MediaCodec::H264 && best->codec != MediaCodec::H264) {
                best = &t;
            } else if (t.codec == MediaCodec::H265 && best->codec != MediaCodec::H264
                       && best->codec != MediaCodec::H265) {
                best = &t;
            }
        }
    }

    if (!best) {
        for (auto& t : _offer_tracks) {
            if (t.media_type == "audio" && t.codec == MediaCodec::PCMA) {
                best = &t;
                break;
            }
        }
    }

    if (!best) {
        return false;
    }
    _selected_track = *best;
    return true;
}

//==============================================================================
// SDP 解析
//==============================================================================
bool RtpSession::parseOfferSdp(const string& offer_sdp) {
    _offer_tracks.clear();

    string sdp = offer_sdp;
    auto pos = sdp.find("v=0");
    if (pos == string::npos) {
        ErrorL << "SDP 中未找到 v=0";
        return false;
    }
    if (pos > 0) {
        sdp = sdp.substr(pos);
    }

    istringstream stream(sdp);
    string line;
    string cur_media_type;
    int cur_media_port = 0;
    TransportMode cur_mode = TransportMode::UDP;
    TransportDirection cur_dir = TransportDirection::RECVONLY;

    while (getline(stream, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (line.empty()) {
            continue;
        }

        if (line[0] == 'c' && line.size() > 2 && line[1] == '=') {
            auto val = line.substr(2);
            auto p = val.find("IP4 ");
            if (p != string::npos) {
                _remote_ip = val.substr(p + 4);
            } else if ((p = val.find("IP6 ")) != string::npos) {
                _remote_ip = val.substr(p + 4);
            }
            _remote_ip.erase(0, _remote_ip.find_first_not_of(" \t"));
            auto sp = _remote_ip.find_first_of(" \t\r");
            if (sp != string::npos) {
                _remote_ip = _remote_ip.substr(0, sp);
            }
        } else if (line[0] == 'm' && line.size() > 2 && line[1] == '=') {
            auto val = line.substr(2);
            istringstream mss(val);
            string proto;
            mss >> cur_media_type >> cur_media_port >> proto;
            cur_mode = (proto.find("TCP") != string::npos) ? TransportMode::TCP : TransportMode::UDP;
            cur_dir = TransportDirection::RECVONLY;
        } else if (line[0] == 's' && line.size() > 2 && line[1] == '=') {
            _session_name = line.substr(2);
        } else if (line[0] == 'y' && line.size() > 2 && line[1] == '=') {
            _ssrc = (uint32_t)atol(line.substr(2).c_str());
        } else if (line[0] == 'a' && line.size() > 2 && line[1] == '=') {
            auto val = line.substr(2);

            if (val == "sendonly") {
                cur_dir = TransportDirection::RECVONLY;
            } else if (val == "recvonly") {
                cur_dir = TransportDirection::SENDONLY;
            } else if (val == "sendrecv") {
                cur_dir = TransportDirection::SENDRECV;
            }

            if (val.find("rtpmap:") == 0 && !cur_media_type.empty()) {
                auto sub = val.substr(7);
                auto ci = sub.find(' ');
                if (ci == string::npos) {
                    continue;
                }
                int pt = atoi(sub.substr(0, ci).c_str());
                auto codec_part = sub.substr(ci + 1);
                auto slash = codec_part.find('/');
                if (slash == string::npos) {
                    continue;
                }
                string name = codec_part.substr(0, slash);
                uint32_t clock = atoi(codec_part.substr(slash + 1).c_str());

                auto upper = name;
                for (auto& c : upper) {
                    c = toupper(c);
                }

                MediaTrackInfo track;
                track.remote_ip = _remote_ip;
                track.remote_port = (uint16_t)cur_media_port;
                track.media_type = cur_media_type;
                track.payload_type = (uint8_t)pt;
                track.clock_rate = clock;

                if (upper == "H264") {
                    track.codec = MediaCodec::H264;
                    _offer_tracks.push_back(track);
                } else if (upper == "H265") {
                    track.codec = MediaCodec::H265;
                    _offer_tracks.push_back(track);
                } else if (upper == "PCMA" || upper == "G711A" || pt == 8) {
                    track.codec = MediaCodec::PCMA;
                    track.clock_rate = clock > 0 ? clock : 8000;
                    _offer_tracks.push_back(track);
                }
            }
        }
    }

    _mode = cur_mode;
    _direction = cur_dir;

    if (_offer_tracks.empty()) {
        ErrorL << "SDP 解析完成，但未找到支持的编码";
        return false;
    }

    InfoL << "SDP 解析完成，共 " << _offer_tracks.size() << " 条轨道";
    return true;
}

//==============================================================================
// SDP 生成
//==============================================================================
string RtpSession::makeAnswerSdp() const {
    const char* dir;
    switch (_direction) {
        case TransportDirection::SENDONLY: {
            dir = "recvonly";
            break;
        }
        case TransportDirection::RECVONLY: {
            dir = "sendonly";
            break;
        }
        case TransportDirection::SENDRECV: {
            dir = "sendrecv";
            break;
        }
        default: {
            dir = "sendonly";
            break;
        }
    }

    const char* codec_name;
    switch (_selected_track.codec) {
        case MediaCodec::H264: {
            codec_name = "H264";
            break;
        }
        case MediaCodec::H265: {
            codec_name = "H265";
            break;
        }
        case MediaCodec::PCMA: {
            codec_name = "PCMA";
            break;
        }
        default: {
            codec_name = "H264";
            break;
        }
    }

    const char* proto = (_mode == TransportMode::TCP) ? "TCP/RTP/AVP" : "RTP/AVP";

    ostringstream oss;
    oss << "v=0\r\n"
            << "o=- 0 0 IN IP4 " << _local_ip << "\r\n"
            << "s=" << _session_name << "\r\n"
            << "c=IN IP4 " << _local_ip << "\r\n"
            << "t=0 0\r\n"
            << "m=" << _selected_track.media_type << " " << _local_port << " " << proto << " "
            << (int)_selected_track.payload_type << "\r\n"
            << "y=" << _ssrc << "\r\n"
            << "a=" << dir << "\r\n"
            << "a=rtpmap:" << (int)_selected_track.payload_type
            << " " << codec_name << "/" << _selected_track.clock_rate << "\r\n";
    return oss.str();
}

} // namespace sipB
