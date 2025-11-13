#pragma once

#include <Arduino.h>
#include <WebSocketsClient.h>
#include <ArduinoJson.h>
#include <WiFiUdp.h>
#include <NTPClient.h>
#include <lvgl.h>

namespace RemoteSigner {
    // Initialization and cleanup
    void init();
    void cleanup();
    
    // Connection management
    void connectToRelay();
    void disconnect();
    bool isInitialized();
    bool isConnected();
    
    // Configuration management
    void loadConfigFromPreferences();
    void saveConfigToPreferences();
    String getRelayUrl();
    void setRelayUrl(const String& url);
    
    // User keypair management (for signing events)
    String getUserPrivateKey();
    void setUserPrivateKey(const String& privKeyHex);
    String getUserPublicKey();
    
    // Device keypair management (for NIP-46 communication, read-only)
    String getDevicePublicKey();
    
    // Legacy compatibility (maps to user keypair)
    String getPrivateKey();
    void setPrivateKey(const String& privKeyHex);
    String getPublicKey();
    
    String getBunkerUrl();

    bool trySaveConfigToPreferences();
    int getAuthorizedClientCount();
    void removeOldestClient();
    void removeHalfClients();
    
    // WebSocket event handling
    void websocketEvent(WStype_t type, uint8_t* payload, size_t length);
    void handleWebsocketMessage(void* arg, uint8_t* data, size_t len);
    void resetWebsocketFragmentState();
    
    // NIP-46 protocol handlers
    void handleSigningRequestEvent(uint8_t* data);
    void handleConnect(DynamicJsonDocument& doc, const String& requestingPubKey);
    void handleSignEvent(DynamicJsonDocument& doc, const char* requestingPubKey);
    void handlePing(DynamicJsonDocument& doc, const char* requestingPubKey);
    void handleGetPublicKey(DynamicJsonDocument& doc, const char* requestingPubKey);
    void handleNip04Encrypt(DynamicJsonDocument& doc, const char* requestingPubKey);
    void handleNip04Decrypt(DynamicJsonDocument& doc, const char* requestingPubKey);
    void handleNip44Encrypt(DynamicJsonDocument& doc, const char* requestingPubKey);
    void handleNip44Decrypt(DynamicJsonDocument& doc, const char* requestingPubKey);
    
    // Client authorization management
    bool isClientAuthorized(const char* clientPubKey);
    bool promptUserForAuthorization(const String& requestingNpub);
    void addAuthorizedClient(const char* clientPubKey);
    bool checkClientIsAuthorized(const char* clientPubKey, const char* secret);
    void clearAllAuthorizedClients();
    int getAuthorizedClientCount();
    
    // Device keypair generation (internal use)
    void generateDeviceKeypair();
    void saveDeviceKeypairToPreferences();
    
    // Secret key management
    void refreshSecretKey();
    
    // Connection monitoring
    void processLoop();
    void sendPing();
    void updateConnectionStatus();
    
    // Time synchronization
    unsigned long getUnixTimestamp();
    
    // Fragment handling
    bool isFragmentInProgress();
    void handleFragment(uint8_t* payload, size_t length, bool isBinary, bool isLast);
    
    // Status callbacks
    typedef void (*signer_status_callback_t)(bool connected, const String& status);
    void setStatusCallback(signer_status_callback_t callback);
    
    // UI integration
    void setStatusLabel(lv_obj_t* label);
    void displaySigningRequest(const String& eventKind, const String& content);
    void displayConnectionStatus(bool connected);
    
    // Event signing UI callbacks
    typedef void (*signing_confirmation_callback_t)(bool approved);
    
    // Constants
    namespace Config {
        const unsigned long WS_PING_INTERVAL = 5000;
        const unsigned long WS_FRAGMENT_TIMEOUT = 30000;
        const size_t WS_MAX_FRAGMENT_SIZE = 1024 * 1024;
        const unsigned long CONNECTION_TIMEOUT = 30000;
        const int MAX_RECONNECT_ATTEMPTS = 10;
        const unsigned long MIN_RECONNECT_INTERVAL = 5000;
    }
    
    // NIP-46 Methods
    namespace Methods {
        extern const char* CONNECT;
        extern const char* SIGN_EVENT;
        extern const char* PING;
        extern const char* GET_PUBLIC_KEY;
        extern const char* NIP04_ENCRYPT;
        extern const char* NIP04_DECRYPT;
        extern const char* NIP44_ENCRYPT;
        extern const char* NIP44_DECRYPT;
    }
}