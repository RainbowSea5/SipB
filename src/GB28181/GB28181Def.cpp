//
// Created by RainbowSea on 2026/7/1.
//
#include "GB28181Def.h"

#include <iomanip>
#include <utility>

#include "Network/BufferSock.h"
#include "XmlTools/XmlTools.h"

namespace gb28181 {
std::string DeviceInfo::createDeviceInfoResponse(const std::string &c_device_id, const std::string &sn_str) const {
    pugi::xml_document doc;
    auto root = doc.append_child(STR_XML_ROOT_RESPONSE);
    root.append_child(STR_CMD_TYPE).append_child(pugi::node_pcdata).set_value(STR_DEVICE_INFO);
    root.append_child("SN").append_child(pugi::node_pcdata).set_value(sn_str);
    root.append_child("DeviceID").append_child(pugi::node_pcdata).set_value(c_device_id);
    root.append_child("Result").append_child(pugi::node_pcdata).set_value("OK");

    root.append_child("Manufacturer").append_child(pugi::node_pcdata).set_value(manufacturer);
    root.append_child("Model").append_child(pugi::node_pcdata).set_value(model);
    root.append_child("Firmware").append_child(pugi::node_pcdata).set_value(firmware);

    return XmlTools::xmlDocumentToString(doc);
}

void DeviceInfo::appendItemToDocument(pugi::xml_node &doc, bool detail) const {
    auto item = doc.append_child("Item");

    // 基本字段（无论 detail 都包含）
    item.append_child("DeviceID").append_child(pugi::node_pcdata).set_value(device_id.c_str());
    item.append_child("Name").append_child(pugi::node_pcdata).set_value(name.c_str());
    item.append_child("Status").append_child(pugi::node_pcdata).set_value(status.c_str());
    // 制造商/型号
    item.append_child("Manufacturer").append_child(pugi::node_pcdata).set_value(manufacturer.c_str());
    item.append_child("Model").append_child(pugi::node_pcdata).set_value(model.c_str());

    if (!detail) {
        return;
    }
    //位置
    item.append_child("Longitude").append_child(pugi::node_pcdata).set_value(std::to_string(longitude).c_str());
    item.append_child("Latitude").append_child(pugi::node_pcdata).set_value(std::to_string(latitude).c_str());

    // 详细描述
    item.append_child("ParentID").append_child(pugi::node_pcdata).set_value(parentId.c_str());
    item.append_child("Parental").append_child(pugi::node_pcdata).set_value(std::to_string(parental).c_str());
    item.append_child("Owner").append_child(pugi::node_pcdata).set_value(owner.c_str());
    item.append_child("CivilCode").append_child(pugi::node_pcdata).set_value(civil_code.c_str());
    item.append_child("Block").append_child(pugi::node_pcdata).set_value(block.c_str());
    item.append_child("Address").append_child(pugi::node_pcdata).set_value(address.c_str());

    // 安全与认证
    item.append_child("SafetyWay").append_child(pugi::node_pcdata).set_value(std::to_string(safetyWay).c_str());
    item.append_child("RegisterWay").append_child(pugi::node_pcdata).set_value(std::to_string(registerWay).c_str());
    item.append_child("CertNum").append_child(pugi::node_pcdata).set_value(certNum.c_str());
    item.append_child("Certifiable").append_child(pugi::node_pcdata).set_value(std::to_string(certifiable).c_str());
    item.append_child("ErrCode").append_child(pugi::node_pcdata).set_value(std::to_string(errCode).c_str());
    item.append_child("EndTime").append_child(pugi::node_pcdata).set_value(endTime.c_str());
    item.append_child("Secrecy").append_child(pugi::node_pcdata).set_value(std::to_string(secrecy).c_str());

    // 网络
    item.append_child("IPAddress").append_child(pugi::node_pcdata).set_value(ipAddress.c_str());
    item.append_child("Port").append_child(pugi::node_pcdata).set_value(std::to_string(port).c_str());
    item.append_child("Password").append_child(pugi::node_pcdata).set_value(password.c_str());
}

MobilePositionInfo::MobilePositionInfo(std::string utc_time_str, double longitude, double latitude, double direction,
                                       double altitude) : utc_time_str(std::move(utc_time_str)),
                                                          longitude(longitude),
                                                          latitude(latitude),
                                                          direction(direction),
                                                          altitude(altitude) {
}

MobilePositionInfo::MobilePositionInfo(uint32_t time_stamp, double longitude, double latitude, double direction,
                                       double altitude) : longitude(longitude), latitude(latitude),
                                                          direction(direction),
                                                          altitude(altitude) {
    std::tm tm{};
    auto ts = static_cast<time_t>(time_stamp);
    gmtime_r(&ts, &tm);

    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y-%m-%dT%H:%M:%SZ");
    utc_time_str = oss.str();
}

std::string MobilePositionInfo::createMobilePositionXml(const std::string& device_id,const std::string& sn_str) const {
    pugi::xml_document doc;
    auto root = doc.append_child(STR_XML_ROOT_NOTIFY);
    root.append_child(STR_CMD_TYPE).append_child(pugi::node_pcdata).set_value(STR_MOBILE_POSITION);
    root.append_child("SN").append_child(pugi::node_pcdata).set_value(sn_str);
    root.append_child("DeviceID").append_child(pugi::node_pcdata).set_value(device_id);
    root.append_child("Time").append_child(pugi::node_pcdata).set_value(utc_time_str);

    root.append_child("Longitude").append_child(pugi::node_pcdata).set_value(std::to_string(longitude));
    root.append_child("Latitude").append_child(pugi::node_pcdata).set_value(std::to_string(latitude));
    root.append_child("Speed").append_child(pugi::node_pcdata).set_value(std::to_string(speed));
    root.append_child("Direction").append_child(pugi::node_pcdata).set_value(std::to_string(direction));
    root.append_child("Altitude").append_child(pugi::node_pcdata).set_value(std::to_string(altitude));

    return XmlTools::xmlDocumentToString(doc);
}

SubscribeInfo::SubscribeInfo(std::string cmd_type, uint32_t expires, std::string sn_str, uint32_t interval)
    : cmd_type(std::move(cmd_type)),interval(interval),sn_str(std::move(sn_str)) {

    expiration_time = toolkit::getCurrentMillisecond()/1000 + expires;
}

SubscribeInfo::SubscribeInfo(std::string cmd_type, uint32_t expires, std::string sn_str)
    : cmd_type(std::move(cmd_type)),sn_str(std::move(sn_str)) {

    expiration_time = toolkit::getCurrentMillisecond()/1000 + expires;
}

void SubscribeInfo::update(const std::string &c_sn_str, uint32_t expires, uint32_t c_interval) {
    if (this->sn_str != sn_str) {
        this->sn_str = c_sn_str;
    }
    expiration_time = toolkit::getCurrentMillisecond()/1000 + expires;
    this->interval = c_interval;
}

bool SubscribeInfo::overdue() const {
    return expiration_time < toolkit::getCurrentMillisecond()/1000;
}

bool SubscribeInfo::needReport() const {
    return toolkit::getCurrentMillisecond()/1000 - last_report_time >= interval;
}
}
