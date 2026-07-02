//
// Created by RainbowSea on 2026/7/2.
//

#ifndef SIPB_SIPBDEF_H
#define SIPB_SIPBDEF_H

#include <cstdint>
#include <iostream>
#include <utility>

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


enum class ClientStatus {
    UN_INIT = 0,
    INIT,
    REGISTERING,
    REGISTERED,
    UNREGISTERING,
    UNREGISTER,
};

struct DeviceInfo {
    DeviceInfo(std::string manufacturer, std::string model, std::string firmware, std::string device_id,
        std::string name)
        : manufacturer(std::move(manufacturer)),
          model(std::move(model)),
          firmware(std::move(firmware)),
          device_id(std::move(device_id)),
          name(std::move(name)) {
    }

    DeviceInfo(std::string manufacturer, std::string model, std::string firmware)
        : manufacturer(std::move(manufacturer)),
          model(std::move(model)),
          firmware(std::move(firmware)) {
    }

    std::string manufacturer;
    std::string model;
    std::string firmware;
    [[nodiscard]] std::string createDeviceInfoResponse(const std::string& device_id,const std::string& sn_str) const;

    //详细数据
    std::string device_id;
    std::string name;

    void appendItemToDocument(pugi::xml_node& doc,bool detail = false,bool use_attr = false) const;

#pragma region 设备详细介绍

    std::string owner;
    //设备或系统所归属的行政区域代码
    std::string civil_code;

    /**
     * @brief 行政区域/所属辖区 (Block) 设备所属的行政区划或细分区域名称，例如："高新区"、"B3栋"
     * 用于对设备进行区域性归类管理
     * @note 必选字段
     */
    std::string block;

    //详细安装地址 (Address)
    std::string address;

    /**
     * @brief 父级/子设备标识 (Parental)
     *
     * 标识当前节点在目录树中的类型：
     * - 1: 有子节点（组织节点、NVR、DVR等父级设备）
     * - 0: 叶子节点（实际的摄像头、编码器等终端设备）
     *
     * @note 必选字段，用于构建目录树的层级结构
     */
    int parental = 0;

    //父设备ID (ParentID)
    std::string parentId;
#pragma endregion

#pragma region 安全与认证信息

    /**
     * @brief 信令安全模式 (SafetyWay)
     *
     * 信令传输的安全加密方式：
     * - 0: 不采用安全机制 (默认)
     * - 2: S/MIME签名
     * - 3: S/MIME加密并签名
     * - 4: 数字摘要方式
     *
     * @note 可选字段，默认为0
     */
    int safetyWay = 0;

    /**
     * @brief 注册认证方式 (RegisterWay)
     *
     * 设备向SIP服务器注册时的认证模式：
     * - 1: 基于RFC 3261的认证注册 (默认)
     * - 2: 基于口令的双向认证
     * - 3: 基于数字证书的双向认证
     *
     * @note 必选字段，默认为1
     */
    int registerWay = 1;

    /**
     * @brief 证书序列号 (CertNum)
     *
     * 当使用数字证书认证时，证书的唯一序列号
     * 用于证书吊销列表(CRL)查询和证书匹配
     *
     * @note 使用证书认证时必选
     */
    std::string certNum;

    /**
     * @brief 证书有效标识 (Certifiable)
     *
     * 标识设备证书是否有效：
     * - 1: 证书有效
     * - 0: 证书无效
     *
     * @note 有证书时必选
     */
    int certifiable = 0;

    /**
     * @brief 证书无效原因码 (ErrCode)
     *
     * 当Certifiable为0时，返回具体错误原因：
     * - 400: 证书已过期
     * - 401: 证书被吊销
     * - 402: 证书格式错误
     * - 403: 证书不受信任
     *
     * @note 证书无效时必选
     */
    int errCode = 0;

    /**
     * @brief 证书终止有效期 (EndTime)
     *
     * 证书的到期时间，格式：YYYY-MM-DDThh:mm:ss
     * 例如："2026-12-31T23:59:59"
     *
     * @note 有证书时必选
     */
    std::string endTime;


    /**
     * @brief 保密属性 (Secrecy)
     *
     * 标识设备或通道是否涉及国家秘密：
     * - 0: 不涉密 (默认)
     * - 1: 涉密
     *
     * 涉密设备在显示和操作时需要特殊权限控制
     *
     * @note 必选字段
     */
    int secrecy = 0;
#pragma endregion

#pragma region 网络连接信息
    std::string ipAddress;
    int port = 5060;
    std::string password;
    //设备当前的运行状态：ON/OFF/UNKNOWN
    std::string status{"ON"};
    std::string decoder_tag;
    int sub_num{0};
#pragma endregion

#pragma region 位置
    double longitude = 0.0;
    double latitude = 0.0;
#pragma endregion
};

struct MobilePositionInfo {
    MobilePositionInfo() = default;
    MobilePositionInfo(std::string utc_time_str, double longitude, double latitude, double direction, double altitude);

    MobilePositionInfo(uint32_t time_stamp, double longitude, double latitude, double direction, double altitude);

    [[nodiscard]] std::string createMobilePositionXml(const std::string& device_id,const std::string& sn_str) const;

    std::string utc_time_str{};
    //经度
    double longitude{};
    //纬度
    double latitude{};
    //方向，高度
    double direction{},altitude{},speed{0};
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
