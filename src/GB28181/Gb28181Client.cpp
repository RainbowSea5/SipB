#include "Gb28181Client.h"

#include <iostream>
#include <cstring>
#include <cstdlib>
#include <utility>
#include <sys/socket.h>   // AF_INET
#include <netinet/in.h>   // IPPROTO_UDP
#include <openssl/evp.h>

#include "SipTools/SipTools.h"
using namespace toolkit;

namespace gb28181 {
Gb28181Client::Gb28181Client(EventPoller::Ptr poller)
    : _ex_ctx(nullptr)
      , _running(false)
      , _registered(false)
      , _server_port(5060)
      , _expires(3600)
      , _poller(std::move(poller)) {
}

Gb28181Client::~Gb28181Client() {
    stop();
}

bool Gb28181Client::init(bool is_udp, const std::string& user_agent,int local_port){
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

    if (!user_agent.empty()) {
        eXosip_set_user_agent(_ex_ctx, user_agent.c_str());
    }

    InfoL << "eXosip initialized, listening on UDP " << local_port;
    return true;
}

bool Gb28181Client::startRegister(const std::string &server_ip, int server_port,const std::string &device_code,
    const std::string& server_id,const std::string &domain,const std::string &auth_user,
    const std::string &auth_pass,int expires) {

    if (_ex_ctx == nullptr) {
        ErrorL << "eXosip not initialized";
        return false;
    }

    _server_ip = server_ip;
    _server_port = server_port;
    _device_id = device_code;
    _server_domain = domain;
    _user_id = auth_user;
    _password = auth_pass;
    _expires = expires;
    _server_id = server_id;

    // 设置 Digest 认证信息 —— eXosip 在收到 401/407 后自动使用
    // 这些信息计算 Authorization 头并重发 REGISTER
    eXosip_clear_authentication_info(_ex_ctx);
    eXosip_add_authentication_info(_ex_ctx,_user_id.c_str(),_user_id.c_str(),
        _password.c_str(),nullptr,nullptr);

    char local_ip[64] = {};
    if (eXosip_guess_localip(_ex_ctx, AF_INET, local_ip, 64) == 0) {
        _local_ip = local_ip;
    }

    eXosip_masquerade_contact(_ex_ctx, server_ip.c_str(), server_port);
    // PrintI("from: %s, to: %s, contact: %s, protocol: %d", ua_core.from, ua_core.to, ua_core.contact,
         // ua_core.sip_protocol_);

    _running = true;
    _registered = false;
    sendInitialRegister();
    _event_thread = std::thread(&Gb28181Client::eventLoop, this);


    return true;
}

void Gb28181Client::stop() {
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
int ua_add_outboundproxy(osip_message_t *msg, const char *outboundproxy) {
    int ret = 0;
    char head[1024] = {0};

    snprintf(head, sizeof(head) - 1, "<%s;lr>", outboundproxy);

    osip_list_special_free(&msg->routes, reinterpret_cast<void (*)(void *)>(osip_route_free));
    ret = osip_message_set_route(msg, head);
    return ret;
}
void Gb28181Client::sendInitialRegister() {
    eXosip_lock(_ex_ctx);
    std::string from = "sip:" + _device_id + "@" + _server_domain;
    // Use _server_ip (not _server_domain) as the transport target,
    // so eXosip sends the REGISTER packet to the correct SIP server address.
    std::string proxy = "sip:" + _server_id  + "@" + _server_ip + ":" + std::to_string(_server_port);
    osip_message_t *reg_message = nullptr;
    int ret = eXosip_register_build_initial_register(_ex_ctx, from.c_str(),proxy.c_str(),
        nullptr, _expires, &reg_message);

    if (ret < 0 || reg_message == nullptr) {
        ErrorL << "Failed to build initial REGISTER (ret=" << ret << ")";
        auto cb = _on_register_func;
        if (cb) {
            cb(false, "eXosip_register_build_initial_register failed");
        }
        return;
    }
    _register_id = ret;

    // ua_add_outboundproxy(reg_message,outboundproxy.c_str());
    auto uri = reg_message->req_uri;
    DebugL << uri->host << uri->password << uri->port << uri->scheme << uri->username;

    // DebugL << "发送注册消息 \n" << SipTools::sipMessageToString(reg_message);
    ret = eXosip_register_send_register(_ex_ctx, _register_id,reg_message);
    if (ret != 0) {
        ErrorL << "Failed to send REGISTER (ret=" << ret << ")";
        auto cb = _on_register_func;
        if (cb) {
            cb(false, "eXosip_register_send_register failed");
        }
    } else {
        InfoL << "Initial REGISTER sent (expires=" << _expires << "s)";
    }
    eXosip_unlock(_ex_ctx);

}

void Gb28181Client::sendRefreshRegister() {
    osip_message_t *reg = nullptr;
    int ret = eXosip_register_build_register(_ex_ctx, _register_id, _expires, &reg);
    if (ret == 0 && reg) {
        eXosip_register_send_register(_ex_ctx,_register_id, reg);
        InfoL << "Refresh REGISTER sent";
    }
}

void Gb28181Client::onEventMessageNew(eXosip_event_t *event) {
    if (!event) {
        return;
    }
    auto request = event->request;
    auto response = event->response;
    if (MSG_IS_SUBSCRIBE(request)) {

    }
}

void Gb28181Client::eventLoop() {
    DebugL << "Event thread started";

    while (_running) {
        eXosip_event_t *event = eXosip_event_wait(_ex_ctx, 5, 0);

        if (event == nullptr) {
            continue;
        }

        eXosip_lock(_ex_ctx);
        eXosip_default_action(_ex_ctx, event);
        eXosip_unlock(_ex_ctx);

        SipTools::printEvent(event,_user_id.c_str());
        auto request = event->request;
        auto response = event->response;

        switch (event->type) {
            case EXOSIP_REGISTRATION_SUCCESS: {
                InfoL << "注册成功";
                _registered = true;
                auto cb = _on_register_func;
                if (cb) {
                    cb(true, "");
                }
                break;
            }

            case EXOSIP_REGISTRATION_FAILURE: {
                std::string reason = "Registration failed";
                if (response) {
                    if (response->status_code == 401) {
                        DebugL << "401 内部会自动处理，直接返回";
                        return;
                    }
                    reason += " (" + std::to_string(response->status_code);
                    if (response->reason_phrase) {
                        reason += " " + std::string(response->reason_phrase);
                    }
                    reason += ")";
                }
                ErrorL << "<<< " << reason << " >>>";
                _registered = false;
                auto cb = _on_register_func;
                if (cb) {
                    cb(false, reason);
                }
                break;
            }

            case EXOSIP_MESSAGE_NEW: {
                onEventMessageNew(event);
                break;
            }

            default:
                break;
        }

        eXosip_event_free(event);
    }

    InfoL << "Event thread exited";
}
}
