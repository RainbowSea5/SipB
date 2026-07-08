#include "SipBClient.h"

#include <iostream>
#include <cstring>
#include <cstdlib>
#include <utility>
#include <sys/socket.h>   // AF_INET
#include <netinet/in.h>   // IPPROTO_UDP

#include "SipTools/SipTools.h"
#include "XmlTools/XmlTools.h"
using namespace toolkit;
using namespace std;
using namespace pugi;

namespace sipB {
SipBClient::SipBClient(EventPoller::Ptr poller)
    : _ex_ctx(nullptr)
      , _running(false)
      , _server_port(5060)
      , _poller(std::move(poller)) {
}

SipBClient::~SipBClient() {
    stop();
}

bool SipBClient::init(bool is_udp, const std::string &user_agent, int local_port) {
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

bool SipBClient::startRegister(const std::string &server_ip, int server_port, const std::string &device_code,
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
    _event_thread = std::thread(&SipBClient::eventLoop, this);
    return true;
}

void SipBClient::stop() {
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

void SipBClient::setOnSubscribe(SubscribeCallback cb) {
    checkNotRegister();
    _on_subscribe_func = std::move(cb);
}

void SipBClient::setOnInit(std::function<void()> on_init_func) {
    checkNotRegister();
    _on_init_func = std::move(on_init_func);
}

void SipBClient::setOnRegister(std::function<void(bool success, const std::string &reason)> on_register_func) {
    checkNotRegister();
    _on_register_func = std::move(on_register_func);
}

void SipBClient::setOnQueryAllResource(OnQueryAllResource cb) {
    checkNotRegister();
    _on_query_all_resource = std::move(cb);
}

void SipBClient::setOnQueryResourceInfo(OnQueryResourceInfoCallback cb) {
    checkNotRegister();
    _on_query_resource_info_cb = std::move(cb);
}

void SipBClient::setOnQueryHistoryAlarm(OnQueryHistoryAlarmCallback cb) {
    checkNotRegister();
    _on_query_history_alarm_cb = std::move(cb);
}

void SipBClient::setOnQueryHistoryVideo(OnQueryHistoryVideoCallback cb) {
    checkNotRegister();
    _on_query_history_video_cb = std::move(cb);
}

void SipBClient::setOnStartLiveVideo(StartLiveVideoCallback cb) {
    checkNotRegister();
    _on_start_real_play = std::move(cb);
}

void SipBClient::setOnStopCall(StopCallCallback cb) {
    checkNotRegister();
    _on_stop_call = std::move(cb);
}

void SipBClient::setOnStartLiveAudio(StartLiveAudioCallback cb) {
    checkNotRegister();
    _on_start_real_talk = std::move(cb);
}

void SipBClient::setOnAddPcmPlayer(PcmPlayerCallback cb) {
    checkNotRegister();
    _on_add_pcm_player = std::move(cb);
}


void SipBClient::sendInitialRegister() {
    std::string from = "sip:" + _device_id + "@" + _server_domain;
    std::string proxy = "sip:" + _server_id + "@" + _server_ip + ":" + std::to_string(_server_port);
    _sip_proxy = proxy;
    _sip_from = from;
    _sip_to = _sip_proxy;


    auto l = lockContext();
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
    _status = ClientStatus::REGISTERING;
}

void SipBClient::sendRefreshRegister() {
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

bool SipBClient::sendUnRegisterMessage() {
    if (!_ex_ctx || !_register_id) {
        return false;
    }

    auto l = lockContext();
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

void SipBClient::checkRefreshRegister() {
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

void SipBClient::checkSubscribe() {
    for (auto it = _subscribe_list.begin(); it != _subscribe_list.end();) {
        auto &info = *it;
        if (info.overdue()) {
            InfoL << "订阅到期，清理订阅 " << it->cmd_type;
            it = _subscribe_list.erase(it);
            continue;
        }

        ++it;
    }
}

void SipBClient::sendResourceReport() {
    if (!_on_query_all_resource) {
        WarnL << "资源上报：未设置设备目录回调";
        return;
    }

    auto weak_ptr = weak_from_this();
    _on_query_all_resource([weak_ptr](const vector<DeviceInfo> &vec) {
        auto client = weak_ptr.lock();
        if (!client || !client->isRegistered()) {
            return;
        }

        size_t total = vec.size();

        //这里默认保证 设备只有一个，不再分段分多条消息发送
        pugi::xml_document doc;
        auto root = doc.append_child("SIP_XML");
        root.append_attribute("EventType").set_value("Push_Resource");
        root.append_child("Code").append_child(pugi::node_pcdata).set_value(client->_device_id);

        auto list_node = root.append_child("SubList");
        for (auto &info: vec) {
            info.appendItemToDocument(list_node);
        }
        list_node.append_attribute("SubNum").set_value(total);

        auto xml_str = XmlTools::xmlDocumentToString(doc);
        InfoL << "资源上报 " << total << " 个设备, body=" << xml_str.size() << " bytes";
        client->sendMessage(xml_str, STR_METHOD_NOTIFY);
        InfoL << "资源上报完成，共 " << total << " 个设备";
    });
}

void SipBClient::onEventMessageNew(eXosip_event_t *event) {
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

void SipBClient::handleSubscribe(eXosip_event_t *event) {
    auto request = event->request;

    auto xml_doc = SipTools::sipMessageGetBodyXmlDocument(request);
    auto xml_root = xml_doc.document_element();
    std::string cmd_type = xml_root.child_value(STR_CMD_TYPE);
    std::string sn_str = xml_root.child_value("SN");
    auto interval_str = xml_root.child_value("Interval");

    auto interval = atoi(interval_str);

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

    //多加三秒，防止服务器续订慢
    expires += 3;

    bool find = false;
    for (auto &subscribe_info: _subscribe_list) {
        if (subscribe_info.cmd_type == cmd_type) {
            subscribe_info.update(sn_str, expires, interval);
            find = true;
            break;
        }
    }
    if (!find) {
        SubscribeInfo info(cmd_type, expires, sn_str, interval);
        _subscribe_list.push_back(info);
    }

    //这里通知上层
    if (_on_subscribe_func) {
        _on_subscribe_func(cmd_type, expires);
    }
}

void SipBClient::handleMessage(eXosip_event_t *event) {
    auto request = event->request;
    auto xml_doc = SipTools::sipMessageGetBodyXmlDocument(request);
    auto xml_root = xml_doc.document_element();

    if (!xml_doc || !xml_root || strcmp(xml_root.name(), "SIP_XML") != 0) {
        return;
    }

    auto event_type = xml_root.attribute("EventType").value();
    if (strcmp(event_type, STR_EVENT_TYPE_REQUEST_RESOURCE) == 0) {
        handleResourceRequest(event, xml_root);
    } else if (strcmp(event_type, STR_EVENT_TYPE_REQUEST_HISTORY_ALARM) == 0) {
        handleHistoryAlarmRequest(event, xml_root);
    } else if (strcmp(event_type, STR_EVENT_TYPE_REQUEST_HISTORY_VIDEO) == 0) {
        handleHistoryVideoRequest(event, xml_root);
    } else if (strcmp(event_type, STR_EVENT_TYPE_REQUEST_PTZ_CONTROL) == 0) {
        handlePtzControlRequest(event, xml_root);
    } else {
        WarnL << "未实现的 EventType: " << event_type;
    }
}

void SipBClient::handleResourceRequest(eXosip_event_t *event, const xml_node &xml_root) {
    auto item_node = xml_root.child("Item");
    std::string code = item_node.attribute("Code").value();
    int from_index = atoi(item_node.attribute("FromIndex").value());
    int to_index = atoi(item_node.attribute("ToIndex").value());
    auto tid = event->tid;

    InfoL << "[ResourceRequest] Code: " << code << ", From: " << from_index << ", To: " << to_index;

    if (_on_query_resource_info_cb) {
        _wait_answer_event[event->tid] = event;

        auto weak_ptr = weak_from_this();

        auto cb = [weak_ptr, code, from_index, to_index,tid](const vector<DeviceInfo> &items, int real_num) {
            auto client = weak_ptr.lock();
            if (!client) return;
            PrintD("查询资源设备共%d个", items.size());
            auto xml_str = DeviceInfo::makeQueryResourceResponse(items,code,from_index,to_index,real_num);
            client->async([tid,weak_ptr,xml_str] {
                if (auto cli = weak_ptr.lock()) {
                    cli->sendMessageResponse(tid, xml_str, "资源信息");
                }
            });
        };

        _on_query_resource_info_cb(code, from_index, to_index, cb);
    }else {
        //没有回调时 发送空内容
        WarnL << "未设置回调，查询资源设备 返回空";
        auto xml_str = DeviceInfo::makeQueryResourceResponse({},code,from_index,to_index,0);
        sendMessageResponse(event, "", "资源信息");
    }
}

void SipBClient::handleHistoryAlarmRequest(eXosip_event_t *event, const xml_node &xml_root) {
    auto item_node = xml_root.child("Item");
    std::string code = item_node.attribute("Code").value();
    std::string user_code = item_node.attribute("UserCode").value();
    std::string type = item_node.attribute("Type").value();
    std::string begin_time = item_node.attribute("BeginTime").value();
    std::string end_time = item_node.attribute("EndTime").value();
    std::string level = item_node.attribute("Level").value();
    int from_index = atoi(item_node.attribute("FromIndex").value());
    int to_index = atoi(item_node.attribute("ToIndex").value());
    auto tid = event->tid;

    InfoL << "[HistoryAlarmRequest] Code: " << code
          << ", UserCode: " << user_code
          << ", Type: " << type
          << ", BeginTime: " << begin_time
          << ", EndTime: " << end_time
          << ", Level: " << level
          << ", From: " << from_index << ", To: " << to_index;

    if (_on_query_history_alarm_cb) {
        _wait_answer_event[event->tid] = event;

        auto weak_ptr = weak_from_this();

        auto cb = [weak_ptr, from_index, to_index, tid](const std::vector<AlarmInfo> &items, int real_num) {
            auto client = weak_ptr.lock();
            if (!client) return;
            PrintD("查询历史告警共%d条", items.size());
            auto xml_str = AlarmInfo::makeQueryHistoryAlarmResponse(items, from_index, to_index, real_num);
            client->async([tid, weak_ptr, xml_str] {
                if (auto cli = weak_ptr.lock()) {
                    cli->sendMessageResponse(tid, xml_str, "历史告警");
                }
            });
        };

        _on_query_history_alarm_cb(code, user_code, type, begin_time, end_time, level, from_index, to_index, cb);
    } else {
        WarnL << "未设置回调，查询历史告警 返回空";
        auto xml_str = AlarmInfo::makeQueryHistoryAlarmResponse({}, from_index, to_index, 0);
        sendMessageResponse(event, xml_str, "历史告警");
    }
}

void SipBClient::handleHistoryVideoRequest(eXosip_event_t *event, const xml_node &xml_root) {
    auto item_node = xml_root.child("Item");
    std::string code = item_node.attribute("Code").value();
    int32_t type = atoi(item_node.attribute("Type").value());
    std::string user_code = item_node.attribute("UserCode").value();
    std::string begin_time = item_node.attribute("BeginTime").value();
    std::string end_time = item_node.attribute("EndTime").value();
    int from_index = atoi(item_node.attribute("FromIndex").value());
    int to_index = atoi(item_node.attribute("ToIndex").value());
    auto tid = event->tid;

    InfoL << "[HistoryVideoRequest] Code: " << code
          << ", Type: " << type
          << ", UserCode: " << user_code
          << ", BeginTime: " << begin_time
          << ", EndTime: " << end_time
          << ", From: " << from_index << ", To: " << to_index;

    if (_on_query_history_video_cb) {
        _wait_answer_event[event->tid] = event;

        auto weak_ptr = weak_from_this();

        auto cb = [weak_ptr, from_index, to_index, tid](const std::vector<RecordInfo> &items, int real_num) {
            auto client = weak_ptr.lock();
            if (!client) return;
            PrintD("查询录像共%d条", items.size());
            auto xml_str = RecordInfo::makeQueryHistoryVideoResponse(items, from_index, to_index, real_num);
            client->async([tid, weak_ptr, xml_str] {
                if (auto cli = weak_ptr.lock()) {
                    cli->sendMessageResponse(tid, xml_str, "录像检索");
                }
            });
        };

        _on_query_history_video_cb(code, type, user_code, begin_time, end_time, from_index, to_index, cb);
    } else {
        WarnL << "未设置回调，查询录像 返回空";
        auto xml_str = RecordInfo::makeQueryHistoryVideoResponse({}, from_index, to_index, 0);
        sendMessageResponse(event, xml_str, "录像检索");
    }
}

void SipBClient::handlePtzControlRequest(eXosip_event_t *event, const pugi::xml_node &xml_root) {
    auto item_node = xml_root.child("Item");
    std::string code = item_node.attribute("Code").value();
    int32_t command = atoi(item_node.attribute("Command").value());
    int32_t command_para1 = atoi(item_node.attribute("CommandPara1").value());
    int32_t command_para2 = atoi(item_node.attribute("CommandPara2").value());
    int32_t command_para3 = atoi(item_node.attribute("CommandPara3").value());

    InfoL << "[PtzControl] Code: " << code
          << ", Command: " << command
          << ", Para1: " << command_para1
          << ", Para2: " << command_para2
          << ", Para3: " << command_para3;

    if (_on_ptz_control_cb) {
        _wait_answer_event[event->tid] = event;

        auto weak_ptr = weak_from_this();
        auto tid = event->tid;

        auto success_cb = [weak_ptr, tid, code] {
            auto client = weak_ptr.lock();
            if (!client) return;
            InfoL << "[PtzControl] 执行成功 Code: " << code;
            client->async([tid, weak_ptr] {
                if (auto cli = weak_ptr.lock()) {
                    cli->sendMessageResponse(tid, "", "云镜控制");
                }
            });
        };

        _on_ptz_control_cb(code, command, command_para1, command_para2, command_para3, success_cb);
    } else {
        WarnL << "未设置回调，云镜控制 直接返回成功";
        sendMessageResponse(event, "", "云镜控制");
    }
}

void SipBClient::sendMessageResponse(int tid, const std::string &body_str, const std::string &log_tag) {
    auto *event = _wait_answer_event[tid];
    _wait_answer_event.erase(tid);
    if (event) {
        sendMessageResponse(event, body_str, log_tag);
        eXosip_event_free(event);
    }
}

void SipBClient::sendMessageResponse(const eXosip_event_t *event, const std::string &body_str, const std::string &log_tag) const {
    auto l = lockContext();
    osip_message_t *answer;
    auto ret = eXosip_message_build_answer(_ex_ctx, event->tid, 200, &answer);
    DebugL << "响应" << log_tag << "-构建消息 " << ret;
    if (ret == 0) {
        osip_message_set_body(answer, body_str.c_str(), body_str.size());
        ret = eXosip_message_send_answer(_ex_ctx, event->tid, 200, answer);
        DebugL << "响应" << log_tag << "-发送消息 " << ret;
        if (ret == 0) {
            InfoL << "响应" << log_tag << "-成功";
        }
    }
    if (ret) {
        ErrorL << "响应" << log_tag << "-失败 " << ret;
    }
}

void SipBClient::processEvent(eXosip_event_t *event) {
    if (!event) {
        return;
    }

    //让exosip 处理一些常规操作，401回复，首次订阅回复200 等
    {
        auto l = lockContext();
        eXosip_default_action(_ex_ctx, event);
    }

    if (_print_message) {
        SipTools::printEvent(event, _user_id.c_str());
    }
    auto request = event->request;
    auto response = event->response;

    switch (event->type) {
        case EXOSIP_REGISTRATION_SUCCESS: {
            InfoL << "注册成功";
            _status = ClientStatus::REGISTERED;
            _last_register_time = getCurrentMillisecond() / 1000;
            if (auto cb = _on_register_func) {
                cb(true, "");
            }
            if (!_resource_reported) {
                sendResourceReport();
                _resource_reported = true;
            }
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
            _resource_reported = false;
            if (auto cb = _on_register_func) {
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
        case EXOSIP_CALL_INVITE: {
            onEventInvite(event);
            break;
        }
        case EXOSIP_CALL_ACK: {
            onEventInviteAck(event);
            break;
        }
        case EXOSIP_CALL_CANCELLED: {
            closeCall({Err_success,"对端取消"},event->cid,event->did);
            break;
        }
        case EXOSIP_CALL_CLOSED: {
            closeCall({Err_success,"对端发送BYE"},event->cid,event->did);
            break;
        }

        default:
            break;
    }
    //有些事件需要延时释放
    if (_wait_answer_event.find(event->tid) == _wait_answer_event.end()) {
        eXosip_event_free(event);
    }
}

void SipBClient::async(const std::function<void()>& func) {
    if (_ex_ctx && _running){
        auto l = lockThis();
        _run_list.emplace_back(func);
        eXosip_wakeup_event(_ex_ctx);
    }
}


onceToken SipBClient::lockContext() const {
    return {
        [this]() {
            eXosip_lock(_ex_ctx);
        },
        [this]() {
            eXosip_unlock(_ex_ctx);
        }
    };
}

std::unique_lock<std::mutex> SipBClient::lockThis() {
    return std::unique_lock(_mtx);
}

void SipBClient::checkNotRegister() const {
    if (isRegistered()) {
        throw std::runtime_error("不允许执行，已注册！");
    }
}

void SipBClient::sendMessage(const std::string &body_str, const std::string &method) {
    auto l = lockContext();
    if (!isRegistered())
        return;
    osip_message_t *msg{nullptr};
    auto ret = eXosip_message_build_request(_ex_ctx, &msg, method.c_str(),
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

void SipBClient::onMessageAnswered(eXosip_event_t *event) {
    auto request = event->request;
    auto response = event->response;
    if (!request || !response) {
        return;
    }
    auto request_doc = SipTools::sipMessageGetBodyXmlDocument(request);
    auto root_node = request_doc.document_element();
}

void SipBClient::sendCallResponse(eXosip_event_t* event, int status_code, const std::string& reason) {
    auto l = lockContext();
    osip_message_t* answer = nullptr;
    int ret = eXosip_call_build_answer(_ex_ctx, event->tid, status_code, &answer);
    if (ret == 0 && answer) {
        if (!reason.empty()) {
            osip_message_set_header(answer, "Warning", reason.c_str());
        }
        eXosip_call_send_answer(_ex_ctx, event->tid, status_code, answer);
    }
}

void SipBClient::onEventInviteAck(eXosip_event_t *event) {
    int cid = event->cid;
    int did = event->did;
    auto it = _active_sessions.find(cid);
    if (it == _active_sessions.end() || !it->second) {
        return sendCallTerminate(event);
    }
    auto session = it->second;
    auto weak_ptr = weak_from_this();

    auto on_error = [weak_ptr,cid,did](const SockException& ex) {
        if (ex.getErrCode() == Err_success) {
            return;
        }
        if (auto client = weak_ptr.lock()) {
            client->async([weak_ptr,cid,did,ex]() {
                if (auto cli = weak_ptr.lock()) {
                    cli->closeCall(ex,cid,did);
                }
            });
        }
    };

    session->onStart(on_error);
    onStartStream(session);
}

void SipBClient::onStartStream(RtpSession::Ptr &session) {
    auto weak_session = session->weak_from_this();
    // 根据会话类型通知上层开始生产数据
    std::string session_type = session->sessionName();
    MediaCodec codec = session->trackInfo().codec;
    auto cid = session->getCid();
    auto weak_client = weak_from_this();

    if (session_type == "Play") {
        if (_on_start_real_play) {
            auto on_video_data = [weak_session, codec](const uint8_t* data, size_t len, uint64_t pts_ms) {
                if (auto ss = weak_session.lock()) {
                    if (codec == MediaCodec::H264) {
                        ss->inputH264(data, len, pts_ms);
                        return true;
                    }else if (codec == MediaCodec::H265) {
                        ss->inputH265(data, len, pts_ms);
                        return true;
                    }else {
                        WarnL << "没有合适的编码" << (uint8_t)codec;
                    }
                }
                return false;
            };
            _on_start_real_play(cid, codec, on_video_data);
        }else {
            PrintE("[%s] [%d]无法开始获取实时视频，无回调",session_type.c_str(),cid);
        }
    }else if (session_type == "Talk") {
        if (_on_start_real_talk) {
            auto on_audio_data = [weak_session, cid](const uint8_t* data, size_t len, uint64_t pts_ms) {
                if (auto ss = weak_session.lock()) {
                    ss->inputPCMA(data, len, pts_ms);
                    return true;
                }
                return false;
            };
            _on_start_real_talk(cid, on_audio_data);
        }
        // session->setOnPcmData([weak_client](const uint8_t* data, size_t len) {
        //
        // });
        if (_on_add_pcm_player) {
            auto player = _on_add_pcm_player(cid);
            if (player) {
                session->setOnPcmData(player);
            }
        }
    }else if (session_type == "Broadcast") {
        if (_on_add_pcm_player) {
            auto player = _on_add_pcm_player(cid);
            if (player) {
                session->setOnPcmData(player);
            }
        }
    }else if (session_type == "Playback") {

    }
}

void SipBClient::onStopStream(RtpSession::Ptr &session, int cid) const {
    if (_on_stop_call) {
        _on_stop_call(cid);
    }
}

void SipBClient::sendCallTerminate(const eXosip_event_t *event) const {
    sendCallTerminate(event->cid,event->did);
}

void SipBClient::sendCallTerminate(int cid, int did) const {
    auto l = lockContext();
    if (int ret = eXosip_call_terminate(_ex_ctx, cid, did)) {
        ErrorL << "发送BYE失败 " << ret;
    }else {
        InfoL << "发送BYE " << cid;
    }
}

void SipBClient::closeCall(const SockException &ex, int cid, int did) {
    if (ex.getErrCode() != Err_success) {
        sendCallTerminate(cid,did);
    }
    auto it = _active_sessions.find(cid);
    if (it == _active_sessions.end() || !it->second) {
        PrintW("通话[%d][%d]结束: %s",cid,did,ex.what());
    }else {
        PrintW("通话[%s][%d][%d]结束: %s",it->second->sessionName().c_str(), cid,did,ex.what());
        //关闭数据采集 编码 输入
        onStopStream(it->second,cid);
        //这里触发析构 来关闭session
        _active_sessions.erase(cid);
    }
}

void SipBClient::onEventInvite(eXosip_event_t *event) {
    if (!event || !event->request) {
        return;
    }
    auto request = event->request;

    osip_body_t* body = nullptr;
    osip_message_get_body(request, 0, &body);
    if (!body || !body->body) {
        sendCallResponse(event, 488, "No SDP");
        return;
    }

    {
        auto l = lockContext();
        eXosip_call_send_answer(_ex_ctx,event->tid,100,nullptr);
    }

    std::string sdp(body->body);

    // 创建 RTP 会话（内部解析 SDP）
    auto session = RtpSession::create(sdp);
    if (!session) {
        ErrorL << "创建 RtpSession 失败";
        sendCallResponse(event, 500, "Failed to create session");
        return;
    }

    // 从会话获取 session name (s= 行)
    std::string session_name = session->sessionName();
    if (session_name.empty()) {
        sendCallResponse(event, 400, "Bad Request - no s= line");
        return;
    }

    // 相同类型会话只能存在一个
    for (auto&[cid, s] : _active_sessions) {
        if (s && s->sessionName() == session_name) {
            WarnL << "会话类型 " << session_name << " 已存在，返回 503";
            sendCallResponse(event, 503, "Session type already exists");
            return;
        }
    }

    int port = -1;
    {
        auto l = lockContext();
        port = eXosip_find_free_port(_ex_ctx,_rtp_port,session->getIPProto());
    }
    if (port<=0) {
        ErrorL << "查找可用端口失败 " << port;
        sendCallResponse(event, 500, "Failed to find free port");
        return;
    }
    //更新开始查找的端口
    _rtp_port = port;

    std::string answer_sdp = session->makeAnswerSdp(_local_ip, port);
    _active_sessions[event->cid] = session;
    session->setCid(event->cid);

    InfoL << "创建会话成功, 类型=" << session_name << ", RTP 端口=" << port;

    {
        auto l = lockContext();
        osip_message_t* answer = nullptr;
        int ret = eXosip_call_build_answer(_ex_ctx, event->tid, 200, &answer);
        if (ret == 0 && answer) {
            osip_message_set_body(answer, answer_sdp.c_str(), answer_sdp.size());
            osip_message_set_content_type(answer, "application/sdp");
            eXosip_call_send_answer(_ex_ctx, event->tid, 200, answer);
            DebugL << "INVITE 200 OK 已发送";
        } else {
            ErrorL << "构建 INVITE 200 OK 失败 ret=" << ret;
        }
    }
}

void SipBClient::eventLoop() {
    DebugL << "Event thread started";
    auto ptr = shared_from_this();
    while (_running) {
        eXosip_event_t *event = eXosip_event_wait(_ex_ctx, 1, 0);
        processEvent(event);

        if (isRegistered()) {
            checkRefreshRegister();
            checkSubscribe();
        }

        //异步执行逻辑
        {
            std::list<std::function<void()>> list;
            //加锁取，无锁状态执行
            {
                auto l = lockThis();
                list = _run_list;
            }
            for (auto &function: list) {
                if (function) {
                    function();
                }
            }
        }
    }

    InfoL << "Event thread exited";
}
}
