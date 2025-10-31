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
    String getPrivateKey();
    void setPrivateKey(const String& privKeyHex);
    String getPublicKey();
    String getBunkerUrl();
    
    // WebSocket event handling
    void websocketEvent(WStype_t type, uint8_t* payload, size_t length);
    void handleWebsocketMessage(void* arg, uint8_t* data, size_t len);
    void resetWebsocketFragmentState();
    
    // NIP-46 protocol handlers
    void handleSigningRequestEvent(uint8_t* data);
    void handleConnect(DynamicJsonDocument& doc, const String& requestingPubKey, bool useNip44Encryption);
    void handleSignEvent(DynamicJsonDocument& doc, const char* requestingPubKey, bool useNip44Encryption);
    void handlePing(DynamicJsonDocument& doc, const char* requestingPubKey, bool useNip44Encryption);
    void handleGetPublicKey(DynamicJsonDocument& doc, const char* requestingPubKey, bool useNip44Encryption);
    void handleNip04Encrypt(DynamicJsonDocument& doc, const char* requestingPubKey);
    void handleNip04Decrypt(DynamicJsonDocument& doc, const char* requestingPubKey);
    void handleNip44Encrypt(DynamicJsonDocument& doc, const char* requestingPubKey);
    void handleNip44Decrypt(DynamicJsonDocument& doc, const char* requestingPubKey);
    
    // Client authorization management
    bool isClientAuthorized(const char* clientPubKey);
    bool promptUserForAuthorization(const String& requestingNpub);
    void addAuthorizedClient(const char* clientPubKey);
    bool checkClientIsAuthorized(const char* clientPubKey, const char* secret);
    
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
        const unsigned long WS_PING_INTERVAL = 5000; // 10 seconds
        const unsigned long WS_FRAGMENT_TIMEOUT = 30000; // 30 seconds
        const size_t WS_MAX_FRAGMENT_SIZE = 1024 * 1024; // 1MB
        const unsigned long CONNECTION_TIMEOUT = 30000; // 30 seconds
        const int MAX_RECONNECT_ATTEMPTS = 10;
        const unsigned long MIN_RECONNECT_INTERVAL = 5000; // 5 seconds
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