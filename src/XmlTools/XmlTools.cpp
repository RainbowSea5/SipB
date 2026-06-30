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

std::string XmlTools::xmlDocumentToString(xml_document& doc) {
    pugi::xml_node decl = doc.first_child();
    if (!decl || decl.type() != pugi::node_declaration)
    {
        decl = doc.prepend_child(pugi::node_declaration);
    }

    decl.append_attribute("version") = "1.0";
    decl.append_attribute("encoding") = "UTF-8";
    ostringstream oss;
    doc.save(oss," ",format_default,xml_encoding::encoding_auto);
    return oss.str();
}
