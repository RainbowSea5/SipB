//
// Created by RainbowSea on 2026/6/26.
//

#ifndef SIPB_SIPTOOLS_H
#define SIPB_SIPTOOLS_H

#include <string>

#include "eXosip2/eXosip.h"
#include "XmlTools/XmlTools.h"
namespace SipTools {

std::string sipMessageToString(osip_message_t* msg);

std::string sipMessageGetBody(const osip_message_t* msg);

pugi::xml_document sipMessageGetBodyXmlDocument(const osip_message_t* msg);

void printEvent(eXosip_event_t* event,const char* user_id = "");
}

#endif //SIPB_SIPTOOLS_H
