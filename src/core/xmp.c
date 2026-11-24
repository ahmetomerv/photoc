#include "xmp_internal.h"

#include <limits.h>
#include <libxml/parser.h>
#include <libxml/tree.h>
#include <stdlib.h>
#include <string.h>

static bool xmp_private_name(const xmlNs *ns, const xmlChar *name)
{
    if (ns == NULL || ns->href == NULL || name == NULL)
        return false;
    const char *uri = (const char *)ns->href;
    const char *local = (const char *)name;
    if (strcmp(uri, "http://ns.adobe.com/exif/1.0/") == 0)
        return strncmp(local, "GPS", 3) == 0 ||
               strcmp(local, "BodySerialNumber") == 0 ||
               strcmp(local, "CameraSerialNumber") == 0 ||
               strcmp(local, "LensSerialNumber") == 0 ||
               strcmp(local, "ImageUniqueID") == 0;
    if (strcmp(uri, "http://ns.adobe.com/exif/1.0/aux/") == 0 ||
        strcmp(uri, "http://cipa.jp/exif/1.0/") == 0)
        return strstr(local, "SerialNumber") != NULL ||
               strcmp(local, "ImageUniqueID") == 0;
    if (strcmp(uri, "http://ns.adobe.com/tiff/1.0/") == 0)
        return strcmp(local, "Artist") == 0 ||
               strcmp(local, "ImageUniqueID") == 0;
    if (strcmp(uri, "http://ns.adobe.com/photoshop/1.0/") == 0)
        return strcmp(local, "City") == 0 || strcmp(local, "State") == 0 ||
               strcmp(local, "Country") == 0 ||
               strcmp(local, "Location") == 0 || strcmp(local, "Credit") == 0 ||
               strcmp(local, "CaptionWriter") == 0 ||
               strcmp(local, "Source") == 0;
    if (strcmp(uri, "http://iptc.org/std/Iptc4xmpCore/1.0/xmlns/") == 0 ||
        strcmp(uri, "http://iptc.org/std/Iptc4xmpExt/2008-02-29/") == 0)
        return strstr(local, "Location") != NULL ||
               strcmp(local, "City") == 0 ||
               strcmp(local, "ProvinceState") == 0 ||
               strcmp(local, "CountryName") == 0 ||
               strcmp(local, "CountryCode") == 0 ||
               strcmp(local, "Creator") == 0 ||
               strcmp(local, "CreatorContactInfo") == 0 ||
               strstr(local, "SerialNumber") != NULL;
    if (strcmp(uri, "http://purl.org/dc/elements/1.1/") == 0)
        return strcmp(local, "creator") == 0;
    if (strcmp(uri, "http://ns.adobe.com/xap/1.0/rights/") == 0)
        return strcmp(local, "Owner") == 0;
    if (strcmp(uri, "http://ns.adobe.com/xap/1.0/mm/") == 0)
        return strcmp(local, "DocumentID") == 0 ||
               strcmp(local, "InstanceID") == 0 ||
               strcmp(local, "OriginalDocumentID") == 0;
    return false;
}

static bool remove_xmp_private(xmlNode *node)
{
    bool changed = false;
    for (xmlAttr *attr = node->properties; attr != NULL;) {
        xmlAttr *following = attr->next;
        if (xmp_private_name(attr->ns, attr->name)) {
            xmlRemoveProp(attr);
            changed = true;
        }
        attr = following;
    }
    for (xmlNode *child = node->children; child != NULL;) {
        xmlNode *next = child->next;
        if (child->type == XML_ELEMENT_NODE &&
            xmp_private_name(child->ns, child->name)) {
            xmlUnlinkNode(child);
            xmlFreeNode(child);
            changed = true;
        } else {
            changed |= remove_xmp_private(child);
        }
        child = next;
    }
    return changed;
}

photoc_jpeg_edit_result
photoc_xmp_remove_private(const char *xml, size_t length, unsigned char **out,
                          size_t *out_length, bool *changed)
{
    *out = NULL;
    *out_length = 0;
    *changed = false;
    if (xml == NULL || length == 0 || length > INT_MAX)
        return PHOTOC_JPEG_EDIT_UNSUPPORTED_XMP;
    xmlDocPtr doc = xmlReadMemory(xml, (int)length, "embedded.xmp", NULL,
                                  XML_PARSE_NONET | XML_PARSE_NOERROR |
                                      XML_PARSE_NOWARNING);
    if (doc == NULL)
        return PHOTOC_JPEG_EDIT_UNSUPPORTED_XMP;
    if (doc->intSubset != NULL || doc->extSubset != NULL ||
        xmlDocGetRootElement(doc) == NULL) {
        xmlFreeDoc(doc);
        return PHOTOC_JPEG_EDIT_UNSUPPORTED_XMP;
    }
    *changed = remove_xmp_private((xmlNode *)doc);
    if (!*changed) {
        xmlFreeDoc(doc);
        return PHOTOC_JPEG_EDIT_OK;
    }
    xmlChar *dumped = NULL;
    int dumped_length = 0;
    xmlDocDumpMemoryEnc(doc, &dumped, &dumped_length, "UTF-8");
    xmlFreeDoc(doc);
    if (dumped == NULL || dumped_length <= 0) {
        xmlFree(dumped);
        return PHOTOC_JPEG_EDIT_UNSUPPORTED_XMP;
    }
    unsigned char *copy = malloc((size_t)dumped_length);
    if (copy == NULL) {
        xmlFree(dumped);
        return PHOTOC_JPEG_EDIT_NO_MEMORY;
    }
    memcpy(copy, dumped, (size_t)dumped_length);
    xmlFree(dumped);
    *out = copy;
    *out_length = (size_t)dumped_length;
    return PHOTOC_JPEG_EDIT_OK;
}
