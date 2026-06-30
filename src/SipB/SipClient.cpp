#include "SipClient.h"

#include <iostream>
#include <cstring>
#include <cstdlib>
#include <utility>
#include <sys/socket.h>   // AF_INET
#include <netinet/in.h>   // IPPROTO_UDP
using namespace toolkit;

namespace SipB {
SipClient::SipClient(toolkit::EventPoller::Ptr poller)
    : _ex_ctx(nullptr)
      , _running(false)
      , _registered(false)
      , _server_port(5060)
      , _expires(3600)
      , _poller(std::move(poller)) {
}

SipClient::~SipClient() {
    stop();
}

bool SipClient::init(bool is_udp, int local_port) {
    if (_ex_ctx) {
        WarnL << "已经初始化了";
        return true;
    }
    _ex_ctx = eXosip_malloc();
    if (_ex_ctx == nullptr) {
        ErrorL << "malloc failed";
        return false;
    }

    if (eXosip_init(_ex_ctx) != 0) {
        ErrorL << "eXosip_init failed";
        eXosip_quit(_ex_ctx);
        _ex_ctx = nullptr;
        return false;
    }

    auto transport = is_udp ? IPPROTO_UDP : IPPROTO_TCP;
    if (eXosip_listen_addr(_ex_ctx, transport, nullptr, local_port, AF_INET, 0) != 0) {
        ErrorL << "eXosip_listen_addr failed on port " << local_port;
        eXosip_quit(_ex_ctx);
        _ex_ctx = nullptr;
        return false;
    }

    InfoL << "eXosip initialized, listening on UDP " << local_port;
    return true;
}

bool SipClient::startRegister(const std::string &server_ip, int server_port,const std::string &device_code,
    const std::string &domain,const std::string &auth_user,
    const std::string &auth_pass,int expires,StatusCallback callback) {

    if (_ex_ctx == nullptr) {
        ErrorL << "eXosip not initialized";
        return false;
    }

    _server_ip = server_ip;
    _server_port = server_port;
    _device_code = device_code;
    _domain = domain;
    _auth_user = auth_user;
    _auth_pass = auth_pass;
    _expires = expires;
    _callback = callback;

    // 设置 Digest 认证信息 —— eXosip 在收到 401/407 后自动使用
    // 这些信息计算 Authorization 头并重发 REGISTER
    eXosip_clear_authentication_info(_ex_ctx);
    eXosip_add_authentication_info(_ex_ctx,
                                   _auth_user.c_str(), // username
                                   _auth_user.c_str(), // sip_user
                                   _domain.c_str(), // sip_domain
                                   _auth_pass.c_str(), // password
                                   nullptr); // ha1: 自动计算

    _running = true;
    _registered = false;
    _event_thread = std::thread(&SipClient::eventLoop, this);

    sendInitialRegister();

    return true;
}

void SipClient::stop() {
    if (!_running) {
        return;
    }

    _running = false;

    if (_event_thread.joinable()) {
        _event_thread.join();
    }

    // 发送注销（expires = 0 的 REGISTER）
    if (_ex_ctx) {
        osip_message_t *reg = nullptr;
        if (eXosip_register_build_register(_ex_ctx,_register_id, 0, &reg) == 0 && reg) {
            eXosip_register_send_register(_ex_ctx,_register_id, reg);
        }
    }

    if (_ex_ctx) {
        eXosip_quit(_ex_ctx);
        _ex_ctx = nullptr;
    }

    _registered = false;
    InfoL << "stopped";
}

void SipClient::sendInitialRegister() {
    // 电网B协议 URI 格式: sip:设备编码@域名
    std::string user_uri = "sip:" + _device_code + "@" + _domain;

    osip_message_t *reg_message = nullptr;
    int ret = eXosip_register_build_initial_register(_ex_ctx, user_uri.c_str(),
                                                     user_uri.c_str(), user_uri.c_str(), _expires, &reg_message);

    if (ret < 0 || reg_message == nullptr) {
        ErrorL << "Failed to build initial REGISTER (ret=" << ret << ")";
        if (_callback) {
            _callback(false, "eXosip_register_build_initial_register failed");
        }
        return;
    }
    _register_id = ret;

    ret = eXosip_register_send_register(_ex_ctx, _register_id,reg_message);
    if (ret != 0) {
        ErrorL << "Failed to send REGISTER (ret=" << ret << ")";
        if (_callback) {
            _callback(false, "eXosip_register_send_register failed");
        }
    } else {
        InfoL << "Initial REGISTER sent (expires=" << _expires << "s)";
    }
}

void SipClient::sendRefreshRegister() {
    osip_message_t *reg = nullptr;
    int ret = eXosip_register_build_register(_ex_ctx, _register_id, _expires, &reg);
    if (ret == 0 && reg) {
        eXosip_register_send_register(_ex_ctx,_register_id, reg);
        InfoL << "Refresh REGISTER sent";
    }
}

void SipClient::eventLoop() {
    DebugL << "Event thread started";

    while (_running) {
        eXosip_event_t *evt = eXosip_event_wait(_ex_ctx, 5, 0);

        if (evt == nullptr) {
            continue;
        }

        eXosip_lock(_ex_ctx);
        eXosip_default_action(_ex_ctx, evt);
        eXosip_unlock(_ex_ctx);

        switch (evt->type) {
            case EXOSIP_REGISTRATION_SUCCESS: {
                InfoL << "注册成功";
                _registered = true;
                if (_callback) {
                    _callback(true, "Registration successful");
                }
                break;
            }

            case EXOSIP_REGISTRATION_FAILURE: {
                std::string reason = "Registration failed";
                if (evt->response) {
                    reason += " (" + std::to_string(evt->response->status_code);
                    if (evt->response->reason_phrase) {
                        reason += " " + std::string(evt->response->reason_phrase);
                    }
                    reason += ")";
                }
                ErrorL << "<<< " << reason << " >>>";
                _registered = false;
                if (_callback) {
                    _callback(false, reason);
                }
                break;
            }

            case EXOSIP_MESSAGE_NEW: {
                if (evt->request) {
                    InfoL << "Received request: "
                            << evt->request->sip_method;
                }
                break;
            }

            default:
                break;
        }

        eXosip_event_free(evt);
    }

    InfoL << "Event thread exited";
}
}
