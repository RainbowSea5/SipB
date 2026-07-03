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

    /** 订阅回调: event_type, expires */
    using SubscribeCallback = std::function<void(const std::string& event_type, int expires)>;
    void setOnSubscribe(SubscribeCallback cb);

    void setOnInit(std::function<void()> on_init_func);

    void setOnRegister(std::function<void(bool success, const std::string &reason)> on_register_func);

    using OnQueryAllResource = std::function<void(std::function<void(std::vector<DeviceInfo>&)>)>;
    void setOnQueryAllResource(OnQueryAllResource cb);

    using OnQueryResourceInfoCallback = std::function<void(const std::string& code,int from_index, int to_index,
                                                           std::function<void(const std::vector<DeviceInfo>& items, int real_num)>
    )>;
    using OnQueryHistoryAlarmCallback = std::function<void(const std::string& code,
        const std::string& user_code, const std::string& type,
        const std::string& begin_time, const std::string& end_time,
        const std::string& level, int from_index, int to_index,
        std::function<void(const std::vector<AlarmInfo>& items, int real_num)>
    )>;

    void setOnQueryResourceInfo(OnQueryResourceInfoCallback cb);
    void setOnQueryHistoryAlarm(OnQueryHistoryAlarmCallback cb);

    void openDebuggerLog() { _print_message = true;}

    std::weak_ptr<SipBClient> weakPtr();
private:
    void checkNotRegister() const;
    void sendMessage(const std::string& body_str,const std::string& method = STR_METHOD_MESSAGE);
    void onMessageAnswered(eXosip_event_t * event);

    void eventLoop();
    void sendInitialRegister();
    void sendRefreshRegister();

    bool sendUnRegisterMessage();

    void checkRefreshRegister();
    void checkSubscribe();

    void onEventMessageNew(eXosip_event_t* event);

    void handleSubscribe(eXosip_event_t* event);
    void handleMessage(eXosip_event_t* event);

    //[B.2] 主动上报 资源
    void sendResourceReport();

    //[B.3] 处理服务器下发的 资源信息获取
    void handleResourceRequest(eXosip_event_t *event, const pugi::xml_node &xml_root);
    //[B.4] 处理服务器下发的 历史告警查询请求
    void handleHistoryAlarmRequest(eXosip_event_t *event, const pugi::xml_node &xml_root);
    // 通用的 MESSAGE 响应
    void sendMessageResponse(int tid, const std::string& body_str, const std::string& log_tag);
    void sendMessageResponse(const eXosip_event_t* event, const std::string& body_str, const std::string& log_tag) const;

    void processEvent(eXosip_event_t* event);

    void async(const std::function<void()>& func);

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

    OnQueryAllResource _on_query_all_resource;
    OnQueryResourceInfoCallback _on_query_resource_info_cb;
    // 历史告警查询回调
    OnQueryHistoryAlarmCallback _on_query_history_alarm_cb;
#pragma endregion

    std::mutex _mtx;
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

    // 发送消息序列号
    uint16_t _sn{1};
    std::string _sip_from,_sip_to,_sip_proxy;

    bool _print_message{false};
    bool _resource_reported{false};

    //只在event thread插入删除o
    std::map<int,eXosip_event_t*> _wait_answer_event;
    // lockThis 访问
    std::list<std::function<void()>> _run_list;
};
}
#endif // SIP_B_H
