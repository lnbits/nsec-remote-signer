/**
 * @file main.cpp
 * @brief Main application entry point for Remote Nostr Signer
 * 
 * Modular ESP32 Point of Sale system featuring:
 * - Touch screen interface
 * - WiFi connectivity with AP mode setup
 * - Nostr Wallet Connect integration for Lightning payments
 * - LVGL-based user interface with QR code generation
 * - Deep sleep power management
 * 
 * @author BlackCoffee bc@lnbits.com
 * @version 1.0.0
 * @date 07-2025
 */

// ArduinoGFX is now included via display.h
#include <Arduino.h>
#include <lvgl.h>
#include <WiFi.h>
#include <Preferences.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "app.h"

// Import Nostr library for memory initialization
#include "../lib/nostr/nostr.h"

static const String SOFTWARE_VERSION = "v1.0.0";

// Memory space definitions for Nostr operations to prevent heap fragmentation
#define EVENT_NOTE_SIZE 2000000
#define ENCRYPTED_MESSAGE_BIN_SIZE 100000

// Remaining global variables that main.cpp still needs
static unsigned long wifi_connect_start_time = 0;
static const unsigned long WIFI_CONNECT_TIMEOUT = 10000; // 10 seconds
static bool wifi_connection_attempted = false;

// Legacy AP mode variables removed - now handled by WiFiManager module

// Queue definitions for task communication
typedef enum {
    WIFI_SCAN,
    WIFI_CONNECT,
    WIFI_DISCONNECT,
    WIFI_STOP_SCAN
} wifi_command_type_t;

typedef struct {
    wifi_command_type_t type;
    char ssid[33];
    char password[65];
} wifi_command_t;

typedef struct {
    int network_count;
    char ssids[9][33];
    int rssi[9];
    bool encrypted[9];
} wifi_scan_result_t;

// Task management
static TaskHandle_t wifi_task_handle = NULL;
static TaskHandle_t invoice_task_handle = NULL;
static QueueHandle_t wifi_command_queue = NULL;
static QueueHandle_t wifi_scan_result_queue = NULL;
static QueueHandle_t invoice_command_queue = NULL;
static QueueHandle_t invoice_status_queue = NULL;

// Global UI state variables (to be migrated to UI module eventually)
static char current_ssid[33];
static char current_password[65];
static lv_obj_t *wifi_status_label = NULL;
static lv_timer_t *wifi_status_timer = NULL;
static lv_obj_t *main_wifi_status_label = NULL;
static lv_obj_t *relay_status_label = NULL;

// Invoice overlay variables (to be migrated to UI module eventually)
static lv_timer_t *invoice_timer = NULL;
static int invoice_counter = 0;

// Global state for preferences
static Preferences preferences;

// Forward declarations for functions that still need to be implemented or migrated
static void wifi_main_status_updater_cb(lv_timer_t *timer);

void setup(void)
{
    Serial.begin(115200);
    Serial.println("=== Remote Nostr Signer Starting ===");
    Serial.println("Software Version: " + SOFTWARE_VERSION);
    
    // Initialize PSRAM memory space for Nostr operations to prevent heap fragmentation
    Serial.println("Initializing Nostr memory space...");
    nostr::initMemorySpace(EVENT_NOTE_SIZE, ENCRYPTED_MESSAGE_BIN_SIZE);
    Serial.println("Nostr memory space initialized");
    
    // Initialize all application modules through the App coordinator
    App::init();
    
    // Create status update timer (this will eventually move to a module)
    lv_timer_create(wifi_main_status_updater_cb, 1000, NULL);
    
    // Create queues for task communication (this will eventually move to respective modules)
    wifi_command_queue = xQueueCreate(10, sizeof(wifi_command_t));
    wifi_scan_result_queue = xQueueCreate(5, sizeof(wifi_scan_result_t));
    invoice_command_queue = xQueueCreate(5, sizeof(int));
    invoice_status_queue = xQueueCreate(5, sizeof(int));
    
    Serial.println("=== Setup Complete ===");
}

void loop(void)
{
    lv_timer_handler();
    
    App::run();
    
    delay(5);
}

// Temporary status update function (will be moved to appropriate module)
static void wifi_main_status_updater_cb(lv_timer_t *timer) {
    // Check WiFi connection status and timeout
    bool wifi_connected = (WiFi.status() == WL_CONNECTED);
    
    // If we attempted to connect but haven't succeeded within timeout, stop trying
    if (wifi_connection_attempted && !wifi_connected) {
        unsigned long current_time = millis();
        if (current_time - wifi_connect_start_time > WIFI_CONNECT_TIMEOUT) {
            Serial.println("WiFi connection timeout - stopping connection attempts");
            WiFi.disconnect(true);
            wifi_connection_attempted = false;
        }
    }
    
    // AP mode processing now handled by WiFiManager module
    
    delay(1);
}