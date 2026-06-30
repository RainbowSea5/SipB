#ifndef SIP_GB28181_H
#define SIP_GB28181_H

#include <string>
#include <functional>
#include <thread>
#include <atomic>
#include <memory>
#include <eXosip2/eXosip.h>
#include "Poller/EventPoller.h"

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
    bool isRegistered() const { return _registered; }

    void setOnInit(std::function<void()> on_init_func) {
        _on_init_func = std::move(on_init_func);
    }

    void setOnRegister(std::function<void(bool success, const std::string &reason)> on_register_func) {
        _on_register_func = std::move(on_register_func);
    }
private:
    void eventLoop();
    void sendInitialRegister();
    void sendRefreshRegister();
    void onEventMessageNew(eXosip_event_t* event);

    std::function<void()> _on_init_func;
    std::function<void(bool success,const std::string& reason)> _on_register_func;

    toolkit::EventPoller::Ptr _poller;
    eXosip_t* _ex_ctx;
    std::atomic<bool> _running;
    std::atomic<bool> _registered;
    std::thread _event_thread;

    // 注册参数缓存
    std::string _server_ip;
    int _server_port;
    std::string _server_id, _server_domain,_user_id,_password,_device_id;
    std::string _local_ip;
    int _expires,_register_id{};
};
}
#endif // SIP_GB28181_H
