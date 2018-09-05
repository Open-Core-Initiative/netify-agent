// Netify Agent
// Copyright (C) 2015-2018 eGloo Incorporated <http://www.egloo.ca>
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

#ifndef _ND_JSON_H
#define _ND_JSON_H

#define ND_JSON_TOKENER_DEPTH   16

class ndJsonInitException : public runtime_error
{
public:
    explicit ndJsonInitException(const string &what_arg)
        : runtime_error(what_arg) { }
};

class ndJsonParseException : public runtime_error
{
public:
    explicit ndJsonParseException(const string &what_arg)
        : runtime_error(what_arg) { }
};

class ndJson
{
public:
    ndJson();
    ndJson(json_object *root);
    void Destroy(void);

    static json_object *CreateObject(void);
    json_object *CreateObject(json_object *parent, const string &name);
    json_object *CreateArray(json_object *parent, const string &name);

    void AddObject(json_object *parent, const string &name, json_object *object);
    void AddObject(json_object *parent, const string &name, const char *value);
    void AddObject(json_object *parent, const string &name, const string &value);
    void AddObject(json_object *parent, const string &name, int32_t value);
    void AddObject(json_object *parent, const string &name, int64_t value);
    void AddObject(json_object *parent, const string &name, uint32_t value);
    void AddObject(json_object *parent, const string &name, uint64_t value);
    void AddObject(json_object *parent, const string &name, double value);
    void AddObject(json_object *parent, const string &name, bool value);

    void PushObject(json_object *parent, const char *value);
    void PushObject(json_object *parent, const string &value);
    void PushObject(json_object *parent, int32_t value);
    void PushObject(json_object *parent, int64_t value);
    void PushObject(json_object *parent, double value);
    void PushObject(json_object *parent, bool value);
    void PushObject(json_object *parent, json_object *object);

    void ToString(string &output, bool pretty = true);

    void SaveToFile(const string &filename);

    json_object *GetRoot(void) { return root; }

protected:
    json_object *root;
};

#include "nd-json-response-code.h"

typedef vector<string> ndJsonDataChunks;
typedef map<string, ndJsonDataChunks> ndJsonData;

#ifdef _ND_USE_PLUGINS
typedef map<string, string> ndJsonPluginParams;
typedef ndJsonPluginParams ndJsonPluginReplies;

typedef map<string, string> ndJsonPluginRequest;
typedef map<string, ndJsonPluginParams> ndJsonPluginDispatch;
#endif

class ndJsonResponse
{
public:
    ndJsonResponse()
    {
        jtok = json_tokener_new_ex(ND_JSON_TOKENER_DEPTH);
        if (jtok == NULL)
            throw ndJsonInitException(strerror(ENOMEM));
    }

    virtual ~ndJsonResponse()
    {
        if (jtok != NULL) json_tokener_free(jtok);
    }

    void Parse(const string &json);

    double version;

    ndJsonResponseCode resp_code;
    string resp_message;

    string uuid_site;

    ndJsonData data;

#ifdef _ND_USE_PLUGINS
    ndJsonPluginRequest plugin_request_service_param;
    ndJsonPluginRequest plugin_request_task_exec;

    ndJsonPluginDispatch plugin_params;
#endif

protected:
    void UnserializeData(json_object *jdata);
#ifdef _ND_USE_PLUGINS
    void UnserializePluginRequest(json_object *jrequest, ndJsonPluginRequest &plugin_request);
    void UnserializePluginDispatch(json_object *jdispatch);
#endif

    json_tokener *jtok;
};

#endif // _ND_JSON_H
// vi: expandtab shiftwidth=4 softtabstop=4 tabstop=4
