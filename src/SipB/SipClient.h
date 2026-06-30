#ifndef SIP_B_H
#define SIP_B_H

#include <string>
#include <functional>
#include <thread>
#include <atomic>
#include <memory>
#include <eXosip2/eXosip.h>
#include "Poller/EventPoller.h"

//电力系统电网B协议
namespace SipB {

class SipClient :public std::enable_shared_from_this<SipClient>{
public:
    using StatusCallback = std::function<void(bool success, const std::string& reason)>;

    explicit SipClient(toolkit::EventPoller::Ptr poller);
    ~SipClient();

    /** 初始化 eXosip 栈并监听本地 UDP 端口 */
    bool init(bool is_udp = true, int local_port = 5060);

    /**
     * 开启注册流程（启动事件线程，立即发送初始 REGISTER）
     * @param server_ip   SIP 服务器地址
     * @param server_port SIP 服务器端口
     * @param device_code 设备编码（用于 From/To URI）
     * @param domain       SIP 域名
     * @param auth_user    Digest 认证用户名
     * @param auth_pass    Digest 认证密码
     * @param expires      注册过期时间（秒），默认 3600
     * @param callback     注册状态回调（可选）
     */
    bool startRegister(const std::string& server_ip,
                       int server_port,
                       const std::string& device_code,
                       const std::string& domain,
                       const std::string& auth_user,
                       const std::string& auth_pass,
                       int expires = 3600,
                       StatusCallback callback = nullptr);

    /** 停止注册，发送注销，清理资源 */
    void stop();

    /** 是否已注册成功 */
    bool isRegistered() const { return _registered; }

private:
    void eventLoop();
    void sendInitialRegister();
    void sendRefreshRegister();

    toolkit::EventPoller::Ptr _poller;
    struct eXosip_t* _ex_ctx;
    std::atomic<bool> _running;
    std::atomic<bool> _registered;
    std::thread _event_thread;

    // 注册参数缓存
    std::string _server_ip;
    int _server_port;
    std::string _device_code;
    std::string _domain;
    std::string _auth_user;
    std::string _auth_pass;
    int _expires;
    int _register_id;
    StatusCallback _callback;
};
}
#endif // SIP_B_H
