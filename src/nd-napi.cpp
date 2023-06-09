// Netify Agent
// Copyright (C) 2015-2022 eGloo Incorporated <http://www.egloo.ca>
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <iomanip>
#include <iostream>
#include <set>
#include <map>
#include <queue>
#include <sstream>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <list>
#include <vector>
#include <locale>
#include <atomic>
#include <regex>
#include <mutex>

#include <sys/stat.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/resource.h>

#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <getopt.h>
#include <signal.h>
#include <time.h>
#include <unistd.h>
#include <locale.h>
#include <syslog.h>
#include <fcntl.h>

#include <arpa/inet.h>
#include <arpa/nameser.h>

#include <netdb.h>
#include <netinet/in.h>

#include <net/if.h>
#include <net/if_arp.h>
#if defined(__linux__)
#include <linux/if_packet.h>
#elif defined(__FreeBSD__)
#include <net/if_dl.h>
#endif

#define __FAVOR_BSD 1
#include <netinet/tcp.h>
#undef __FAVOR_BSD

#include <curl/curl.h>
#include <pcap/pcap.h>
#include <pthread.h>
#include <resolv.h>

#include <nlohmann/json.hpp>
using json = nlohmann::json;

#ifdef _ND_USE_CONNTRACK
#include <libnetfilter_conntrack/libnetfilter_conntrack.h>
#endif

#if defined(_ND_USE_LIBTCMALLOC) && defined(HAVE_GPERFTOOLS_MALLOC_EXTENSION_H)
#include <gperftools/malloc_extension.h>
#elif defined(HAVE_MALLOC_TRIM)
#include <malloc.h>
#endif

#include <radix/radix_tree.hpp>

using namespace std;

#include "netifyd.h"

#include "nd-config.h"
#include "nd-signal.h"
#include "nd-ndpi.h"
#include "nd-risks.h"
#include "nd-serializer.h"
#include "nd-packet.h"
#include "nd-json.h"
#include "nd-util.h"
#include "nd-addr.h"
#ifdef _ND_USE_NETLINK
#include "nd-netlink.h"
#endif
#include "nd-apps.h"
#include "nd-protos.h"
#include "nd-category.h"
#include "nd-flow.h"
#include "nd-flow-map.h"
#include "nd-dhc.h"
#include "nd-fhc.h"
#include "nd-thread.h"
class ndInstanceStatus;
#include "nd-plugin.h"
#include "nd-instance.h"
#include "nd-flow-parser.h"
#ifdef _ND_USE_CONNTRACK
#include "nd-conntrack.h"
#endif
#include "nd-detection.h"
#include "nd-capture.h"
#ifdef _ND_USE_LIBPCAP
#include "nd-capture-pcap.h"
#endif
#ifdef _ND_USE_TPACKETV3
#include "nd-capture-tpv3.h"
#endif
#ifdef _ND_USE_NFQUEUE
#include "nd-capture-nfq.h"
#endif
#include "nd-base64.h"
#include "nd-napi.h"

#define _ND_DEBUG_CURL     1

static int ndNetifyApiThread_curl_debug(CURL *ch __attribute__((unused)),
    curl_infotype type, char *data, size_t size, void *param)
{
    string buffer;
    if (! _ND_DEBUG_CURL) return 0;

    ndThread *thread = reinterpret_cast<ndThread *>(param);

    switch (type) {
    case CURLINFO_TEXT:
        buffer.assign(data, size);
        nd_dprintf("%s: %s", thread->GetTag().c_str(), buffer.c_str());
        break;
    case CURLINFO_HEADER_IN:
        buffer.assign(data, size);
        nd_dprintf("%s: <-- %s", thread->GetTag().c_str(), buffer.c_str());
        break;
    case CURLINFO_HEADER_OUT:
        buffer.assign(data, size);
        nd_dprintf("%s: --> %s", thread->GetTag().c_str(), buffer.c_str());
        break;
    case CURLINFO_DATA_IN:
        nd_dprintf("%s: <-- %lu data bytes\n", thread->GetTag().c_str(), size);
        break;
    case CURLINFO_DATA_OUT:
        nd_dprintf("%s: --> %lu data bytes\n", thread->GetTag().c_str(), size);
        break;
    case CURLINFO_SSL_DATA_IN:
        nd_dprintf("%s: <-- %lu SSL bytes\n", thread->GetTag().c_str(), size);
        break;
    case CURLINFO_SSL_DATA_OUT:
        nd_dprintf("%s: --> %lu SSL bytes\n", thread->GetTag().c_str(), size);
        break;
    default:
        break;
    }

    return 0;
}

static size_t ndNetifyApiThread_read_data(
    char *data, size_t size, size_t nmemb, void *user)
{
    size_t length = size * nmemb;
    ndNetifyApiThread *thread_napi = reinterpret_cast<ndNetifyApiThread *>(user);

    thread_napi->AppendData((const char *)data, length);

    return length;
}

static size_t ndNetifyApiThread_parse_header(
    char *data, size_t size, size_t nmemb, void *user)
{
    size_t length = size * nmemb;

    // size_t ndNetifyApiThread_parse_header(char*, size_t, size_t, void*): HTTP/1.1 200 OK, 1, 17
    //nd_dprintf("%s: %s, %u, %u\n", __PRETTY_FUNCTION__, data, size, nmemb);

    if (size != 1 || length == 0) return 0;

    ndNetifyApiThread *thread_napi = reinterpret_cast<ndNetifyApiThread *>(user);

    string header_data;
    header_data.assign(data, length);

    thread_napi->ParseHeader(header_data);

    return length;
}

#if (LIBCURL_VERSION_NUM < 0x073200)
static int ndNetifyApiThread_progress(void *user,
    double dltotal __attribute__((unused)), double dlnow __attribute__((unused)),
    double ultotal __attribute__((unused)), double ulnow __attribute__((unused)))
#else
static int ndNetifyApiThread_progress(void *user,
    curl_off_t dltotal __attribute__((unused)), curl_off_t dlnow __attribute__((unused)),
    curl_off_t ultotal __attribute__((unused)), curl_off_t ulnow __attribute__((unused)))
#endif
{
    ndNetifyApiThread *thread_napi = reinterpret_cast<ndNetifyApiThread *>(user);

    if (thread_napi->ShouldTerminate()) return 1;

    return 0;
}

ndNetifyApiThread::ndNetifyApiThread()
    : ndThread("nap-api-update"),
    ch(NULL), headers_tx(NULL)
{
    if ((ch = curl_easy_init()) == NULL)
        throw ndThreadException("curl_easy_init");

    curl_easy_setopt(ch, CURLOPT_MAXREDIRS, 3L);
    curl_easy_setopt(ch, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(ch, CURLOPT_CONNECTTIMEOUT, 20L);
    curl_easy_setopt(ch, CURLOPT_TIMEOUT, 60L);
    curl_easy_setopt(ch, CURLOPT_NOSIGNAL, 1L);

    curl_easy_setopt(ch, CURLOPT_WRITEFUNCTION, ndNetifyApiThread_read_data);
    curl_easy_setopt(ch, CURLOPT_WRITEDATA, static_cast<void *>(this));

    curl_easy_setopt(ch, CURLOPT_HEADERFUNCTION, ndNetifyApiThread_parse_header);
    curl_easy_setopt(ch, CURLOPT_HEADERDATA, static_cast<void *>(this));

    curl_easy_setopt(ch, CURLOPT_NOPROGRESS, 0L);
#if (LIBCURL_VERSION_NUM < 0x073200)
    curl_easy_setopt(ch, CURLOPT_PROGRESSFUNCTION, ndNetifyApiThread_progress);
    curl_easy_setopt(ch, CURLOPT_PROGRESSDATA, static_cast<void *>(this));
#else
    curl_easy_setopt(ch, CURLOPT_XFERINFOFUNCTION, ndNetifyApiThread_progress);
    curl_easy_setopt(ch, CURLOPT_XFERINFODATA, static_cast<void *>(this));
#endif
#ifdef _ND_WITH_LIBCURL_ZLIB
#if (LIBCURL_VERSION_NUM >= 0x072106)
    curl_easy_setopt(ch, CURLOPT_ACCEPT_ENCODING, "gzip");
#endif
#endif // _ND_WITH_LIBCURL_ZLIB
    if (_ND_DEBUG_CURL) {
        curl_easy_setopt(ch, CURLOPT_VERBOSE, 1L);
        curl_easy_setopt(ch, CURLOPT_DEBUGFUNCTION, ndNetifyApiThread_curl_debug);
        curl_easy_setopt(ch, CURLOPT_DEBUGDATA, static_cast<void *>(this));
    }

//    curl_easy_setopt(ch, CURLOPT_SSL_VERIFYPEER, 0L);
//    curl_easy_setopt(ch, CURLOPT_SSL_VERIFYHOST, 0L);
//    curl_easy_setopt(ch, CURLOPT_SSLVERSION, CURL_SSLVERSION_TLSv1);

    ostringstream header;
    header << "User-Agent: " << nd_get_version_and_features();

    headers_tx = curl_slist_append(headers_tx, header.str().c_str());
    headers_tx = curl_slist_append(headers_tx, "Content-Type: application/json");

    header.str("");

    string uuid;
    ndGC.LoadUUID(ndGlobalConfig::UUID_AGENT, uuid);
    header << "X-UUID: " << uuid;

    headers_tx = curl_slist_append(headers_tx, header.str().c_str());
    header.str("");

    ndGC.LoadUUID(ndGlobalConfig::UUID_SERIAL, uuid);
    header << "X-UUID-Serial: " << uuid;

    headers_tx = curl_slist_append(headers_tx, header.str().c_str());

    curl_easy_setopt(ch, CURLOPT_HTTPHEADER, headers_tx);
}

ndNetifyApiThread::~ndNetifyApiThread()
{
    Terminate();

    Join();

    if (ch != NULL) {
        curl_easy_cleanup(ch);
        ch = NULL;
    }

    if (headers_tx != NULL) {
        curl_slist_free_all(headers_tx);
        headers_tx = NULL;
    }
}

void ndNetifyApiThread::ParseHeader(const string &header_raw)
{
    string key, value;
    size_t p = string::npos;
    if ((p = header_raw.find_first_of(":")) != string::npos) {
        key = header_raw.substr(0, p);
        value = header_raw.substr(p + 1);
    }

    if (! key.empty() && ! value.empty()) {

        transform(key.begin(), key.end(), key.begin(),
            [](unsigned char c){ return tolower(c); }
        );

        nd_trim(key);
        nd_trim(value);

        if (headers_rx.find(key) == headers_rx.end()) {
            headers_rx[key] = value;
            if (_ND_DEBUG_CURL) {
                nd_dprintf("%s: header: %s: %s\n",
                    tag.c_str(), key.c_str(), value.c_str());
            }
        }
    }
}

unsigned ndNetifyApiThread::Get(const string &url)
{
    CURLcode curl_rc;

    curl_easy_setopt(ch, CURLOPT_URL, url.c_str());

    body_data.clear();
    headers_rx.clear();

    nd_dprintf("%s: GET: %s\n", tag.c_str(), url.c_str());

    if ((curl_rc = curl_easy_perform(ch)) != CURLE_OK)
        throw curl_rc;

    long http_rc = 0;
    if ((curl_rc = curl_easy_getinfo(ch,
        CURLINFO_RESPONSE_CODE, &http_rc)) != CURLE_OK)
        throw curl_rc;

    char *content_type = NULL;
    curl_easy_getinfo(ch, CURLINFO_CONTENT_TYPE, &content_type);

    double content_length = 0.0f;
#if (LIBCURL_VERSION_NUM < 0x075500)
    curl_easy_getinfo(ch, CURLINFO_CONTENT_LENGTH_DOWNLOAD, &content_length);
#else
    curl_easy_getinfo(ch, CURLINFO_CONTENT_LENGTH_DOWNLOAD_T, &content_length);
#endif

    if (http_rc == 200) {
        if (content_type == NULL) throw string("Content-type is NULL");

        if (content_length == 0.0f) throw string("Zero-length content length");
    }

    return (unsigned)http_rc;
}

void *ndNetifyApiRefreshCategories::Entry(void)
{
    unsigned page = 0, pages = 1;

    unordered_map<unsigned, string> requests;

    requests[ndCAT_TYPE_APP] = "/lookup/applications";
    requests[ndCAT_TYPE_PROTO] = "/lookup/protocols";

    queue<ndCategoryType> cqueue;
    cqueue.push(ndCAT_TYPE_PROTO);
    cqueue.push(ndCAT_TYPE_MAX);

    ndCategoryType cid = ndCAT_TYPE_APP;

    while (! ShouldTerminate()) {

        unsigned rc = 0;

        // Done?
        if (cid == ndCAT_TYPE_MAX || cqueue.size() == 0) {
            if (categories.Save())
                ndi.SendIPC(ndInstance::ndIPC_UPDATE_NAPI_DONE);
            break;
        }

        ostringstream url;
        url << ndGC.url_napi << requests[cid];
        url << "?vendor=" << ndGC.napi_vendor;
        url << "&settings_limit=100";

        if (page > 0) url << "&page=" << page;

        try {
            rc = Get(url.str().c_str());
        }
        catch (CURLcode &rc) {
            nd_printf("%s: Error: %s\n", tag.c_str(), curl_easy_strerror(rc));
            break;
        }
        catch (const string &es) {
            nd_printf("%s: Error: %s\n", tag.c_str(), es.c_str());
            break;
        }
        catch (exception &e) {
            nd_printf("%s: Unknown error: %s.\n", tag.c_str(), e.what());
            break;
        }
        catch (...) {
            nd_printf("%s: Unknown error.\n", tag.c_str());
            break;
        }

        if (rc == 429) {
            unsigned ttl = 0;
            if (headers_rx.find("retry-after") != headers_rx.end()) {
                string retry_value = headers_rx["retry-after"];
                if (isdigit(retry_value[0]))
                    ttl = (unsigned)strtoul(retry_value.c_str(), NULL, 0);
            }

            if (ttl == 0) ttl = _ND_NAPI_RETRY_TTL;
            while (! ShouldTerminate() && ttl != 0) {
                sleep(1);
                ttl--;
            }

            continue;
        }

        if (rc != 200) {
            nd_printf("%s: HTTP return code: %u\n", tag.c_str(), rc);
            break;
        }

        try {
            json j = json::parse(body_data);

            unsigned code = j["status_code"].get<unsigned>();
            string message = j["status_message"].get<string>();

            if (code != 0) {
                nd_dprintf("%s: result: %s [%u]\n",
                    tag.c_str(), message.c_str(), code);
                break;
            }

            auto it_data = j.find("data");

            if (it_data != j.end())
                categories.Load(cid, (*it_data));
            else {
                nd_printf("%s: Missing element: data\n", tag.c_str());
                nd_dprintf("%s\n", body_data.c_str());
                break;
            }

            auto it_di = j.find("data_info");

            if (it_di != j.end()) {
                if (page == 0) {
                    page = 2;
                    pages = (*it_di)["total_pages"].get<unsigned>();
                }
                else if (page == pages) {
                    page = 0;
                    cid = cqueue.front();
                    cqueue.pop();
                }
                else page++;
            }
            else {
                page = 0;
                pages = 1;
                cid = cqueue.front();
                cqueue.pop();
            }
        } catch (...) {
            nd_printf("%s: JSON decode error.\n", tag.c_str());
            break;
        }
    }

    return NULL;
}

// vi: expandtab shiftwidth=4 softtabstop=4 tabstop=4
