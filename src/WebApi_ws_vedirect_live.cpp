// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2022 Thomas Basler and others
 */
#include "WebApi_ws_vedirect_live.h"
#include "AsyncJson.h"
#include "Configuration.h"
#include "MessageOutput.h"
#include "WebApi.h"
#include "defaults.h"

WebApiWsVedirectLiveClass::WebApiWsVedirectLiveClass()
    : _ws("/vedirectlivedata")
{
}

void WebApiWsVedirectLiveClass::init(AsyncWebServer* server)
{
    using std::placeholders::_1;
    using std::placeholders::_2;
    using std::placeholders::_3;
    using std::placeholders::_4;
    using std::placeholders::_5;
    using std::placeholders::_6;

    _server = server;
    _server->on("/api/vedirectlivedata/status", HTTP_GET, std::bind(&WebApiWsVedirectLiveClass::onLivedataStatus, this, _1));

    _server->addHandler(&_ws);
    _ws.onEvent(std::bind(&WebApiWsVedirectLiveClass::onWebsocketEvent, this, _1, _2, _3, _4, _5, _6));
}

void WebApiWsVedirectLiveClass::loop()
{
    // see: https://github.com/me-no-dev/ESPAsyncWebServer#limiting-the-number-of-web-socket-clients
    if (millis() - _lastWsCleanup > 1000) {
        _ws.cleanupClients();
        _lastWsCleanup = millis();
    }

    // do nothing if no WS client is connected
    if (_ws.count() == 0) {
        return;
    }

    if (millis() - _lastVedirectUpdateCheck < 1000) {
        return;
    }
    _lastVedirectUpdateCheck = millis();

    uint32_t maxTimeStamp = 0;
    if (VeDirect.getLastUpdate() > maxTimeStamp) {
        maxTimeStamp = VeDirect.getLastUpdate();
    }

    // Update on ve.direct change or at least after 10 seconds
    if (millis() - _lastWsPublish > (10 * 1000) || (maxTimeStamp != _newestVedirectTimestamp)) {

        DynamicJsonDocument root(1024);
        JsonVariant var = root;
        generateJsonResponse(var);

        String buffer;
        if (buffer) {
            serializeJson(root, buffer);
            
            if (Configuration.get().Security_AllowReadonly) {
                _ws.setAuthentication("", "");
            } else {
                _ws.setAuthentication(AUTH_USERNAME, Configuration.get().Security_Password);
            }

            _ws.textAll(buffer);
        }

        _lastWsPublish = millis();
    }
}

void WebApiWsVedirectLiveClass::generateJsonResponse(JsonVariant& root)
{
    // device info
    root[F("data_age")] = (millis() - VeDirect.getLastUpdate() ) / 1000;
    root[F("age_critical")] = ((millis() - VeDirect.getLastUpdate()) / 1000) > Configuration.get().Vedirect_PollInterval * 5;
    root[F("PID")] = VeDirect.getPidAsString(VeDirect.veMap["PID"].c_str());
    root[F("SER")] = VeDirect.veMap["SER"];
    root[F("FW")] = VeDirect.veMap["FW"];
    root[F("LOAD")] = VeDirect.veMap["LOAD"];
    root[F("CS")] = VeDirect.getCsAsString(VeDirect.veMap["CS"].c_str());
    root[F("ERR")] = VeDirect.getErrAsString(VeDirect.veMap["ERR"].c_str());
    root[F("OR")] = VeDirect.getOrAsString(VeDirect.veMap["OR"].c_str());
    root[F("MPPT")] = VeDirect.getMpptAsString(VeDirect.veMap["MPPT"].c_str());
    root[F("HSDS")]["v"] = VeDirect.veMap["HSDS"].toInt();
    root[F("HSDS")]["u"] = "Days";

    // battery info    
    root[F("V")]["v"] = round(VeDirect.veMap["V"].toDouble() / 10.0) / 100.0;
    root[F("V")]["u"] = "V";
    root[F("I")]["v"] = round(VeDirect.veMap["I"].toDouble() / 10.0) / 100.0;
    root[F("I")]["u"] = "A";

    // panel info
    root[F("VPV")]["v"] = round(VeDirect.veMap["VPV"].toDouble() / 10.0) / 100.0;
    root[F("VPV")]["u"] = "V";
    root[F("PPV")]["v"] = VeDirect.veMap["PPV"].toInt();
    root[F("PPV")]["u"] = "W";
    root[F("H19")]["v"] = VeDirect.veMap["H19"].toDouble() / 100.0;
    root[F("H19")]["u"] = "kWh";
    root[F("H20")]["v"] = VeDirect.veMap["H20"].toDouble() / 100.0;
    root[F("H20")]["u"] = "kWh";
    root[F("H21")]["v"] = VeDirect.veMap["H21"].toInt();
    root[F("H21")]["u"] = "W";
    root[F("H22")]["v"] = VeDirect.veMap["H22"].toDouble() / 100.0;
    root[F("H22")]["u"] = "kWh";
    root[F("H23")]["v"] = VeDirect.veMap["H23"].toInt();
    root[F("H23")]["u"] = "W";

    if (VeDirect.getLastUpdate() > _newestVedirectTimestamp) {
        _newestVedirectTimestamp = VeDirect.getLastUpdate();
    }
}

void WebApiWsVedirectLiveClass::onWebsocketEvent(AsyncWebSocket* server, AsyncWebSocketClient* client, AwsEventType type, void* arg, uint8_t* data, size_t len)
{
    if (type == WS_EVT_CONNECT) {
        char str[64];
        snprintf(str, sizeof(str), "Websocket: [%s][%u] connect", server->url(), client->id());
        Serial.println(str);
        MessageOutput.println(str);
    } else if (type == WS_EVT_DISCONNECT) {
        char str[64];
        snprintf(str, sizeof(str), "Websocket: [%s][%u] disconnect", server->url(), client->id());
        Serial.println(str);
        MessageOutput.println(str);
    }
}

void WebApiWsVedirectLiveClass::onLivedataStatus(AsyncWebServerRequest* request)
{
    if (!WebApi.checkCredentialsReadonly(request)) {
        return;
    }
    AsyncJsonResponse* response = new AsyncJsonResponse(false, 1024U);
    JsonVariant root = response->getRoot().as<JsonVariant>();
    generateJsonResponse(root);

    response->setLength();
    request->send(response);
}