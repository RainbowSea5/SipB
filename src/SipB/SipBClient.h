#ifndef SIP_B_H
#define SIP_B_H

#include <string>
#include <functional>
#include <thread>
#include <atomic>
#include <memory>
#include <eXosip2/eXosip.h>
#include "Poller/EventPoller.h"
#include "SipBDef.h"

//电力系统电网B协议
namespace sipB {

class SipBClient :public std::enable_shared_from_this<SipBClient>{
public:
    explicit SipBClient(toolkit::EventPoller::Ptr poller);
    ~SipBClient();

    bool init(bool is_udp, const std::string& user_agent,int local_port = 5060);

    bool startRegister(const std::string& server_ip,int server_port,const std::string& device_code,
                       const std::string& server_id,const std::string& domain,const std::string& auth_user,
                       const std::string& auth_pass,int expires = 3600);

    /** 停止注册，发送注销，清理资源 */
    void stop();

    /** 是否已注册成功 */
    bool isRegistered() const { return _status == ClientStatus::REGISTERED; }

    //更新位置信息
    void setPositionInfo(MobilePositionInfo info);

    /** 订阅回调: event_type, expires */
    using SubscribeCallback = std::function<void(const std::string& event_type, int expires)>;
    void setOnSubscribe(SubscribeCallback cb) {
        _on_subscribe_func = std::move(cb);
    }

    void setOnInit(std::function<void()> on_init_func) {
        _on_init_func = std::move(on_init_func);
    }

    void setOnRegister(std::function<void(bool success, const std::string &reason)> on_register_func) {
        _on_register_func = std::move(on_register_func);
    }

    using CatalogQueryCallback = std::function<void(std::function<void(std::vector<DeviceInfo>&,bool)>)>;
    void setOnCatalogQuery(CatalogQueryCallback cb) {
        _on_catalog_query_cb = std::move(cb);
    }

    using OnQueryDeviceInfoCallback = std::function<void(std::function<void(DeviceInfo& info)>)>;
    void setOnQueryDeviceInfo(OnQueryDeviceInfoCallback on_device_info_func) {
        _on_query_device_info_func = std::move(on_device_info_func);
    }

    void openDebuggerLog() { _print_message = true;}

    std::weak_ptr<SipBClient> weakPtr();
private:
    void sendMessage(const std::string& body_str,const std::string& method = STR_METHOD_MESSAGE);
    void onMessageAnswered(eXosip_event_t * event);

    void eventLoop();
    void sendInitialRegister();
    void sendRefreshRegister();

    bool sendUnRegisterMessage();

    void checkKeepAlive();
    void checkRefreshRegister();
    void checkSubscribe();
    void checkPositionSubscribe(SubscribeInfo& info);
    void sendResourceReport();

    void onEventMessageNew(eXosip_event_t* event);

    void handleSubscribe(eXosip_event_t* event);
    void handleMessage(eXosip_event_t* event);
    void handleCatalogQuery(eXosip_event_t* event, osip_message_t* request, const std::string& sn);
    void handleDeviceInfoQuery(eXosip_event_t* event, osip_message_t* request, const std::string& sn);

    void onKeepAliveAnswer(int status_code);
    toolkit::onceToken lockContext() const;
    std::unique_lock<std::mutex> lockThis();


    ClientStatus _status{ClientStatus::UN_INIT};

#pragma region 内部回调
    //初始化回调
    std::function<void()> _on_init_func;
    //注册结果回调
    std::function<void(bool success,const std::string& reason)> _on_register_func;
    // 订阅回调
    SubscribeCallback _on_subscribe_func;
    // 目录查询回调
    CatalogQueryCallback _on_catalog_query_cb;
    // 网络设备信息查询
    OnQueryDeviceInfoCallback _on_query_device_info_func;
#pragma endregion

    std::mutex _mtx;
    MobilePositionInfo _position_info;
    std::list<SubscribeInfo> _subscribe_list;

    toolkit::EventPoller::Ptr _poller;
    eXosip_t* _ex_ctx;
    std::atomic<bool> _running;
    std::thread _event_thread;

#pragma region 注册信息/参数
    std::string _server_ip;
    int _server_port;
    std::string _server_id, _server_domain;
    std::string _user_id,_password,_device_id;
    std::string _local_ip;
    uint16_t _local_port{0};

    //注册
    int _expires{0},_register_id{0};
    uint64_t _last_register_time{0};
#pragma endregion

    //心跳
    int _keepalive_interval{10};
    uint64_t _last_keep_alive_time{0},_last_keep_alive_response_time{0};

    // 发送消息序列号
    uint16_t _sn{1};
    std::string _sip_from,_sip_to,_sip_proxy;

    bool _print_message{false};
    bool _resource_reported{false};
};
}
#endif // SIP_B_H
