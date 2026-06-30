//
// Created by RainbowSea on 2026/6/29.
//

#include "XmlTools.h"

#include "Util/logger.h"

using namespace pugi;
using namespace std;
xml_document XmlTools::stringToXmlDocument(const std::string &str) {
    xml_document doc;
    xml_parse_result res = doc.load_string(str.data(),str.length());

    if (res) {
        return doc;
    }
    ErrorL << "加载xml失败，" << res.description() << "\n" << str;
    return {};
}

std::string XmlTools::xmlDocumentToString(const xml_document& doc) {
    ostringstream oss;
    doc.save(oss);
    return oss.str();
}
