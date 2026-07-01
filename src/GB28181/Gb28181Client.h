#ifndef SIP_GB28181_H
#define SIP_GB28181_H

#include <string>
#include <functional>
#include <thread>
#include <atomic>
#include <memory>
#include <utility>
#include <eXosip2/eXosip.h>
#include "Poller/EventPoller.h"
#include "GB28181Def.h"
namespace gb28181 {

class Gb28181Client :public std::enable_shared_from_this<Gb28181Client>{
public:
    explicit Gb28181Client(toolkit::EventPoller::Ptr poller);
    ~Gb28181Client();

    /** 初始化 eXosip 栈并监听本地 UDP 端口 */
    bool init(bool is_udp, const std::string& user_agent,int local_port = 5060);

    bool startRegister(const std::string& server_ip,int server_port,const std::string& device_code,
                       const std::string& server_id,const std::string& domain,const std::string& auth_user,
                       const std::string& auth_pass,int expires = 3600);

    /** 停止注册，发送注销，清理资源 */
    void stop();

    /** 是否已注册成功 */
    bool isRegistered() const { return _status == ClientStatus::REGISTERED; }

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

    std::weak_ptr<Gb28181Client> weakPtr();
private:
    void sendMessage(const std::string& body_str);
    void onMessageAnswered(eXosip_event_t * event);

    void eventLoop();
    void sendInitialRegister();
    void sendRefreshRegister();

    bool sendUnRegisterMessage();

    void checkKeepAlive();
    void onKeepAliveAnswer(int status_code);
    void checkRefreshRegister();

    void onEventMessageNew(eXosip_event_t* event);

    void handleSubscribe(eXosip_event_t* event);
    void handleMessage(eXosip_event_t* event);
    void handleCatalogQuery(eXosip_event_t* event, osip_message_t* request, const std::string& sn);
    void handleDeviceInfoQuery(eXosip_event_t* event, osip_message_t* request, const std::string& sn);
    toolkit::onceToken lockContext() const;

    ClientStatus _status{ClientStatus::UN_INIT};
    // 订阅回调
    SubscribeCallback _on_subscribe_func;
    // 目录查询回调
    CatalogQueryCallback _on_catalog_query_cb;
    // 网络设备信息查询
    OnQueryDeviceInfoCallback _on_query_device_info_func;



private:
    std::function<void()> _on_init_func;
    std::function<void(bool success,const std::string& reason)> _on_register_func;

    toolkit::EventPoller::Ptr _poller;
    eXosip_t* _ex_ctx;
    std::atomic<bool> _running;
    std::thread _event_thread;

    // 注册参数缓存
    std::string _server_ip;
    int _server_port;
    std::string _server_id, _server_domain;
    std::string _user_id,_password,_device_id;
    std::string _local_ip;
    uint16_t _local_port{0};

    int _keepalive_interval{10};
    uint64_t _last_keep_alive_time{0},_last_keep_alive_response_time{0};

    // 序列号
    int _sn{0};

    std::string _sip_from,_sip_to,_sip_proxy;

    int _expires{0},_register_id{0};
    uint64_t _last_register_time{0};
};
}
#endif // SIP_GB28181_H
