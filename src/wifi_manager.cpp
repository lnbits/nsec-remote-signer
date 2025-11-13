#include "wifi_manager.h"
#include <WiFi.h>
#include "settings.h"
#include "app.h"

#include "ui.h"
#include "remote_signer.h"

// Import Nostr library components for key derivation
#include "../lib/nostr/nostr.h"

namespace WiFiManager {
    // Global WiFi state
    static WiFiUDP ntpUDP;
    static NTPClient timeClient(ntpUDP, "pool.ntp.org", 0, 60000);
    
    // Connection management
    static unsigned long wifi_connect_start_time = 0;
    static const unsigned long WIFI_CONNECT_TIMEOUT = 10000; // 10 seconds
    static bool wifi_connection_attempted = false;
    static char current_ssid[33];
    static char current_password[65];
    
    // Access Point mode
    static bool ap_mode_active = false;
    static WebServer ap_server(80);
    static DNSServer dns_server;
    static const char* ap_ssid = "Remote-Signer-Portal";
    static const char* ap_ip = "192.168.4.1";
    
    // Task and queue management
    static TaskHandle_t wifi_task_handle = NULL;
    static QueueHandle_t wifi_command_queue = NULL;
    static QueueHandle_t wifi_scan_result_queue = NULL;
    
    // UI elements
    static lv_obj_t* wifi_status_label = NULL;
    static lv_obj_t* main_wifi_status_label = NULL;
    static lv_timer_t* wifi_scan_timer = NULL;
    static lv_timer_t* wifi_status_timer = NULL;
    
    // SSID storage for UI
    static std::vector<String> wifi_ssids;
    
    // Status callback
    static wifi_status_callback_t status_callback = nullptr;
    
    // Background operations control
    static bool background_operations_paused = false;
    
    // Preferences instance
    static Preferences preferences;
    
    // WiFi task function - runs on Core 0
    static void wifiTask(void *parameter) {
        Serial.println("WiFi task started");
        while (true) {
            wifi_command_t command;
            if (xQueueReceive(wifi_command_queue, &command, portMAX_DELAY)) {
                Serial.print("WiFi task received command: ");
                Serial.println(command.type);
                
                switch (command.type) {
                    case WIFI_SCAN: {
                        Serial.println("Starting WiFi scan...");
                        
                        // Stop auto-reconnect and put WiFi in scan mode
                        WiFi.setAutoReconnect(false);
                        WiFi.disconnect(true);  // Disconnect and clear config
                        delay(1000);  // Wait for clean disconnect
                        
                        // Set WiFi to station mode for scanning
                        WiFi.mode(WIFI_STA);
                        delay(100);
                        
                        int n = WiFi.scanNetworks();
                        Serial.print("Scan completed, found ");
                        Serial.print(n);
                        Serial.println(" networks");
                        
                        wifi_scan_result_t result;
                        
                        // Check for scan errors (negative return values)
                        if (n < 0) {
                            Serial.print("WiFi scan failed with error code: ");
                            Serial.println(n);
                            Serial.println("Retrying scan in 1 second...");
                            delay(1000);
                            
                            // Retry scan once
                            n = WiFi.scanNetworks();
                            Serial.print("Retry scan found ");
                            Serial.print(n);
                            Serial.println(" networks");
                        }
                        
                        if (n < 0) {
                            Serial.println("Scan failed after retry, returning empty results");
                            result.network_count = 0;
                        } else {
                            result.network_count = (n > 9) ? 9 : n;
                            
                            for (int i = 0; i < result.network_count; i++) {
                                strncpy(result.ssids[i], WiFi.SSID(i).c_str(), 32);
                                result.ssids[i][32] = '\0';
                                result.rssi[i] = WiFi.RSSI(i);
                                result.encrypted[i] = (WiFi.encryptionType(i) != WIFI_AUTH_OPEN);
                            }
                        }
                        
                        // Re-enable auto-reconnect for normal operation
                        WiFi.setAutoReconnect(true);
                        
                        if (wifi_scan_result_queue != NULL) {
                            if (xQueueSend(wifi_scan_result_queue, &result, 0) == pdTRUE) {
                                Serial.println("Scan results sent to queue successfully");
                            } else {
                                Serial.println("Failed to send scan results to queue");
                            }
                        }
                        break;
                    }
                    case WIFI_CONNECT:
                        Serial.println("Connecting to WiFi...");
                        WiFi.begin(command.ssid, command.password);
                        break;
                    case WIFI_DISCONNECT:
                        Serial.println("Disconnecting from WiFi...");
                        WiFi.disconnect(true);
                        break;
                    case WIFI_STOP_SCAN:
                        Serial.println("Stopping WiFi scan...");
                        break;
                }
            }
            vTaskDelay(pdMS_TO_TICKS(100));
        }
    }
    
    // Status checker callback
    static void wifiStatusCheckerCB(lv_timer_t *timer) {
        lv_obj_t* status_label = (lv_obj_t*)timer->user_data;
        int attempts = (int)lv_obj_get_user_data(status_label);
        attempts++;

        if (WiFi.status() == WL_CONNECTED) {
            lv_label_set_text_fmt(status_label, "Connected!\nIP: %s", WiFi.localIP().toString().c_str());
            
            preferences.begin("wifi-creds", false);
            preferences.putString("ssid", current_ssid);
            preferences.putString("password", current_password);
            preferences.end();
            Serial.println("WiFi credentials saved.");

            lv_timer_del(timer);
            wifi_status_timer = NULL;
            return;
        }

        if (attempts > 30) { // 15-second timeout
            lv_label_set_text(status_label, "Connection Failed!");
            WiFi.disconnect(true);
            lv_timer_del(timer);
            wifi_status_timer = NULL;
            return;
        }

        lv_obj_set_user_data(status_label, (void*)attempts);
        delay(1);
    }
    
    // Main status updater callback
    static void mainStatusUpdaterCB(lv_timer_t *timer) {
        if (ap_mode_active) {
            if (main_wifi_status_label != NULL && lv_obj_is_valid(main_wifi_status_label)) {
                lv_label_set_text(main_wifi_status_label, "AP Mode Active");
                lv_obj_set_style_text_color(main_wifi_status_label, lv_color_hex(0x4CAF50), 0);
            }
            // Relay status will be handled by external modules
            return;
        }

        if (main_wifi_status_label != NULL && lv_obj_is_valid(main_wifi_status_label)) {
            if (WiFi.status() == WL_CONNECTED) {
                String status_text = String(LV_SYMBOL_WIFI) + " " + WiFi.SSID();
                lv_label_set_text(main_wifi_status_label, status_text.c_str());
                lv_obj_set_style_text_color(main_wifi_status_label, lv_color_hex(0x00FF00), 0);
            } else {
                unsigned long current_time = millis();
                if (wifi_connection_attempted && (current_time - wifi_connect_start_time > WIFI_CONNECT_TIMEOUT)) {
                    lv_label_set_text(main_wifi_status_label, LV_SYMBOL_WIFI " Timeout");
                    lv_obj_set_style_text_color(main_wifi_status_label, lv_color_hex(0xFF5722), 0);
                } else {
                    lv_label_set_text(main_wifi_status_label, LV_SYMBOL_WIFI " Not Connected");
                    lv_obj_set_style_text_color(main_wifi_status_label, lv_color_hex(0x9E9E9E), 0);
                }
            }
        }
        
        if (status_callback) {
            status_callback(WiFi.status() == WL_CONNECTED, 
                           WiFi.status() == WL_CONNECTED ? "Connected" : "Disconnected");
        }
        
        delay(1);
    }
    
    void init() {
        WiFi.mode(WIFI_STA);
        timeClient.begin();
        timeClient.setTimeOffset(0);
        
        // Create queues
        wifi_command_queue = xQueueCreate(10, sizeof(wifi_command_t));
        wifi_scan_result_queue = xQueueCreate(5, sizeof(wifi_scan_result_t));
        
        createTask();
        createStatusTimer();
        
        // Load Bunker URL
        loadBunkerUrl();
        
        // Try to connect to saved WiFi if not in AP mode and background operations aren't paused
        if (!ap_mode_active && !isBackgroundOperationsPaused()) {
            preferences.begin("wifi-creds", true);
            String saved_ssid = preferences.getString("ssid", "");
            String saved_pass = preferences.getString("password", "");
            preferences.end();

            if (saved_ssid.length() > 0) {
                Serial.println("Found saved WiFi credentials.");
                Serial.print("Connecting to ");
                Serial.println(saved_ssid);
                startConnection(saved_ssid.c_str(), saved_pass.c_str());
            }
        } else if (isBackgroundOperationsPaused()) {
            Serial.println("Background operations paused - skipping auto WiFi connection");
        }
    }
    
    void cleanup() {
        deleteTask();
        deleteStatusTimer();
        
        if (wifi_command_queue) {
            vQueueDelete(wifi_command_queue);
            wifi_command_queue = NULL;
        }
        
        if (wifi_scan_result_queue) {
            vQueueDelete(wifi_scan_result_queue);
            wifi_scan_result_queue = NULL;
        }
    }
    
    void processLoop() {
        if (isAPModeActive()) {
            dns_server.processNextRequest();
            ap_server.handleClient();
        }
    }
    
    void startConnection(const char* ssid, const char* password) {
        strncpy(current_ssid, ssid, sizeof(current_ssid) - 1);
        current_ssid[sizeof(current_ssid) - 1] = '\0';
        strncpy(current_password, password, sizeof(current_password) - 1);
        current_password[sizeof(current_password) - 1] = '\0';
        
        if (wifi_command_queue != NULL) {
            wifi_command_t command;
            command.type = WIFI_CONNECT;
            strncpy(command.ssid, ssid, sizeof(command.ssid) - 1);
            command.ssid[sizeof(command.ssid) - 1] = '\0';
            strncpy(command.password, password, sizeof(command.password) - 1);
            command.password[sizeof(command.password) - 1] = '\0';
            xQueueSend(wifi_command_queue, &command, 0);
        }
        
        wifi_connect_start_time = millis();
        wifi_connection_attempted = true;
    }
    
    void disconnect() {
        if (wifi_command_queue != NULL) {
            wifi_command_t command;
            command.type = WIFI_DISCONNECT;
            xQueueSend(wifi_command_queue, &command, 0);
        }
    }
    
    bool isConnected() {
        return WiFi.status() == WL_CONNECTED;
    }
    
    String getSSID() {
        return WiFi.SSID();
    }
    
    String getLocalIP() {
        return WiFi.localIP().toString();
    }
    
    wl_status_t getStatus() {
        return WiFi.status();
    }
    
    void startScan() {
        Serial.println("Scanning for WiFi networks...");
        if (UI::getWiFiList()) {
            lv_obj_clean(UI::getWiFiList());
            lv_obj_t* scanning_text = lv_list_add_text(UI::getWiFiList(), "Scanning for networks...");
            lv_obj_set_style_bg_opa(scanning_text, LV_OPA_TRANSP, LV_PART_MAIN);
            lv_obj_set_style_text_color(scanning_text, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
            lv_obj_set_style_pad_all(scanning_text, 5, LV_PART_MAIN);
        }
        
        if (wifi_command_queue != NULL) {
            wifi_command_t command;
            command.type = WIFI_SCAN;
            if (xQueueSend(wifi_command_queue, &command, 0) == pdTRUE) {
                Serial.println("Scan command sent to WiFi task successfully");
            } else {
                Serial.println("Failed to send scan command to WiFi task");
            }
        }
        
        wifi_scan_timer = lv_timer_create([](lv_timer_t *timer) {
            if (processScanResults()) {
                lv_timer_del(timer);
                wifi_scan_timer = NULL;
            }
        }, 500, NULL);
    }
    
    bool processScanResults() {
        if (wifi_scan_result_queue == NULL) {
            return false;
        }
        
        wifi_scan_result_t result;
        if (xQueueReceive(wifi_scan_result_queue, &result, 0)) {
            Serial.print("Found ");
            Serial.print(result.network_count);
            Serial.println(" networks.");
            
            if (UI::getWiFiList()) {
                lv_obj_clean(UI::getWiFiList());
                
                if (result.network_count == 0) {
                    lv_list_add_text(UI::getWiFiList(), "No networks found");
                } else {
                    wifi_ssids.clear();
                    wifi_ssids.reserve(result.network_count);
                    
                    for (int i = 0; i < result.network_count; i++) {
                        String ssid = String(result.ssids[i]);
                        wifi_ssids.push_back(ssid);
                        
                        String rssi = String(result.rssi[i]);
                        String security = result.encrypted[i] ? "Lck" : " ";
                        String item_text = ssid + " (" + rssi + " dBm) " + security;
                        
                        Serial.print("Adding network: ");
                        Serial.println(item_text);
                        
                        lv_obj_t* list_btn = lv_list_add_btn(UI::getWiFiList(), NULL, item_text.c_str());
                        lv_obj_add_event_cb(list_btn, connectEventHandler, LV_EVENT_CLICKED, (void*)(uintptr_t)i);
                        
                        // Style the list button - transparent background with white text
                        lv_obj_set_style_bg_opa(list_btn, LV_OPA_TRANSP, LV_PART_MAIN);
                        lv_obj_set_style_text_color(list_btn, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
                        lv_obj_set_style_border_width(list_btn, 0, LV_PART_MAIN);
                        lv_obj_set_style_outline_width(list_btn, 0, LV_PART_MAIN);
                        
                        if (i % 3 == 0) {
                            delay(1);
                        }
                    }
                }
            }
            return true;
        }
        return false;
    }
      
    void startAPMode() {
        if (ap_mode_active) {
            Serial.println("AP mode already active");
            return;
        }
        
        Serial.println("Starting Access Point mode...");
        
        if (RemoteSigner::isInitialized()) {
            Serial.println("Disconnected from relay and disabled reconnection");
        }
        
        WiFi.disconnect(true);
        delay(1000);
        
        WiFi.mode(WIFI_AP);
        
        IPAddress local_IP;
        local_IP.fromString(ap_ip);
        IPAddress gateway(192, 168, 4, 1);
        IPAddress subnet(255, 255, 255, 0);
        
        bool ap_started = WiFi.softAP(ap_ssid, Settings::getAPPassword().c_str());
        if (!ap_started) {
            Serial.println("Failed to start AP");
            return;
        }
        
        WiFi.softAPConfig(local_IP, gateway, subnet);
        dns_server.start(53, "*", local_IP);
        
        ap_server.on("/", HTTP_GET, handleAPRoot);
        ap_server.on("/config", HTTP_POST, handleAPConfig);
        ap_server.on("/current-config", HTTP_GET, handleCurrentConfig);
        ap_server.onNotFound([]() {
            ap_server.sendHeader("Location", "http://" + String(ap_ip), true);
            ap_server.send(302, "text/plain", "");
        });
        
        ap_server.begin();
        ap_mode_active = true;
        
        Serial.println("Access Point started successfully");
        updateSettingsScreenForAPMode();
        
        UI::showMessage("Bunker Pairing Code", "Connect to the WiFi hotspot below to set your Nostr key and prefered relay.\nSSID: " + String(ap_ssid) + "\nPassword: " + Settings::getAPPassword() + "\nIP: " + String(ap_ip));
    }
    
    void stopAPMode() {
        if (!ap_mode_active) {
            return;
        }
        
        ap_server.close();
        dns_server.stop();
        WiFi.softAPdisconnect(true);
        WiFi.mode(WIFI_STA);
        ap_mode_active = false;
        
        Serial.println("Access Point stopped");
        
        // Try to reconnect to saved WiFi
        preferences.begin("wifi-creds", true);
        String saved_ssid = preferences.getString("ssid", "");
        String saved_pass = preferences.getString("password", "");
        preferences.end();

        if (saved_ssid.length() > 0) {
            Serial.println("Attempting to reconnect to saved WiFi: " + saved_ssid);
            startConnection(saved_ssid.c_str(), saved_pass.c_str());
        }
    }
    
    bool isAPModeActive() {
        return ap_mode_active;
    }
    
    String getAPSSID() {
        return String(ap_ssid);
    }
    
    String getAPPassword() {
        return Settings::getAPPassword();
    }
    
    String getAPIP() {
        return String(ap_ip);
    }
    
    void loadBunkerUrl() {
        preferences.begin("config", true);
        String saved_url = preferences.getString("bunker_url", "");
        preferences.end();
        
        if (saved_url.length() > 0) {
            // Signer configuration loaded separately
            Serial.println("Signer config will be loaded from preferences");
            Serial.println("Loaded Bunker URL from preferences: " + saved_url);
        } else {
            Serial.println("No saved Bunker URL found, using default");
        }
    }
    
    String getBunkerUrl() {
        return RemoteSigner::getBunkerUrl();
    }
    
    void setStatusLabel(lv_obj_t* label) {
        wifi_status_label = label;
    }
    
    void setMainStatusLabel(lv_obj_t* label) {
        main_wifi_status_label = label;
    }
    
    // Event handlers
    void scanEventHandler(lv_event_t* e) {
        if (e != NULL) {
            lv_event_code_t code = lv_event_get_code(e);
            if (code != LV_EVENT_CLICKED) return;
        }
        startScan();
    }
    
    void connectEventHandler(lv_event_t* e) {
        lv_event_code_t code = lv_event_get_code(e);
        if (code == LV_EVENT_CLICKED) {
            // Reset activity timer on WiFi network selection
            App::resetActivityTimer();
            
            int index = (int)(uintptr_t)lv_event_get_user_data(e);
            
            if (index < wifi_ssids.size()) {
                const char* ssid = wifi_ssids[index].c_str();
                Serial.print("Selected WiFi network: ");
                Serial.println(ssid);
                UI::createWiFiPasswordScreen(ssid);
            } else {
                Serial.println("Invalid WiFi network index");
            }
        }
    }
    
    void passwordKBEventHandler(lv_event_t* e) {
        lv_event_code_t code = lv_event_get_code(e);
        lv_obj_t* kb = lv_event_get_target(e);
        
        if (code == LV_EVENT_READY) {
            lv_obj_t* ta = lv_keyboard_get_textarea(kb);
            const char* password = lv_textarea_get_text(ta);
            strncpy(current_password, password, sizeof(current_password) - 1);
            current_password[sizeof(current_password) - 1] = '\0';
            
            Serial.print("Attempting to connect to ");
            Serial.print(current_ssid);
            Serial.print(" with password: ");
            Serial.println(password);

            lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(ta, LV_OBJ_FLAG_HIDDEN);

            if (wifi_status_label) {
                lv_obj_clear_flag(wifi_status_label, LV_OBJ_FLAG_HIDDEN);
                lv_label_set_text(wifi_status_label, "Connecting...");
                lv_obj_align(wifi_status_label, LV_ALIGN_CENTER, 0, 0);
                lv_obj_set_user_data(wifi_status_label, (void*)0);
            }

            startConnection(current_ssid, current_password);

            if (wifi_status_label) {
                wifi_status_timer = lv_timer_create(wifiStatusCheckerCB, 500, wifi_status_label);
            }
        } else if (code == LV_EVENT_CANCEL) {
            pauseBackgroundOperations(false);
            UI::loadScreen((UI::screen_state_t)2); // SCREEN_WIFI
        }
    }
    
    void passwordBackEventHandler(lv_event_t* e) {
        pauseBackgroundOperations(false);
        UI::loadScreen((UI::screen_state_t)2); // SCREEN_WIFI
    }
    
    void launchAPModeEventHandler(lv_event_t* e) {
        lv_event_code_t code = lv_event_get_code(e);
        if (code == LV_EVENT_CLICKED) {
            Settings::showPinVerificationScreen();
        }
    }
    
    void exitAPModeEventHandler(lv_event_t* e) {
        lv_event_code_t code = lv_event_get_code(e);
        if (code == LV_EVENT_CLICKED) {
            Serial.println("Exiting Access Point mode");
            stopAPMode();
            UI::loadScreen((UI::screen_state_t)1); // SCREEN_SETTINGS
        }
    }
    
    void createTask() {
        xTaskCreatePinnedToCore(
            wifiTask,
            "WiFiTask",
            4096,
            NULL,
            1,
            &wifi_task_handle,
            0
        );
    }
    
    void deleteTask() {
        if (wifi_task_handle != NULL) {
            vTaskDelete(wifi_task_handle);
            wifi_task_handle = NULL;
        }
    }
    
    void setCurrentCredentials(const char* ssid, const char* password) {
        strncpy(current_ssid, ssid, sizeof(current_ssid) - 1);
        current_ssid[sizeof(current_ssid) - 1] = '\0';
        strncpy(current_password, password, sizeof(current_password) - 1);
        current_password[sizeof(current_password) - 1] = '\0';
    }
    
    void setStatusCallback(wifi_status_callback_t callback) {
        status_callback = callback;
    }
    
    void createStatusTimer() {
        lv_timer_create(mainStatusUpdaterCB, 1000, NULL);
    }
    
    void deleteStatusTimer() {
        // Timers are automatically cleaned up when objects are deleted
    }
    
    void handleAPRoot() {
        String html = R"(
<!DOCTYPE html>
<html>
<head>
    <title>Nostr Remote Signer Configuration</title>
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <style>
        body { font-family: Arial, sans-serif; margin: 20px; background-color: #f5f5f5; }
        .container { max-width: 600px; margin: 0 auto; background: white; padding: 20px; border-radius: 8px; box-shadow: 0 2px 10px rgba(0,0,0,0.1); }
        .form-group { margin-bottom: 20px; }
        label { display: block; margin-bottom: 5px; font-weight: bold; color: #333; }
        input[type="text"], input[type="password"], textarea { width: 100%; padding: 10px; border: 1px solid #ddd; border-radius: 4px; font-size: 14px; }
        textarea { height: 60px; resize: vertical; }
        button { background-color: #4CAF50; color: white; padding: 12px 24px; border: none; border-radius: 4px; cursor: pointer; font-size: 16px; }
        button:hover { background-color: #45a049; }
        .info { background-color: #e7f3ff; padding: 15px; border-radius: 4px; margin-bottom: 20px; word-wrap: break-word; }
        .warning { background-color: #fff3cd; border: 1px solid #ffeaa7; padding: 15px; border-radius: 4px; margin-bottom: 20px; }
        .generate-btn { background-color: #2196F3; margin-left: 10px; padding: 8px 16px; font-size: 14px; }
        .generate-btn:hover { background-color: #1976D2; }
        .form-row { display: flex; align-items: end; gap: 10px; }
        .form-row input { flex: 1; }
        h1 { color: #333; text-align: center; }
        .subtitle { text-align: center; color: #666; margin-bottom: 30px; }
        .current-config { font-family: monospace; font-size: 12px; }
    </style>
</head>
<body>
    <div class="container">
        <h1>🔐 Nostr Remote Signer</h1>
        <p class="subtitle">Configure your device to act as a secure remote signer for Nostr applications</p>
        
        <div class="warning">
            <strong>⚠️ Security Notice:</strong> Your private key will be stored securely on this device. Never share it with anyone or enter it on untrusted websites.
        </div>
        
        <div class="info">
            <strong>Current Bunker URL:</strong><br>
            <span id="current-url" class="current-config">Loading...</span>
        </div>
        
        <form action="/config" method="post">
            <div class="form-group">
                <label for="private_key">Nostr Private Key (64-character hex):</label>
                <input type="password" id="private_key" name="private_key" placeholder="64-character hex private key" required value="{{private_key}}">
                <small style="color: #666;">Enter your Nostr private key as 64 hex characters</small>
            </div>
            
            <div class="form-group">
                <label for="relay_url">Nostr Relay URL:</label>
                <input type="text" id="relay_url" name="relay_url" placeholder="wss://relay.nostrconnect.com" required value="{{relay_url}}">
                <small style="color: #666;">WebSocket URL of the Nostr relay to connect to</small>
            </div>
            
            <div class="form-group">
                <label for="public_key">Public Key (readonly):</label>
                <input type="text" id="public_key" name="public_key" readonly style="background-color: #f8f9fa;">
                <small style="color: #666;">This will be automatically calculated from your private key</small>
            </div>
            
            <button type="submit" style="width: 100%;">Save Configuration</button>
        </form>
    </div>
</body>
</html>
        )";
        // now fill in current values
        String currentRelay = RemoteSigner::getRelayUrl();
        String currentPrivateKey = RemoteSigner::getPrivateKey();
        if (currentPrivateKey.length() > 0) {
            html.replace("{{private_key}}", currentPrivateKey);
        }
        if (currentRelay.length() > 0) {
            html.replace("{{relay_url}}", currentRelay);
        } else {
            html.replace("{{relay_url}}", "wss://relay.nostrconnect.com");
        }

        ap_server.send(200, "text/html", html);
    }
    
    void handleAPConfig() {
        if (!ap_server.hasArg("private_key") || !ap_server.hasArg("relay_url")) {
            ap_server.send(400, "text/plain", "Missing required parameters");
            return;
        }
        
        String privateKey = ap_server.arg("private_key");
        String relayUrl = ap_server.arg("relay_url");
        
        Serial.println("Configuring Remote Signer...");
        Serial.println("Private Key length: " + String(privateKey.length()));
        Serial.println("Relay URL: " + relayUrl);
        
        // Validate private key format
        if (privateKey.length() != 64 && !privateKey.startsWith("nsec1")) {
            ap_server.send(400, "text/plain", "Invalid private key format");
            return;
        }
        
        // Convert nsec to hex if needed
        String privateKeyHex = privateKey;
        if (privateKey.startsWith("nsec1")) {
            // TODO: Add nsec to hex conversion if needed
            Serial.println("WARNING: nsec format not yet supported, use hex format");
            ap_server.send(400, "text/plain", "Please use hex format for private key");
            return;
        }
        
        // Derive public key using nostr library
        String publicKeyHex = "";
        try {
            int byteSize = 32;
            byte privateKeyBytes[byteSize];
            fromHex(privateKeyHex, privateKeyBytes, byteSize);
            PrivateKey privKey(privateKeyBytes);
            PublicKey pub = privKey.publicKey();
            publicKeyHex = pub.toString();
            // remove leading 2 bytes from public key
            publicKeyHex = publicKeyHex.substring(2);
            Serial.println("Derived public key: " + publicKeyHex);
        } catch (...) {
            Serial.println("ERROR: Failed to derive public key");
            ap_server.send(400, "text/plain", "Invalid private key - could not derive public key");
            return;
        }
        
        // Save configuration to RemoteSigner
        RemoteSigner::setRelayUrl(relayUrl);
        RemoteSigner::setPrivateKey(privateKeyHex);
        
        // Save to preferences
        Preferences prefs;
        prefs.begin("signer", false);
        prefs.putString("private_key", privateKeyHex);
        prefs.putString("public_key", publicKeyHex);
        prefs.putString("relay_url", relayUrl);
        prefs.end();
        
        Serial.println("Remote Signer configuration saved successfully");
        
        String html = R"(
<!DOCTYPE html>
<html>
<head>
    <title>Configuration Saved</title>
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <style>
        body { font-family: Arial, sans-serif; margin: 20px; text-align: center; background-color: #f5f5f5; }
        .container { max-width: 500px; margin: 0 auto; background: white; padding: 30px; border-radius: 8px; box-shadow: 0 2px 10px rgba(0,0,0,0.1); }
        .success { color: #4CAF50; font-size: 24px; margin: 20px 0; }
        .info { background-color: #e7f3ff; padding: 15px; border-radius: 4px; margin: 20px 0; }
        .back-btn { background-color: #2196F3; color: white; padding: 12px 24px; text-decoration: none; border-radius: 4px; display: inline-block; margin-top: 20px; }
        .config-item { margin: 10px 0; text-align: left; }
        .config-label { font-weight: bold; color: #333; }
        .config-value { font-family: monospace; font-size: 12px; word-break: break-all; color: #666; }
    </style>
</head>
<body>
    <div class="container">
        <div class="success">✓ Configuration saved successfully!</div>
        
        <div class="info">
            <div class="config-item">
                <div class="config-label">Public Key:</div>
                <div class="config-value">)" + publicKeyHex + R"(</div>
            </div>
            <div class="config-item">
                <div class="config-label">Relay:</div>
                <div class="config-value">)" + relayUrl + R"(</div>
            </div>
        </div>
        
        <p>Your remote signer is now configured and ready to use.</p>
        <a href="/" class="back-btn">Back to Configuration</a>
    </div>
</body>
</html>
        )";
        
        ap_server.send(200, "text/html", html);
    }
    
    void handleCurrentConfig() {
        String response = "{";
        response += "\"bunker_url\":\"" + RemoteSigner::getBunkerUrl() + "\",";
        response += "\"relay_url\":\"" + RemoteSigner::getRelayUrl() + "\",";
        response += "\"public_key\":\"" + RemoteSigner::getPublicKey() + "\",";
        response += "\"private_key\":\"";
        response += (RemoteSigner::getPrivateKey().length() > 0 ? "configured" : "");
        response += "\"";
        response += "}";
        
        ap_server.send(200, "application/json", response);
    }
    
    void updateSettingsScreenForAPMode() {
        UI::loadScreen((UI::screen_state_t)1); // SCREEN_SETTINGS
    }
    
    void pauseBackgroundOperations(bool pause) {
        background_operations_paused = pause;
        Serial.println("WiFiManager background operations " + String(pause ? "paused" : "resumed"));
    }
    
    bool isBackgroundOperationsPaused() {
        return background_operations_paused;
    }
}