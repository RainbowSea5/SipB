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
static const char* STR_EVENT_TYPE_REQUEST_HISTORY_VIDEO{"Request_History_Video"};
static const char* STR_EVENT_TYPE_RESPONSE_HISTORY_VIDEO{"Response_History_Video"};
static const char* STR_EVENT_TYPE_REQUEST_PTZ_CONTROL{"Control_Camera"};
static const char* STR_EVENT_TYPE_CAMERA_SNAP{"Camera_Snap"};
static const char* STR_EVENT_TYPE_SNAPSHOT_NOTIFY{"Snapshot_Notify"};

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

//告警类型按位定义 (B.4 Type 字段, INT32)
enum class AlarmType : uint32_t {
    VIDEO_LOSS     = 1 << 0,   // 视频丢失告警
    MOTION_DETECT  = 1 << 1,   // 移动侦测告警
    VIDEO_BLOCK    = 1 << 2,   // 视频遮挡告警
    DEVICE_HIGH_TEMP = 1 << 8, // 设备高温告警
    DEVICE_LOW_TEMP  = 1 << 9, // 设备低温告警
    FAN_FAULT      = 1 << 10,  // 风扇故障告警
    DISK_FAULT     = 1 << 11,  // 磁盘故障告警
    STATUS_EVENT   = 1 << 16,  // 状态事件告警
};

struct AlarmInfo {
    AlarmInfo(std::string code, std::string utc_begin_time, uint16_t status, int32_t type);
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
    int32_t type;
};

static constexpr uint32_t RECORD_TYPE_ALL = 0xFFFFFFFF; // 请求所有录像类型
//录像类型按位定义 (Type 字段, INT32)
enum class RecordType : uint32_t {
    VIDEO_LOSS_ALARM   = 1 << 0,   // 视频丢失告警录像
    MOTION_DETECTION   = 1 << 1,   // 移动侦测告警录像
    VIDEO_OCCLUSION    = 1 << 2,   // 视频遮挡告警录像
    DEVICE_HIGH_TEMP   = 1 << 8,   // 设备高温告警
    DEVICE_LOW_TEMP    = 1 << 9,   // 设备低温告警
    FAN_FAULT          = 1 << 10,  // 风扇故障告警
    DISK_FAULT         = 1 << 11,  // 磁盘故障告警
    STATUS_EVENT       = 1 << 16,  // 状态事件告警
    SCHEDULED          = 1 << 20,  // 定时录像
    USER_REQUEST       = 1 << 21,  // 用户请求录像
};

struct RecordInfo {
    RecordInfo(std::string file_name, std::string file_url, std::string begin_time, std::string end_time,
        int64_t size, int32_t decoder_tag, int32_t type);
    void appendItemToDocument(pugi::xml_node& doc) const;
    static std::string makeQueryHistoryVideoResponse(const std::vector<RecordInfo>& vec,
        int from_index, int to_index, uint32_t real_num);

    //文件名
    std::string file_name;
    //文件URL
    std::string file_url;
    //实际开始时间
    std::string begin_time;
    //实际结束时间
    std::string end_time;
    //文件大小
    int64_t size{0};
    //解码插件标签
    int32_t decoder_tag{0};
    //类型值
    int32_t type;
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


// 云镜控制命令(B.8)
enum class PtzCommand : int32_t {
    IRIS_PLUS       = 0x0102,
    IRIS_MINUS      = 0x0103,
    FOCUS_NEAR      = 0x0202,
    FOCUS_FAR       = 0x0204,
    ZOOM_WIDE       = 0x0302,
    ZOOM_TELE       = 0x0303,
    UP              = 0x0501,
    DOWN            = 0x0502,
    LEFT            = 0x0503,
    RIGHT           = 0x0504,
    PRESET_SET      = 0x0601,
    PRESET_CALL     = 0x0602,
    PRESET_DEL      = 0x0603,
    STOP            = 0x0901,
    WIPER_ON        = 0x0b01,
    WIPER_OFF       = 0x0b02,
    LIGHT_ON        = 0x0d01,
    LIGHT_OFF       = 0x0d02,
    PTZ_ON          = 0x1101,
    PTZ_OFF         = 0x1102,
};

enum class RtpPayload {
    MP4V_ES = 98,
    AVS_P2 = 99,
    H264 = 100,
    H265 = 108,
};
}


#endif //SIPB_SIPBDEF_H
