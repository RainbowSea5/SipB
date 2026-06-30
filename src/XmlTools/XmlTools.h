//
// Created by RainbowSea on 2026/6/29.
//

#ifndef SIPB_XMLTOOLS_H
#define SIPB_XMLTOOLS_H

#include "pugixml.hpp"
namespace XmlTools {

pugi::xml_document stringToXmlDocument(const std::string& str);

std::string xmlDocumentToString(const pugi::xml_document& doc);

}

#endif //SIPB_XMLTOOLS_H
