//
// Created by RainbowSea on 2026/6/26.
//

#include "SipTools.h"
#include "Util/logger.h"
#include "XmlTools/XmlTools.h"
using namespace std;
using namespace XmlTools;
namespace SipTools {
std::string sipMessageToString(osip_message_t *msg) {
    if (msg == nullptr) {
        return {};
    }

    char *buffer = nullptr;
    size_t length = 0;

    int ret = osip_message_to_str(msg, &buffer, &length);
    if (ret != 0 || buffer == nullptr) {
        return {};
    }

    std::string result(buffer, length);

    osip_free(buffer);

    return result;
}

std::string sipMessageGetBody(const osip_message_t *msg) {
    if (msg == nullptr) {
        return {};
    }

    osip_body_t *body = nullptr;
    int ret = osip_message_get_body(msg, 0, &body);

    if (ret != 0 || body == nullptr || body->body == nullptr) {
        return {};
    }

    return {body->body, body->length};
}

bool isXml(const osip_message_t *msg)
{
    if (msg == nullptr || msg->content_type == nullptr)
        return false;

    auto *ct = msg->content_type;

    std::string subtype = ct->subtype ? ct->subtype : "";

    std::transform(subtype.begin(), subtype.end(), subtype.begin(),
                   [](unsigned char c) { return std::tolower(c); });

    return subtype.find("xml") != std::string::npos;
}

pugi::xml_document sipMessageGetBodyXmlDocument(const osip_message_t *msg) {
    if (msg == nullptr || !isXml(msg)) {
        return {};
    }

    osip_body_t *body = nullptr;
    int ret = osip_message_get_body(msg, 0, &body);

    if (ret != 0 || body == nullptr || body->body == nullptr) {
        return {};
    }

    return stringToXmlDocument({body->body, body->length});
}

void printEvent(eXosip_event_t *event, const char *user_id) {
    if (!event)
        return;
    auto msg = event->request;

    auto request = event->request;
    auto response = event->response;
    bool is_my_request = request && osip_strcasecmp(request->from->url->username, user_id) == 0;

    auto request_str = sipMessageToString(request);
    auto response_str = sipMessageToString(response);

    auto request_doc = sipMessageGetBodyXmlDocument(request);
    const auto cmd_type = request_doc.child_value("CmdType");


    //心跳不打印
    if (strcmp(cmd_type, "Keepalive")==0) {
        return;
    }
    if (strcmp(cmd_type, "MobilePosition")==0) {
        if (response->content_length && strcmp(response->content_length->value, "0") == 0) {
            PrintD("上报位置，响应无消息体, 状态码 = %d", response->status_code);
            return;
        }
        if (MSG_IS_SUBSCRIBE(request)) {
            PrintD("Request:服务端 发起/刷新 位置订阅, Response: Empty");
            return;
        }
    }
    PrintD("Request:%s\n%s", is_my_request?"是设备发出的":"不是设备发出", request_str.empty()?"Empty":request_str.c_str());
    PrintD("Response:\n%s", response_str.empty()?"Empty":response_str.c_str());
}
}
