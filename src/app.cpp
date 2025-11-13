#include "app.h"
#include "esp_sleep.h"
#include "driver/gpio.h"
#include "driver/rtc_io.h"

namespace App
{
    // Application state
    static app_state_t current_state = APP_STATE_INITIALIZING;
    static String last_error = "";
    static app_event_callback_t event_callback = nullptr;
    static unsigned long last_health_check = 0;
    static unsigned long last_status_report = 0;

    // Activity tracking (for diagnostics only)
    static unsigned long last_activity_time = 0;

    void init()
    {
        Serial.println("=== Remote Nostr Signer Initializing ===");
        Serial.println("Version: " + Config::VERSION);
        Serial.println("Build Date: " + Config::BUILD_DATE);

        setState(APP_STATE_INITIALIZING);

        try
        {
            // Initialize modules in dependency order
            Serial.println("Initializing Display module...");
            Display::init();

            Serial.println("Initializing Settings module...");
            Settings::init();

            Serial.println("Initializing WiFi Manager module...");
            WiFiManager::init();

            // Set up WiFi status callback
            WiFiManager::setStatusCallback([](bool connected, const char *status)
                                           { App::notifyWiFiStatusChanged(connected); });

            Serial.println("Initializing UI module...");
            UI::init();

            Serial.println("Initializing Remote Signer module...");
            RemoteSigner::init();

            Serial.println("Initializing Firmware Update module...");
            FirmwareUpdate::init();
            
            // Set up Firmware Update callbacks
            FirmwareUpdate::setStatusCallback([](FirmwareUpdate::update_status_t status, FirmwareUpdate::update_error_t error) {
                App::notifyFirmwareUpdateStatusChanged(status, error);
            });
            
            FirmwareUpdate::setProgressCallback([](int progress, size_t current, size_t total) {
                UI::updateFirmwareProgress(progress, current, total);
            });

            // Set up Remote Signer status callback
            RemoteSigner::setStatusCallback([](bool connected, const String &status)
                                           { App::notifySignerStatusChanged(connected); });

            // Load the initial signer screen
            UI::loadScreen(UI::SCREEN_SIGNER_STATUS);

            // Check if WiFi is already connected and trigger signer connection if needed
            if (WiFiManager::isConnected())
            {
                Serial.println("WiFi already connected during initialization - connecting to relay");
                notifyWiFiStatusChanged(true);
            }

            setState(APP_STATE_READY);
            Serial.println("=== Application initialized successfully ===");

            fireEvent("app_initialized", "success");
        }
        catch (const std::exception &e)
        {
            setState(APP_STATE_ERROR);
        }
    }

    void cleanup()
    {
        Serial.println("=== Application cleanup starting ===");

        // Cleanup modules in reverse dependency order
        FirmwareUpdate::cleanup();
        RemoteSigner::cleanup();
        UI::cleanup();
        WiFiManager::cleanup();
        Settings::cleanup();
        Display::cleanup();

        setState(APP_STATE_INITIALIZING);
        Serial.println("=== Application cleanup completed ===");

        fireEvent("app_cleanup", "completed");
    }

    void notifyFirmwareUpdateStatusChanged(FirmwareUpdate::update_status_t status, FirmwareUpdate::update_error_t error)
    {
        Serial.println("Firmware update status changed: " + FirmwareUpdate::getStatusMessage());
        
        switch (status) {
            case FirmwareUpdate::UPDATE_AVAILABLE:
                UI::loadScreen(UI::SCREEN_UPDATE_CONFIRM);
                break;
                
            case FirmwareUpdate::UPDATE_NO_UPDATE:
                UI::showMessage("No Updates", "You have the latest firmware version.");
                delay(2000);
                UI::loadScreen(UI::SCREEN_SETTINGS);
                break;
                
            case FirmwareUpdate::UPDATE_SUCCESS:
                UI::showMessage("Update Complete", "Firmware updated successfully! Device will restart.");
                delay(3000);
                ESP.restart();
                break;
                
            case FirmwareUpdate::UPDATE_ERROR:
                String errorMsg;
                switch (error) {
                    case FirmwareUpdate::ERROR_NETWORK:
                        errorMsg = "Network connection failed";
                        break;
                    case FirmwareUpdate::ERROR_DOWNLOAD_FAILED:
                        errorMsg = "Download failed";
                        break;
                    case FirmwareUpdate::ERROR_FLASH_FAILED:
                        errorMsg = "Installation failed";
                        break;
                    default:
                        errorMsg = "Update failed: " + FirmwareUpdate::getStatusMessage();
                        break;
                }
                UI::showMessage("Update Failed", errorMsg);
                delay(3000);
                UI::loadScreen(UI::SCREEN_SETTINGS);
                break;
        }
        
        fireEvent("firmware_update", FirmwareUpdate::getStatusMessage().c_str());
    }

    void run()
    {
        static bool first_run = true;
        if (first_run) {
            Serial.println("=== App::run() started ===");
            first_run = false;
        }
        
        unsigned long current_time = millis();

        // Process WiFi AP mode events (with error handling)
        try {
            WiFiManager::processLoop();
        } catch (...) {
            Serial.println("ERROR: WiFiManager::processLoop() threw exception");
        }

        // Process Remote Signer WebSocket events (with error handling)
        try {
            RemoteSigner::processLoop();
        } catch (...) {
            Serial.println("ERROR: RemoteSigner::processLoop() threw exception");
        }

        // Check backlight timeout
        Display::checkBacklightTimeout();
        // Periodic health checks
        if (current_time - last_health_check >= Config::HEALTH_CHECK_INTERVAL)
        {
            checkModuleHealth();
            last_health_check = current_time;
        }

        // Periodic status reports
        if (current_time - last_status_report >= Config::STATUS_REPORT_INTERVAL)
        {
            reportModuleStatus();
            last_status_report = current_time;
        }

        // No additional processing needed for continuous monitoring

        // Small delay to prevent watchdog triggers
        delay(1);
    }

    void setState(app_state_t state)
    {
        if (current_state != state)
        {
            current_state = state;
            Serial.println("App state changed to: " + getStateString());
            fireEvent("state_changed", getStateString());
        }
    }

    String getStateString()
    {
        switch (current_state)
        {
        case APP_STATE_INITIALIZING:
            return "Initializing";
        case APP_STATE_READY:
            return "Ready";
        case APP_STATE_ERROR:
            return "Error";
        case APP_STATE_UPDATING:
            return "Updating";
        default:
            return "Unknown";
        }
    }

    void notifyWiFiStatusChanged(bool connected)
    {
        static bool last_wifi_status = false;

        // Prevent duplicate notifications
        if (last_wifi_status == connected)
        {
            return;
        }
        last_wifi_status = connected;

        Serial.println("WiFi status changed: " + String(connected ? "Connected" : "Disconnected"));

        if (connected)
        {
            // WiFi connected - attempt Remote Signer connection
            if (!RemoteSigner::isConnected())
            {
                Serial.println("WiFi connected, attempting relay connection...");
                RemoteSigner::connectToRelay();
            }
        }
        else
        {
            // WiFi disconnected - notify Remote Signer module
            Serial.println("WiFi disconnected, disconnecting from relay...");
            RemoteSigner::disconnect();
        }

        fireEvent("wifi_status", connected ? "connected" : "disconnected");
    }

    void notifySignerStatusChanged(bool connected)
    {
        static bool last_signer_status = false;

        // Prevent duplicate notifications
        if (last_signer_status == connected)
        {
            return;
        }
        last_signer_status = connected;

        Serial.println("Remote Signer status changed: " + String(connected ? "Connected" : "Disconnected"));
        RemoteSigner::displayConnectionStatus(connected);
        fireEvent("signer_status", connected ? "connected" : "disconnected");
    }

    void resetToDefaults()
    {
        Serial.println("Resetting to default configuration...");

        Settings::resetToDefaults();

        fireEvent("reset_defaults", "completed");
    }

    String getVersion()
    {
        return Config::VERSION;
    }

    void resetActivityTimer()
    {
        last_activity_time = millis();
    }
    
    void handleTouchWake()
    {
        resetActivityTimer();
    }

    bool checkModuleHealth()
    {
        bool health_ok = true;

        // Check WiFi module health
        if (!WiFiManager::isConnected() && WiFiManager::getStatus() != WL_IDLE_STATUS)
        {
            Serial.println("WiFi module health check failed");
            health_ok = false;
        }

        // Check Remote Signer module health
        if (RemoteSigner::isInitialized() && !RemoteSigner::isConnected())
        {
            Serial.println("Remote Signer module health check warning - not connected");
            // This is a warning, not a failure
        }

        // Check UI module health
        if (UI::getCurrentScreen() < 0)
        {
            Serial.println("UI module health check failed");
            health_ok = false;
        }

        return health_ok;
    }

    void reportModuleStatus()
    {
        Serial.println("=== Module Status Report ===");
        Serial.println("WiFi: " + String(WiFiManager::isConnected() ? "Connected" : "Disconnected"));
        if (WiFiManager::isConnected())
        {
            Serial.println("  SSID: " + WiFiManager::getSSID());
            Serial.println("  IP: " + WiFiManager::getLocalIP());
        }
        Serial.println("Remote Signer: " + String(RemoteSigner::isConnected() ? "Connected" : "Disconnected"));
        if (RemoteSigner::isConnected())
        {
            Serial.println("  Relay: " + RemoteSigner::getRelayUrl());
        }
        Serial.println("Current Screen: " + String(UI::getCurrentScreen()));
        Serial.println("Free Heap: " + String(ESP.getFreeHeap()));
        Serial.println("============================");
    }

    void fireEvent(const String &event, const String &data)
    {
        if (event_callback)
        {
            event_callback(event, data);
        }
    }
}
