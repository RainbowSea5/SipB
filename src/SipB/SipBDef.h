//
// Created by RainbowSea on 2026/7/2.
//

#ifndef SIPB_SIPBDEF_H
#define SIPB_SIPBDEF_H

#include <cstdint>
#include <iostream>
#include <utility>
#include <vector>

#include "pugixml.hpp"

namespace sipB {

static const char* STR_CMD_TYPE{"CmdType"};

static const char* STR_CATA_LOG{"Catalog"};
static const char* STR_DEVICE_INFO{"DeviceInfo"};
static const char* STR_KEEP_ALIVE{"Keepalive"};

static const char* STR_METHOD_MESSAGE{"MESSAGE"};
static const char* STR_METHOD_NOTIFY{"NOTIFY"};

static const char* STR_XML_ROOT_RESPONSE{"Response"};
static const char* STR_XML_ROOT_QUERY{"Query"};
static const char* STR_XML_ROOT_NOTIFY{"Notify"};

static const char* STR_MOBILE_POSITION{"MobilePosition"};

static const char* STR_EVENT_TYPE_REQUEST_RESOURCE{"Request_Resource"};
static const char* STR_EVENT_TYPE_REQUEST_HISTORY_ALARM{"Request_History_Alarm"};

static const char* STR_EVENT_TYPE_RESPONSE_HISTORY_ALARM{"Response_History_Alarm"};

enum class ClientStatus {
    UN_INIT = 0,
    INIT,
    REGISTERING,
    REGISTERED,
    UNREGISTERING,
    UNREGISTER,
};

struct DeviceInfo {
    DeviceInfo(std::string code, int32_t sub_num, std::string name, uint16_t status, int32_t decoder_tag,
        double longitude, double latitude);
    void appendItemToDocument(pugi::xml_node& doc) const;
    static std::string makeQueryResourceResponse(const std::vector<DeviceInfo>& vec,
        const std::string& code, int from_index, int to_index, uint32_t real_num);

    //节点地址编码
    std::string code;
    //当前节点包含的节点数
    int32_t sub_num;
    std::string name;
    //节点状态值 0：不可用，1：可用
    uint16_t status{0};
    //解码插件标签，参照文档中的 RTP Payload 值
    int32_t decoder_tag;
    //经度
    double longitude = 0.0;
    //纬度
    double latitude = 0.0;
};

struct AlarmInfo {
    AlarmInfo(std::string code, std::string utc_begin_time, uint16_t status, std::string type);
    void appendItemToDocument(pugi::xml_node& doc) const;
    static std::string makeQueryHistoryAlarmResponse(const std::vector<AlarmInfo>& vec,
        int from_index, int to_index, uint32_t real_num);

    //告警源地址编码
    std::string code;
    //实际开始时间
    std::string utc_begin_time;
    //告警状态
    uint16_t status{0};
    //告警类型
    std::string type;
};

struct SubscribeInfo {
    SubscribeInfo(std::string cmd_type, uint32_t expires, std::string sn_str,uint32_t interval);

    SubscribeInfo(std::string cmd_type, uint32_t expires, std::string sn_str);

    [[nodiscard]] bool isMobilePosition() const { return cmd_type == STR_MOBILE_POSITION;}
    void update(const std::string& sn_str, uint32_t expires,uint32_t interval = 0);
    bool overdue() const;
    bool needReport() const;

    std::string cmd_type;
    // uint32_t expires;
    uint32_t interval{0};
    std::string sn_str;
    uint64_t expiration_time,last_report_time{0};
};

}


#endif //SIPB_SIPBDEF_H
