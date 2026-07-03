//
// Created by RainbowSea on 2026/7/2.
//

#include "SipBDef.h"

#include <iomanip>
#include <utility>

#include "Network/BufferSock.h"
#include "Util/logger.h"
#include "XmlTools/XmlTools.h"

namespace sipB {

DeviceInfo::DeviceInfo(std::string code, int32_t sub_num, std::string name, uint16_t status, int32_t decoder_tag,
    double longitude, double latitude): code(std::move(code)),
                                        sub_num(sub_num),
                                        name(std::move(name)),
                                        status(status),
                                        decoder_tag(decoder_tag),
                                        longitude(longitude),
                                        latitude(latitude) {
}

void DeviceInfo::appendItemToDocument(pugi::xml_node &doc) const {
    // State Grid B Push_Resource format: Item uses attributes
    auto item = doc.append_child("Item");
    item.append_attribute("Code").set_value(code);
    item.append_attribute("Name").set_value(name.c_str());
    item.append_attribute("Status").set_value(status);
    item.append_attribute("DecoderTag").set_value(decoder_tag);
    item.append_attribute("Longitude").set_value(longitude);
    item.append_attribute("Latitude").set_value(latitude);
    item.append_attribute("SubNum").set_value(sub_num);
}

std::string DeviceInfo::makeQueryResourceResponse(const std::vector<DeviceInfo> &items, const std::string &code, int from_index,
    int to_index, uint32_t real_num) {

    pugi::xml_document doc;
    auto root = doc.append_child("SIP_XML");
    root.append_attribute("EventType").set_value("Response_Resource");

    auto sub_list = root.append_child("SubList");
    sub_list.append_attribute("Code").set_value(code);
    sub_list.append_attribute("RealNum").set_value(real_num);
    sub_list.append_attribute("SubNum").set_value(items.size());
    sub_list.append_attribute("FromIndex").set_value(from_index);
    sub_list.append_attribute("ToIndex").set_value(to_index);

    for (auto &info: items) {
        info.appendItemToDocument(sub_list);
    }

    return XmlTools::xmlDocumentToString(doc);
}

AlarmInfo::AlarmInfo(std::string code, std::string utc_begin_time, uint16_t status, int32_t type)
    : code(std::move(code)),
      utc_begin_time(std::move(utc_begin_time)),
      status(status),
      type(type) {
}

void AlarmInfo::appendItemToDocument(pugi::xml_node &doc) const {
    auto item = doc.append_child("Item");
    item.append_attribute("Code").set_value(code.c_str());
    item.append_attribute("BeginTime").set_value(utc_begin_time.c_str());
    item.append_attribute("Status").set_value(status);
    item.append_attribute("Type").set_value(type);
}

std::string AlarmInfo::makeQueryHistoryAlarmResponse(const std::vector<AlarmInfo> &items, int from_index,
    int to_index, uint32_t real_num) {
    pugi::xml_document doc;
    auto root = doc.append_child("SIP_XML");
    root.append_attribute("EventType").set_value(STR_EVENT_TYPE_RESPONSE_HISTORY_ALARM);

    auto sub_list = root.append_child("SubList");
    sub_list.append_attribute("RealNum").set_value(real_num);
    sub_list.append_attribute("SubNum").set_value(items.size());
    sub_list.append_attribute("FromIndex").set_value(from_index);
    sub_list.append_attribute("ToIndex").set_value(to_index);

    for (auto &info: items) {
        info.appendItemToDocument(sub_list);
    }

    return XmlTools::xmlDocumentToString(doc);
}

RecordInfo::RecordInfo(std::string file_name, std::string file_url, std::string begin_time, std::string end_time,
    int64_t size, int32_t decoder_tag, int32_t type)
    : file_name(std::move(file_name)),
      file_url(std::move(file_url)),
      begin_time(std::move(begin_time)),
      end_time(std::move(end_time)),
      size(size),
      decoder_tag(decoder_tag),
      type(type) {
}

void RecordInfo::appendItemToDocument(pugi::xml_node &doc) const {
    auto item = doc.append_child("Item");
    item.append_attribute("FileName").set_value(file_name.c_str());
    item.append_attribute("FileUrl").set_value(file_url.c_str());
    item.append_attribute("BeginTime").set_value(begin_time.c_str());
    item.append_attribute("EndTime").set_value(end_time.c_str());
    item.append_attribute("Size").set_value(size);
    item.append_attribute("DecoderTag").set_value(decoder_tag);
    item.append_attribute("Type").set_value(type);
}

std::string RecordInfo::makeQueryHistoryVideoResponse(const std::vector<RecordInfo> &items, int from_index,
    int to_index, uint32_t real_num) {
    pugi::xml_document doc;
    auto root = doc.append_child("SIP_XML");
    root.append_attribute("EventType").set_value(STR_EVENT_TYPE_RESPONSE_HISTORY_VIDEO);

    auto sub_list = root.append_child("SubList");
    sub_list.append_attribute("RealNum").set_value(real_num);
    sub_list.append_attribute("SubNum").set_value(items.size());
    sub_list.append_attribute("FromIndex").set_value(from_index);
    sub_list.append_attribute("ToIndex").set_value(to_index);

    for (auto &info: items) {
        info.appendItemToDocument(sub_list);
    }

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
