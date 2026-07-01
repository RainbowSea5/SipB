#include "Gb28181Client.h"

#include <iostream>
#include <cstring>
#include <utility>
#include <sys/socket.h>   // AF_INET
#include <netinet/in.h>   // IPPROTO_UDP
#include <openssl/evp.h>

#include "SipTools/SipTools.h"
using namespace toolkit;
using namespace std;

namespace gb28181 {
Gb28181Client::Gb28181Client(EventPoller::Ptr poller)
    : _ex_ctx(nullptr)
      , _running(false)
      , _server_port(5060)
      , _poller(std::move(poller)) {
}

Gb28181Client::~Gb28181Client() {
    stop();
}

bool Gb28181Client::init(bool is_udp, const std::string &user_agent, int local_port) {
    getCurrentMillisecond();
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

    _status = ClientStatus::INIT;
    InfoL << "eXosip initialized, listening on UDP " << local_port;
    return true;
}

bool Gb28181Client::startRegister(const std::string &server_ip, int server_port, const std::string &device_code,
                                  const std::string &server_id, const std::string &domain, const std::string &auth_user,
                                  const std::string &auth_pass, int expires) {
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
    eXosip_add_authentication_info(_ex_ctx, _user_id.c_str(), _user_id.c_str(),
                                   _password.c_str(), nullptr, nullptr);

    char local_ip[64] = {};
    if (eXosip_guess_localip(_ex_ctx, AF_INET, local_ip, 64) == 0) {
        _local_ip = local_ip;
    }

    eXosip_masquerade_contact(_ex_ctx, server_ip.c_str(), server_port);
    // PrintI("from: %s, to: %s, contact: %s, protocol: %d", ua_core.from, ua_core.to, ua_core.contact,
    // ua_core.sip_protocol_);

    _running = true;
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
        if (eXosip_register_build_register(_ex_ctx, _register_id, 0, &reg) == 0 && reg) {
            eXosip_register_send_register(_ex_ctx, _register_id, reg);
        }
    }

    if (_ex_ctx) {
        eXosip_quit(_ex_ctx);
        _ex_ctx = nullptr;
    }

    // _registered = false;
    InfoL << "stopped";
}

// int ua_add_outboundproxy(osip_message_t *msg, const char *outboundproxy) {
//     int ret = 0;
//     char head[1024] = {0};
//
//     snprintf(head, sizeof(head) - 1, "<%s;lr>", outboundproxy);
//
//     osip_list_special_free(&msg->routes, reinterpret_cast<void (*)(void *)>(osip_route_free));
//     ret = osip_message_set_route(msg, head);
//     return ret;
// }
void Gb28181Client::sendInitialRegister() {
    eXosip_lock(_ex_ctx);
    std::string from = "sip:" + _device_id + "@" + _server_domain;
    std::string proxy = "sip:" + _server_id + "@" + _server_ip + ":" + std::to_string(_server_port);
    _sip_proxy = proxy;
    _sip_from = from;
    _sip_to = "sip:" + _server_id + "@" + _server_domain;

    osip_message_t *reg_message = nullptr;
    int ret = eXosip_register_build_initial_register(_ex_ctx, from.c_str(), proxy.c_str(),
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
    ret = eXosip_register_send_register(_ex_ctx, _register_id, reg_message);
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
    _status = ClientStatus::REGISTERING;
}

void Gb28181Client::sendRefreshRegister() {
    auto l = lockContext();
    osip_message_t *reg = nullptr;
    int ret = eXosip_register_build_register(_ex_ctx, _register_id, _expires, &reg);
    if (ret == 0 && reg) {
        eXosip_register_send_register(_ex_ctx, _register_id, reg);
        InfoL << "Refresh REGISTER sent";
    } else {
        ErrorL << "构建刷新注册消息失败";
    }
}

bool Gb28181Client::sendUnRegisterMessage() {
    if (!_ex_ctx || !_register_id) {
        return false;
    }

    osip_message_t *reg = nullptr;
    int ret = eXosip_register_build_register(_ex_ctx, _register_id, 0, &reg);
    if (ret || !reg) {
        ErrorL << "创建注销请求失败 " << ret;
        return false;
    }
    ret = eXosip_register_send_register(_ex_ctx, _register_id, reg);
    if (ret <= 0) {
        ErrorL << "发送注销请求失败 " << ret;
        return false;
    }
    return true;
}

void Gb28181Client::checkKeepAlive() {
    if (!isRegistered()) {
        return;
    }

    auto now_time = getCurrentMillisecond() / 1000;

    //三次心跳未响应
    if (_last_keep_alive_time && now_time - _last_keep_alive_response_time >= 3 * _keepalive_interval) {
        InfoL << "心跳超时，发送注销请求";
        if (sendUnRegisterMessage()) {
            _status = ClientStatus::UNREGISTERING;
        }
        return;
    } else if (now_time - _last_keep_alive_time < _keepalive_interval) {
        return;
    }

    _last_keep_alive_time = now_time;

    // Build KeepAlive XML body
    auto sn = _sn++;
    pugi::xml_document doc;
    auto root = doc.append_child("Notify");
    root.append_child("CmdType").append_child(pugi::node_pcdata).set_value("Keepalive");
    root.append_child("SN").append_child(pugi::node_pcdata).set_value(std::to_string(sn).c_str());
    root.append_child("DeviceID").append_child(pugi::node_pcdata).set_value(_device_id.c_str());
    root.append_child("Status").append_child(pugi::node_pcdata).set_value("OK");

    std::string body = XmlTools::xmlDocumentToString(doc);

    // Build and send MESSAGE
    auto l = lockContext();
    osip_message_t *msg = nullptr;
    int ret = eXosip_message_build_request(_ex_ctx, &msg, "MESSAGE",
                                           _sip_proxy.c_str(),
                                           _sip_from.c_str(),
                                           nullptr);
    if (ret == 0 && msg) {
        osip_message_set_to(msg, _sip_to.c_str());
        osip_message_set_content_type(msg, "Application/MANSCDP+xml");
        osip_message_set_body(msg, body.c_str(), body.size());
        eXosip_message_send_request(_ex_ctx, msg);
        InfoL << "心跳已发送, SN: " << sn;
    } else {
        ErrorL << "构建心跳 MESSAGE 失败, ret=" << ret;
    }
}

void Gb28181Client::onKeepAliveAnswer(int status_code) {
    if (status_code == 200) {
        InfoL << "心跳响应-成功 " << status_code;
        _last_keep_alive_response_time = getCurrentMillisecond() / 1000;
    } else {
        InfoL << "心跳响应-失败 " << status_code;
    }
}

void Gb28181Client::checkRefreshRegister() {
    if (!isRegistered()) {
        return;
    }

    auto now_time = getCurrentMillisecond() / 1000;
    auto elapsed = now_time - _last_register_time;

    // 过期时间的 2/3 时触发刷新
    if (elapsed < _expires * 2 / 3) {
        return;
    }

    InfoL << "注册即将过期(" << elapsed << "/" << _expires << "s)，发送刷新注册";
    sendRefreshRegister();
    _last_register_time = now_time;
}

void Gb28181Client::onEventMessageNew(eXosip_event_t *event) {
    if (!event || !event->request) {
        return;
    }
    auto request = event->request;

    if (MSG_IS_SUBSCRIBE(request)) {
        DebugL << "处理订阅新请求";
        handleSubscribe(event);
    } else if (MSG_IS_MESSAGE(request)) {
        DebugL << "收到 MESSAGE 请求";
        handleMessage(event);
    } else if (MSG_IS_NOTIFY(request)) {
        DebugL << "收到 NOTIFY 请求";
    }
}

void Gb28181Client::handleSubscribe(eXosip_event_t *event) {
    auto request = event->request;

    auto xml_doc = SipTools::sipMessageGetBodyXmlDocument(request);
    auto xml_root = xml_doc.document_element();
    std::string cmd_type = xml_root.child_value(STR_CMD_TYPE);

    InfoL << "收到订阅, 类型 " << cmd_type;

    int expires = 3600;
    osip_header_t *hdr = nullptr;
    std::string event_header_str;

    osip_message_get_expires(request, 0, &hdr);
    if (hdr) {
        expires = atoi(hdr->hvalue);
    }
    hdr = nullptr;

    osip_message_header_get_byname(request, "event", 0, &hdr);
    if (hdr) {
        event_header_str = hdr->hvalue;
    }
    PrintD("Event ID: %s, Expires: %d, %s", event_header_str.c_str(), expires, cmd_type.c_str());

    //in-dialog 是对已经存在的
    if (event->type == EXOSIP_IN_SUBSCRIPTION_NEW) {
        auto l = lockContext();
        if (expires <= 0) {
            InfoL << "取消订阅: " << event_header_str << " " << cmd_type;
            osip_message_t *answer = nullptr;
            if (eXosip_insubscription_build_answer(_ex_ctx, event->tid, 200, &answer) == 0 && answer) {
                eXosip_insubscription_send_answer(_ex_ctx, event->tid, 200, answer);
                InfoL << "已发送取消订阅 200 OK";
            }
        } else {
            osip_message_t *answer = nullptr;
            if (eXosip_insubscription_build_answer(_ex_ctx, event->tid, 200, &answer) != 0) {
                ErrorL << "构建 200 OK 响应失败";
                return;
            }

            auto s = std::to_string(expires);
            osip_message_set_header(answer, "Expires", s.c_str());
            eXosip_insubscription_send_answer(_ex_ctx, event->tid, 200, answer);
            DebugL << "响应订阅200ok";
        }
    } else if (expires <= 0) {
        ErrorL << "sssssssssssssssssss" << expires;
    }

    //这里通知上层
    if (_on_subscribe_func) {
        _on_subscribe_func(cmd_type, expires);
    }
}

void Gb28181Client::handleMessage(eXosip_event_t *event) {
    auto request = event->request;
    auto xml_doc = SipTools::sipMessageGetBodyXmlDocument(request);
    auto xml_root = xml_doc.document_element();
    auto cmd_type = xml_root.child_value(STR_CMD_TYPE);

    InfoL << "收到Message消息, CmdType " << cmd_type;
    if (osip_strcasecmp(cmd_type, STR_CATA_LOG) == 0) {
        handleCatalogQuery(event, request, xml_root.child_value("SN"));
    } else if (osip_strcasecmp(cmd_type, STR_DEVICE_INFO) == 0) {
        handleDeviceInfoQuery(event, request, xml_root.child_value("SN"));
    } else {
        WarnL << "未实现的类型";
    }
}

void Gb28181Client::handleCatalogQuery(eXosip_event_t *event, osip_message_t *request, const std::string &sn) {
    InfoL << "Catalog query, SN: " << sn;

    // 1. Send 200 OK to acknowledge
    {
        auto l = lockContext();
        osip_message_t *answer = nullptr;
        if (eXosip_message_build_answer(_ex_ctx, event->tid, 200, &answer) == 0 && answer) {
            eXosip_message_send_answer(_ex_ctx, event->tid, 200, answer);
            InfoL << "已响应 Catalog 查询 200 OK";
        }
    }

    // 2. Build and send response MESSAGE with catalog data
    if (_on_catalog_query_cb) {
        auto weak_ptr = weakPtr();
        _on_catalog_query_cb([weak_ptr,sn](vector<DeviceInfo> &vec, bool detail) {
            auto client = weak_ptr.lock();
            if (!client) {
                return;
            }

            pugi::xml_document doc;
            auto root = doc.append_child(STR_XML_ROOT_RESPONSE);
            root.append_child(STR_CMD_TYPE).append_child(pugi::node_pcdata).set_value(STR_CATA_LOG);
            root.append_child("SN").append_child(pugi::node_pcdata).set_value(sn);
            root.append_child("DeviceID").append_child(pugi::node_pcdata).set_value(client->_device_id);
            root.append_child("SumNum").append_child(pugi::node_pcdata).set_value(to_string(vec.size()));

            auto list_node = root.append_child("DeviceList");
            list_node.append_attribute("Num").set_value(to_string(vec.size()));
            for (auto& info:vec) {
                info.appendItemToDocument(list_node);
            }
            auto xml_str = XmlTools::xmlDocumentToString(doc);
            InfoL << "[CataLog]上报设备信息";
            DebugL << xml_str;
            client->sendMessage(xml_str);
        });
    }
        // if (!body.empty()) {
        //     auto l = lockContext();
        //     osip_message_t *msg = nullptr;
        //     int ret = eXosip_message_build_request(_ex_ctx, &msg, "MESSAGE",
        //                                            _sip_to.c_str(),
        //                                            _sip_from.c_str(),
        //                                            nullptr);
        //     if (ret == 0 && msg) {
        //         osip_message_set_to(msg, _sip_to.c_str());
        //         osip_message_set_content_type(msg, "Application/MANSCDP+xml");
        //         osip_message_set_body(msg, body.c_str(), body.size());
        //         eXosip_message_send_request(_ex_ctx, msg);
        //         InfoL << "已发送 Catalog 响应 MESSAGE, SN: " << sn;
        //     } else {
        //         ErrorL << "构建 Catalog 响应 MESSAGE 失败, ret=" << ret;
        //     }
        // }
    // }
}

void Gb28181Client::handleDeviceInfoQuery(eXosip_event_t *event, osip_message_t *request, const std::string &sn) {
    InfoL << "[DeviceInfo] 设备信息查询, SN: " << sn;

    {
        auto l = lockContext();
        osip_message_t *answer = nullptr;
        if (eXosip_message_build_answer(_ex_ctx, event->tid, 200, &answer) == 0 && answer) {
            eXosip_message_send_answer(_ex_ctx, event->tid, 200, answer);
            InfoL << "已响应 DeviceInfo 查询 200 OK";
        }
    }
    if (_on_query_device_info_func) {
        auto weak_ptr = weakPtr();
        _on_query_device_info_func([sn,weak_ptr](DeviceInfo &info) {
            auto client = weak_ptr.lock();
            if (!client) {
                return;
            }
            auto xml_str = info.createDeviceInfoResponse(client->_device_id, sn);
            InfoL << "[DeviceInfo]上报设备信息";
            DebugL << xml_str;
            client->sendMessage(xml_str);
        });
    }
}

onceToken Gb28181Client::lockContext() const {
    return {
        [this]() {
            eXosip_lock(_ex_ctx);
        },
        [this]() {
            eXosip_unlock(_ex_ctx);
        }
    };
}

std::weak_ptr<Gb28181Client> Gb28181Client::weakPtr() {
    return shared_from_this();
}

void Gb28181Client::sendMessage(const std::string &body_str) {
    if (!isRegistered())
        return;
    auto l = lockContext();
    osip_message_t *msg{nullptr};
    auto ret = eXosip_message_build_request(_ex_ctx, &msg, STR_METHOD_MESSAGE,
                                            _sip_proxy.c_str(), _sip_from.c_str(), nullptr);
    if (ret || msg == nullptr) {
        ErrorL << "构建消息失败 " << body_str;
        return;
    }
    osip_message_set_body(msg, body_str.data(), body_str.size());
    ret = eXosip_message_send_request(_ex_ctx, msg);
    if (ret <= 0 || msg == nullptr) {
        ErrorL << "发送消息失败 " << body_str;
    }
}

void Gb28181Client::onMessageAnswered(eXosip_event_t *event) {
    auto request = event->request;
    auto response = event->response;
    if (!request || !response) {
        return;
    }
    auto request_doc = SipTools::sipMessageGetBodyXmlDocument(request);
    auto root_node = request_doc.document_element();
    const auto cmd_type = root_node.child_value(STR_CMD_TYPE);
    if (osip_strcasecmp(cmd_type, STR_KEEP_ALIVE) == 0) {
        onKeepAliveAnswer(response->status_code);
    }
}

void Gb28181Client::eventLoop() {
    DebugL << "Event thread started";
    auto ptr = shared_from_this();
    while (_running) {
        eXosip_event_t *event = eXosip_event_wait(_ex_ctx, 1, 0);

        checkKeepAlive();
        checkRefreshRegister();

        if (event == nullptr) {
            continue;
        }

        {
            auto l = lockContext();
            eXosip_default_action(_ex_ctx, event);
        }

        // SipTools::printEvent(event,_user_id.c_str());
        auto request = event->request;
        auto response = event->response;

        switch (event->type) {
            case EXOSIP_REGISTRATION_SUCCESS: {
                InfoL << "注册成功";
                _status = ClientStatus::REGISTERED;
                _last_register_time = getCurrentMillisecond() / 1000;
                auto cb = _on_register_func;
                if (cb) {
                    cb(true, "");
                }
                checkKeepAlive();
                break;
            }

            case EXOSIP_REGISTRATION_FAILURE: {
                std::string reason = "Registration failed";
                if (response) {
                    if (response->status_code == 401) {
                        //内部会自动处理401 响应
                        DebugL << "收到401";
                        break;
                    }
                    reason += " (" + std::to_string(response->status_code);
                    if (response->reason_phrase) {
                        reason += " " + std::string(response->reason_phrase);
                    }
                    reason += ")";
                }
                ErrorL << "<<< " << reason << " >>>";
                _status = ClientStatus::UNREGISTER;
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
            case EXOSIP_IN_SUBSCRIPTION_NEW: {
                handleSubscribe(event);
                break;
            }
            case EXOSIP_MESSAGE_ANSWERED: {
                onMessageAnswered(event);
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
