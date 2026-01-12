#pragma once
#include "include/config_ble.h"
#include "core/SettingsService.h"
#include "include/bluetooth_protocol.h"
#include "drivers/LcdDriver.h"
#include "drivers/BatteryMonitor.h"
#include "drivers/RtcDs3231Driver.h"
#include "core/LogbookService.h"

#if BLE_FEATURE_ENABLED
#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <string.h>
#include <ArduinoJson.h>
#include <ctype.h>
#include <Update.h>
#include "mbedtls/sha256.h"
#include "mbedtls/base64.h"
#include <memory>
#endif

// Gestor BLE básico con compile-time on/off. Si BLE_FEATURE_ENABLED==0,
// todas las funciones son no-op para la versión básica.
class BleManager {
public:
#if BLE_FEATURE_ENABLED
    BleManager() = default;

    void begin(const Settings& settings) {
        enabled = settings.bleEnabled;
        pin = settings.blePin;
        name = settings.bleName;

        if (enabled) {
            if (!initStack(settings)) {
                enabled = false;
                return;
            }
            startAdvertising(settings);
            updateStatus();
        }
    }

    void setContext(Settings* settingsPtr,
                    SettingsService* settingsSvcPtr,
                    LcdDriver* lcdPtr,
                    BatteryMonitor* battPtr,
                    RtcDs3231Driver* rtcPtr,
                    LogbookService* logPtr) {
        settings      = settingsPtr;
        settingsSvc   = settingsSvcPtr;
        lcd           = lcdPtr;
        battery       = battPtr;
        rtc           = rtcPtr;
        logbook       = logPtr;
    }

    void setEnabled(bool on) {
        if (on == enabled) return;
        enabled = on;
        if (enabled) {
            if (!initialized) {
                // Inicializa con el PIN almacenado.
                initStackWithPin();
            }
            if (initialized) {
                startAdvertising();
                updateStatus();
            }
        } else {
            connected = false;
            authed = false;
            stopAdvertising();
            deinitStack();
        }
    }

    bool isEnabled() const { return enabled; }
    bool isConnected() const { return connected; }
    bool isAuthed() const { return authed; }
    bool isBusy() const { return busy; }

private:
    struct ServerCallbacks : public BLEServerCallbacks {
        explicit ServerCallbacks(BleManager* mgr) : mgr(mgr) {}
        void onConnect(BLEServer*) override {
            if (mgr) {
                mgr->connected = true;
                mgr->authed = false;
                mgr->updateStatus();
            }
        }
        void onDisconnect(BLEServer*) override {
            if (mgr) {
                mgr->connected = false;
                mgr->authed = false;
                mgr->cancelOta();
                mgr->busy = false;
                mgr->updateStatus();
                if (mgr->enabled) {
                    mgr->startAdvertising();
                } else {
                    mgr->stopAdvertising();
                }
            }
        }
        BleManager* mgr;
    };

    struct ControlCallbacks : public BLECharacteristicCallbacks {
        explicit ControlCallbacks(BleManager* mgr) : mgr(mgr) {}
        void onWrite(BLECharacteristic* ch) override {
            if (!mgr) return;
            std::string v = ch->getValue().c_str();
            mgr->handleControl(v);
        }
        BleManager* mgr;
    };

    void startAdvertising(const Settings& settings) {
        if (!initialized) return;
        BLEAdvertising* adv = BLEDevice::getAdvertising();
        adv->stop();
        adv->setScanResponse(true);
        adv->addServiceUUID(BtProtocol::kServiceMainUuid);
        adv->setMinPreferred(0x06); // intervalos largos
        adv->setMinPreferred(0x12);
        BLEDevice::startAdvertising();
    }

    void startAdvertising() {
        if (!initialized) return;
        BLEAdvertising* adv = BLEDevice::getAdvertising();
        adv->stop();
        adv->setScanResponse(true);
        adv->addServiceUUID(BtProtocol::kServiceMainUuid);
        adv->setMinPreferred(0x06); // intervalos largos
        adv->setMinPreferred(0x12);
        BLEDevice::startAdvertising();
    }

    void stopAdvertising() {
        if (!initialized) return;
        BLEAdvertising* adv = BLEDevice::getAdvertising();
        if (adv) adv->stop();
        if (server) server->disconnect(0);
    }

    bool initStack(const Settings& settings) {
        if (initialized) return true;
        BLEDevice::init(deviceName(settings));
        server = BLEDevice::createServer();
        server->setCallbacks(new ServerCallbacks(this));

        BLEService* svc = server->createService(BtProtocol::kServiceMainUuid);

        controlChar  = svc->createCharacteristic(BtProtocol::kCharControlUuid,
                                                 BLECharacteristic::PROPERTY_READ |
                                                 BLECharacteristic::PROPERTY_WRITE |
                                                 BLECharacteristic::PROPERTY_NOTIFY);
        statusChar   = svc->createCharacteristic(BtProtocol::kCharStatusUuid,
                                                 BLECharacteristic::PROPERTY_READ  |
                                                 BLECharacteristic::PROPERTY_NOTIFY);
        settingsChar = svc->createCharacteristic(BtProtocol::kCharSettingsUuid,
                                                 BLECharacteristic::PROPERTY_READ  |
                                                 BLECharacteristic::PROPERTY_WRITE);
        logChar      = svc->createCharacteristic(BtProtocol::kCharLogbookUuid,
                                                 BLECharacteristic::PROPERTY_READ  |
                                                 BLECharacteristic::PROPERTY_NOTIFY);
        otaChar      = svc->createCharacteristic(BtProtocol::kCharOtaUuid,
                                                 BLECharacteristic::PROPERTY_WRITE |
                                                 BLECharacteristic::PROPERTY_NOTIFY);

        controlChar->setCallbacks(new ControlCallbacks(this));

        svc->start();
        initialized = true;
        return true;
    }

    bool initStackWithPin() {
        if (initialized) return true;
        Settings tmp;
        memset(&tmp, 0, sizeof(tmp));
        pin.copy(tmp.blePin, sizeof(tmp.blePin) - 1);
        tmp.blePin[sizeof(tmp.blePin) - 1] = '\0';
        name.copy(tmp.bleName, sizeof(tmp.bleName) - 1);
        tmp.bleName[sizeof(tmp.bleName) - 1] = '\0';
        return initStack(tmp);
    }

    void deinitStack() {
        if (!initialized) return;
        BLEDevice::deinit(true);
        initialized = false;
    }

    void updateStatus() {
        if (!statusChar) return;
        char buf[64];
        snprintf(buf, sizeof(buf), "VER:%s,CON:%d,AUTH:%d",
                 BtProtocol::kVersion,
                 connected ? 1 : 0,
                 authed ? 1 : 0);
        statusChar->setValue((uint8_t*)buf, strlen(buf));
        statusChar->notify();
    }

    void notifyStatusJson() {
        if (!statusChar) return;
        StaticJsonDocument<128> doc;
        doc["type"] = "status";
        doc["fw"]   = BtProtocol::kVersion;
        doc["con"]  = connected;
        doc["auth"] = authed;
        doc["busy"] = busy;
        if (battery) {
            doc["bat"] = battery->getBatteryPercent();
            doc["chg"] = battery->isChargerConnected();
        }
        char out[192];
        size_t n = serializeJson(doc, out, sizeof(out));
        statusChar->setValue((uint8_t*)out, n);
        statusChar->notify();
    }

    void handleControl(const std::string& payload) {
        StaticJsonDocument<512> doc;
        DeserializationError err = deserializeJson(doc, payload);
        if (err) {
            sendControlResp("{\"type\":\"err\",\"msg\":\"bad_json\"}");
            return;
        }
        const char* type = doc["type"] | "";
        if (strcmp(type, "ping") == 0) {
            sendControlResp(payload);
            return;
        }
        if (strcmp(type, "auth") == 0) {
            const char* pinIn = doc["pin"] | "";
            authed = (pinIn && !pin.empty() && strcmp(pinIn, pin.c_str()) == 0);
            notifyStatusJson();
            sendControlResp(authed ? "{\"type\":\"auth\",\"ok\":true}" : "{\"type\":\"auth\",\"ok\":false}");
            return;
        }
        if (!authed) {
            sendControlResp("{\"type\":\"err\",\"msg\":\"auth_required\"}");
            return;
        }
        if (strcmp(type, "set_time") == 0 && rtc) {
            uint32_t epoch = doc["epoch"] | 0;
            UtcDateTime dt{};
            epochToUtc(epoch, dt);
            rtc->setUtc(dt);
            sendControlResp("{\"type\":\"set_time\",\"ok\":true}");
            return;
        }
        if (strcmp(type, "get_status") == 0) {
            notifyStatusJson();
            return;
        }
        if (strcmp(type, "get_settings") == 0) {
            sendSettings();
            return;
        }
        if (strcmp(type, "set_settings") == 0) {
            applySettings(doc["settings"]);
            return;
        }
        if (strcmp(type, "list_logs") == 0) {
            sendLogStats();
            return;
        }
        if (strcmp(type, "get_log") == 0) {
            int idx = doc["index"] | -1;
            streamLogs(idx);
            return;
        }
        if (strcmp(type, "ota_begin") == 0) {
            handleOtaBegin(doc);
            return;
        }
        if (strcmp(type, "ota_data") == 0) {
            handleOtaData(doc);
            return;
        }
        if (strcmp(type, "ota_end") == 0) {
            handleOtaEnd(doc);
            return;
        }
        if (strcmp(type, "ota_begin") == 0 ||
            strcmp(type, "ota_data") == 0 ||
            strcmp(type, "ota_end") == 0) {
            sendControlResp("{\"type\":\"err\",\"msg\":\"ota_not_impl\"}");
            return;
        }
        sendControlResp("{\"type\":\"err\",\"msg\":\"unknown\"}");
    }

    void sendControlResp(const std::string& msg) {
        if (!controlChar) return;
        controlChar->setValue((uint8_t*)msg.data(), msg.size());
        controlChar->notify();
    }

    // OTA helpers
    void handleOtaBegin(JsonVariant doc) {
        if (!authed) {
            sendControlResp("{\"type\":\"ota_begin\",\"ok\":false,\"err\":\"auth\"}");
            return;
        }
        uint32_t sz = doc["size"] | 0;
        const char* hash = doc["sha256"] | nullptr;
        if (sz == 0 || !hash || strlen(hash) != 64) {
            sendControlResp("{\"type\":\"ota_begin\",\"ok\":false,\"err\":\"args\"}");
            return;
        }
        if (otaInProgress) {
            sendControlResp("{\"type\":\"ota_begin\",\"ok\":false,\"err\":\"busy\"}");
            return;
        }
        if (!Update.begin(sz)) {
            sendControlResp("{\"type\":\"ota_begin\",\"ok\":false,\"err\":\"begin\"}");
            return;
        }
        otaInProgress = true;
        busy = true;
        otaExpectedSize = sz;
        otaWritten = 0;
        otaHashExpected = hash;
        mbedtls_sha256_init(&otaShaCtx);
        mbedtls_sha256_starts(&otaShaCtx, 0);
        sendControlResp("{\"type\":\"ota_begin\",\"ok\":true}");
    }

    void handleOtaData(JsonVariant doc) {
        if (!authed) {
            sendControlResp("{\"type\":\"ota_data\",\"ok\":false,\"err\":\"auth\"}");
            return;
        }
        if (!otaInProgress) {
            sendControlResp("{\"type\":\"ota_data\",\"ok\":false,\"err\":\"no_begin\"}");
            return;
        }
        uint32_t off = doc["off"] | 0;
        const char* dataB64 = doc["data"] | nullptr;
        if (!dataB64) {
            sendControlResp("{\"type\":\"ota_data\",\"ok\":false,\"err\":\"args\"}");
            return;
        }
        // Base64 decode
        size_t inLen = strlen(dataB64);
        // Worst case output = 3/4 of input
        size_t outLen = (inLen * 3) / 4 + 4;
        std::unique_ptr<uint8_t[]> buf(new uint8_t[outLen]);
        size_t decodedLen = 0;
        int r = mbedtls_base64_decode(buf.get(), outLen, &decodedLen,
                                      reinterpret_cast<const unsigned char*>(dataB64), inLen);
        if (r != 0 || decodedLen == 0) {
            sendControlResp("{\"type\":\"ota_data\",\"ok\":false,\"err\":\"b64\"}");
            return;
        }
        if (off != otaWritten) {
            sendControlResp("{\"type\":\"ota_data\",\"ok\":false,\"err\":\"offset\"}");
            return;
        }
        size_t wr = Update.write(buf.get(), decodedLen);
        if (wr != decodedLen) {
            sendControlResp("{\"type\":\"ota_data\",\"ok\":false,\"err\":\"write\"}");
            return;
        }
        otaWritten += decodedLen;
        mbedtls_sha256_update(&otaShaCtx, buf.get(), decodedLen);

        StaticJsonDocument<96> resp;
        resp["type"] = "ota_data";
        resp["ok"] = true;
        resp["written"] = otaWritten;
        char out[128];
        size_t n = serializeJson(resp, out, sizeof(out));
        sendControlResp(std::string(out, n));
    }

    void handleOtaEnd(JsonVariant doc) {
        if (!authed) {
            sendControlResp("{\"type\":\"ota_end\",\"ok\":false,\"err\":\"auth\"}");
            return;
        }
        if (!otaInProgress) {
            sendControlResp("{\"type\":\"ota_end\",\"ok\":false,\"err\":\"no_begin\"}");
            return;
        }
        // Hash final
        uint8_t digest[32];
        mbedtls_sha256_finish(&otaShaCtx, digest);
        char hex[65];
        for (int i = 0; i < 32; ++i) {
            sprintf(&hex[i*2], "%02x", digest[i]);
        }
        hex[64] = '\0';
        if (otaHashExpected != hex) {
            sendControlResp("{\"type\":\"ota_end\",\"ok\":false,\"err\":\"hash\"}");
            cancelOta();
            return;
        }
        if (!Update.end(true)) {
            sendControlResp("{\"type\":\"ota_end\",\"ok\":false,\"err\":\"end\"}");
            cancelOta();
            return;
        }
        otaInProgress = false;
        busy = false;
        sendControlResp("{\"type\":\"ota_end\",\"ok\":true}");
        delay(200);
        ESP.restart();
    }

    void cancelOta() {
        if (otaInProgress) {
            Update.abort();
        }
        otaInProgress = false;
        busy = false;
    }

    void sendSettings() {
        if (!settings || !settingsSvc) {
            sendControlResp("{\"type\":\"get_settings\",\"ok\":false}");
            return;
        }
        StaticJsonDocument<256> doc;
        doc["type"] = "get_settings";
        doc["unit"] = (settings->unidadMetros == UnitType::METERS) ? "m" : "ft";
        doc["lang"] = (settings->idioma == Language::ES) ? "es" : "en";
        doc["bright"] = settings->brilloPantalla;
        doc["sleep"] = settings->ahorroTimeoutOption;
        doc["invert"] = settings->inverPant;
        doc["hudMask"] = settings->hud.toMask();
        doc["hudClean"] = settings->hudMinimalFlight;
        doc["name"] = settings->bleName;
        char out[256];
        size_t n = serializeJson(doc, out, sizeof(out));
        sendControlResp(std::string(out, n));
    }

    void applySettings(JsonVariant settingsObj) {
        if (!settings || !settingsSvc) {
            sendControlResp("{\"type\":\"set_settings\",\"ok\":false}");
            return;
        }
        if (settingsObj.isNull()) {
            sendControlResp("{\"type\":\"set_settings\",\"ok\":false,\"err\":\"no_settings\"}");
            return;
        }
        Settings s = *settings;
        const char* unit = settingsObj["unit"] | nullptr;
        if (unit) {
            s.unidadMetros = (strcmp(unit, "ft") == 0) ? UnitType::FEET : UnitType::METERS;
        }
        const char* lang = settingsObj["lang"] | nullptr;
        if (lang) {
            s.idioma = (strcmp(lang, "en") == 0) ? Language::EN : Language::ES;
        }
        if (settingsObj.containsKey("bright")) {
            int v = settingsObj["bright"];
            if (v < 0 || v > 1) { sendControlResp("{\"type\":\"set_settings\",\"ok\":false,\"err\":\"bright\"}"); return; }
            s.brilloPantalla = (uint8_t)v;
        }
        if (settingsObj.containsKey("sleep")) {
            int v = settingsObj["sleep"];
            if (v < 0 || v > 3) { sendControlResp("{\"type\":\"set_settings\",\"ok\":false,\"err\":\"sleep\"}"); return; }
            s.ahorroTimeoutOption = (uint8_t)v;
        }
        if (settingsObj.containsKey("invert")) {
            s.inverPant = settingsObj["invert"];
        }
        if (settingsObj.containsKey("hudMask")) {
            int m = settingsObj["hudMask"];
            if (m < 0 || m > 0x3F) { sendControlResp("{\"type\":\"set_settings\",\"ok\":false,\"err\":\"hudMask\"}"); return; }
            uint8_t mask = (uint8_t)m;
            s.hud = HudConfig::fromMask(mask);
        }
        if (settingsObj.containsKey("hudClean")) {
            s.hudMinimalFlight = settingsObj["hudClean"];
        }
        if (settingsObj.containsKey("name")) {
            const char* nm = settingsObj["name"];
            if (!nm) { sendControlResp("{\"type\":\"set_settings\",\"ok\":false,\"err\":\"name\"}"); return; }
            size_t len = strlen(nm);
            if (len < 4 || len >= BLE_NAME_MAX_LEN) { sendControlResp("{\"type\":\"set_settings\",\"ok\":false,\"err\":\"name_len\"}"); return; }
            // validar caracteres simples
            for (size_t i = 0; i < len; ++i) {
                char c = nm[i];
                if (!(isalpha(c) || isdigit(c) || c=='-' || c=='_')) {
                    sendControlResp("{\"type\":\"set_settings\",\"ok\":false,\"err\":\"name_chars\"}");
                    return;
                }
            }
            strncpy(s.bleName, nm, sizeof(s.bleName));
            s.bleName[sizeof(s.bleName)-1] = '\0';
            name = s.bleName;
        }
        settingsSvc->save(s);
        *settings = s;
        if (lcd) {
            lcd->setRotation(settings->inverPant);
        }
        if (initialized && settingsObj.containsKey("name")) {
            // Reanunciar con el nuevo nombre
            deinitStack();
            initStack(s);
            startAdvertising(s);
        }
        sendControlResp("{\"type\":\"set_settings\",\"ok\":true}");
        notifyStatusJson();
    }

    void sendLogStats() {
        if (!logbook) {
            sendControlResp("{\"type\":\"list_logs\",\"ok\":false}");
            return;
        }
        LogbookService::Stats st{};
        if (!logbook->getStats(st)) {
            sendControlResp("{\"type\":\"list_logs\",\"ok\":false}");
            return;
        }
        StaticJsonDocument<128> doc;
        doc["type"] = "list_logs";
        doc["ok"] = true;
        doc["count"] = st.count;
        doc["totalIds"] = st.totalIds;
        char out[160];
        size_t n = serializeJson(doc, out, sizeof(out));
        sendControlResp(std::string(out, n));
    }

    void sendLogRecord(int idxNewestFirst) {
        if (!logbook || idxNewestFirst < 0) {
            sendControlResp("{\"type\":\"get_log\",\"ok\":false}");
            return;
        }
        LogbookService::Record rec{};
        if (!logbook->getByIndex((uint16_t)idxNewestFirst, rec)) {
            sendControlResp("{\"type\":\"get_log\",\"ok\":false}");
            return;
        }
        StaticJsonDocument<256> doc;
        doc["type"] = "get_log";
        doc["ok"] = true;
        doc["id"] = rec.id;
        doc["ts"] = rec.tsUtc;
        doc["exit"] = rec.exitAltM;
        doc["deploy"] = rec.deployAltM;
        doc["ff"] = rec.freefallTimeS;
        doc["vff"] = rec.vmaxFFmps;
        doc["vcan"] = rec.vmaxCanopymps;
        char out[300];
        size_t n = serializeJson(doc, out, sizeof(out));
        sendControlResp(std::string(out, n));
    }

    void streamLogs(int startIdx) {
        if (!authed) {
            sendControlResp("{\"type\":\"get_log\",\"ok\":false,\"err\":\"auth\"}");
            return;
        }
        if (busy) {
            sendControlResp("{\"type\":\"get_log\",\"ok\":false,\"err\":\"busy\"}");
            return;
        }
        if (!logbook || !controlChar) {
            sendControlResp("{\"type\":\"get_log\",\"ok\":false}");
            return;
        }
        LogbookService::Stats st{};
        if (!logbook->getStats(st) || st.count == 0) {
            sendControlResp("{\"type\":\"get_log\",\"ok\":false,\"err\":\"empty\"}");
            return;
        }
        int idx = (startIdx >= 0) ? startIdx : 0;
        if (idx >= (int)st.count) {
            sendControlResp("{\"type\":\"get_log\",\"ok\":false,\"err\":\"range\"}");
            return;
        }
        busy = true;
        for (int i = idx; i < (int)st.count; ++i) {
            LogbookService::Record rec{};
            if (!logbook->getByIndex((uint16_t)i, rec)) break;
            StaticJsonDocument<256> doc;
            doc["type"] = "log";
            doc["idx"] = i;
            doc["id"] = rec.id;
            doc["ts"] = rec.tsUtc;
            doc["exit"] = rec.exitAltM;
            doc["deploy"] = rec.deployAltM;
            doc["ff"] = rec.freefallTimeS;
            doc["vff"] = rec.vmaxFFmps;
            doc["vcan"] = rec.vmaxCanopymps;
            doc["eof"] = (i == (int)st.count - 1);
            char out[300];
            size_t n = serializeJson(doc, out, sizeof(out));
            controlChar->setValue((uint8_t*)out, n);
            controlChar->notify();
        }
        busy = false;
    }

    static void epochToUtc(uint32_t epoch, UtcDateTime& dt) {
        uint32_t t = epoch;
        dt.second = t % 60; t /= 60;
        dt.minute = t % 60; t /= 60;
        dt.hour   = t % 24; t /= 24;
        // Día/mes/año aproximado (no crítica para set_time simple)
        uint32_t days = t;
        uint16_t year = 1970;
        while (true) {
            bool leap = (year % 4 == 0 && (year % 100 != 0 || year % 400 == 0));
            uint16_t daysInYear = leap ? 366 : 365;
            if (days >= daysInYear) { days -= daysInYear; year++; }
            else break;
        }
        static const uint8_t dm[] = {31,28,31,30,31,30,31,31,30,31,30,31};
        uint8_t month = 0;
        while (month < 12) {
            uint8_t dim = dm[month] + ((month==1 && (year%4==0 && (year%100!=0 || year%400==0))) ? 1 : 0);
            if (days < dim) break;
            days -= dim;
            month++;
        }
        dt.year = year;
        dt.month = month + 1;
        dt.day = days + 1;
    }

    String deviceName(const Settings& settings) {
        if (strlen(settings.bleName) > 0) return String(settings.bleName);
        // Fallback a ALTI-XXXX desde PIN si no hay nombre
        const char* p = settings.blePin;
        char suffix[5] = { p[2], p[3], p[4], p[5], '\0' };
        String n = "ALTI-";
        n += suffix;
        return n;
    }
    String deviceNameFromPin() {
        if (!name.empty()) return String(name.c_str());
        char suffix[5] = { '0','0','0','0','\0' };
        if (pin.size() >= 6) {
            suffix[0] = pin[2];
            suffix[1] = pin[3];
            suffix[2] = pin[4];
            suffix[3] = pin[5];
        }
        String n = "ALTI-";
        n += suffix;
        return n;
    }

    bool enabled = false;
    bool connected = false;
    bool authed = false;
    bool initialized = false;
    bool busy = false;
    bool otaInProgress = false;
    size_t otaExpectedSize = 0;
    size_t otaWritten = 0;
    std::string otaHashExpected;
    mbedtls_sha256_context otaShaCtx;
    std::string pin;
    std::string name;
    Settings* settings = nullptr;
    SettingsService* settingsSvc = nullptr;
    LcdDriver* lcd = nullptr;
    BatteryMonitor* battery = nullptr;
    RtcDs3231Driver* rtc = nullptr;
    LogbookService* logbook = nullptr;
    BLEServer* server = nullptr;
    BLECharacteristic* controlChar = nullptr;
    BLECharacteristic* statusChar = nullptr;
    BLECharacteristic* settingsChar = nullptr;
    BLECharacteristic* logChar = nullptr;
    BLECharacteristic* otaChar = nullptr;
#else
    // Versión sin BLE: no-ops
    void begin(const Settings&) {}
    void setEnabled(bool) {}
    bool isEnabled() const { return false; }
    bool isConnected() const { return false; }
    bool isAuthed() const { return false; }
#endif
};
