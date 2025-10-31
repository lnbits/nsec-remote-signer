#include "remote_signer.h"
#include "settings.h"
#include "ui.h"
#include "app.h"
#include "display.h"
#include "wifi_manager.h"
#include <Preferences.h>
#include "lvgl.h"

// Import Nostr library components from lib/ folder
#include "../lib/nostr/nostr.h"
#include "../lib/nostr/nip44/nip44.h"
#include "../lib/nostr/nip19.h"

namespace RemoteSigner {
    // NIP-46 Method constants
    namespace Methods {
        const char* CONNECT = "connect";
        const char* SIGN_EVENT = "sign_event";
        const char* PING = "ping";
        const char* GET_PUBLIC_KEY = "get_public_key";
        const char* NIP04_ENCRYPT = "nip04_encrypt";
        const char* NIP04_DECRYPT = "nip04_decrypt";
        const char* NIP44_ENCRYPT = "nip44_encrypt";
        const char* NIP44_DECRYPT = "nip44_decrypt";
    }
    
    // WebSocket client
    static WebSocketsClient webSocket;
    static unsigned long last_loop_time = 0;
    
    // Configuration
    static String relayUrl = "";
    static String privateKeyHex = "";
    static String publicKeyHex = "";
    static String secretKey = "";
    static String authorizedClients = "";
    
    // Connection state
    static bool signer_initialized = false;
    static bool connection_in_progress = false;
    static unsigned long last_connection_attempt = 0;
    static unsigned long last_ws_ping = 0;
    static unsigned long last_ws_message_received = 0;
    static int reconnection_attempts = 0;
    static unsigned long last_reconnect_attempt = 0;
    static bool manual_reconnect_needed = false;
    
    // WebSocket fragment management
    static bool ws_fragment_in_progress = false;
    static String ws_fragmented_message = "";
    static size_t ws_fragment_total_size = 0;
    static size_t ws_fragment_received_size = 0;
    static unsigned long ws_fragment_start_time = 0;
    
    // NTP time synchronization
    static WiFiUDP ntpUDP;
    static NTPClient timeClient(ntpUDP, "pool.ntp.org", 0, 60000);
    static unsigned long unixTimestamp = 0;
    
    // Status callback
    static signer_status_callback_t status_callback = nullptr;
    static signing_confirmation_callback_t signing_callback = nullptr;
    static lv_obj_t* status_label = nullptr;
    
    // Memory allocation for JSON documents
    static const size_t JSON_DOC_SIZE = 100000;
    static DynamicJsonDocument eventDoc(0);
    static DynamicJsonDocument eventParamsDoc(0);
    
    void init() {
        Serial.println("RemoteSigner::init() - Initializing Remote Signer module");
        
        // Initialize memory for JSON documents
        eventDoc = DynamicJsonDocument(JSON_DOC_SIZE);
        eventParamsDoc = DynamicJsonDocument(JSON_DOC_SIZE);
        
        // Load configuration
        loadConfigFromPreferences();
        
        // Generate initial secret key
        refreshSecretKey();
        
        // Initialize time client
        timeClient.begin();
        
        signer_initialized = true;
        Serial.println("RemoteSigner::init() - Remote Signer module initialized");
    }
    
    void cleanup() {
        Serial.println("RemoteSigner::cleanup() - Cleaning up Remote Signer module");
        
        disconnect();
        signer_initialized = false;
        
        Serial.println("RemoteSigner::cleanup() - Remote Signer module cleaned up");
    }
    
    void loadConfigFromPreferences() {
        Preferences prefs;
        prefs.begin("signer", true); // Read-only
        
        relayUrl = prefs.getString("relay_url", "wss://relay.nostriot.com");
        privateKeyHex = prefs.getString("private_key", "");
        // derive public key from private key
        if (privateKeyHex.length() == 64) {
            try {
                int byteSize = 32;
                byte privateKeyBytes[byteSize];
                fromHex(privateKeyHex, privateKeyBytes, byteSize);
                PrivateKey privKey(privateKeyBytes);
                PublicKey pub = privKey.publicKey();
                publicKeyHex = pub.toString();
                // remove leading 2 bytes from public key
                publicKeyHex = publicKeyHex.substring(2);
            } catch (...) {
                Serial.println("RemoteSigner: ERROR - Failed to derive public key");
            }
        }
        
        authorizedClients = prefs.getString("auth_clients", "");
        
        prefs.end();
        
        Serial.println("RemoteSigner::loadConfigFromPreferences() - Configuration loaded");
        Serial.println("Relay URL: " + relayUrl);
        Serial.println("Has private key: " + String(privateKeyHex.length() > 0 ? "Yes" : "No"));
    }
    
    void saveConfigToPreferences() {
        Preferences prefs;
        prefs.begin("signer", false); // Read-write
        
        prefs.putString("relay_url", relayUrl);
        prefs.putString("private_key", privateKeyHex);
        prefs.putString("public_key", publicKeyHex);
        prefs.putString("auth_clients", authorizedClients);
        
        prefs.end();
        
        Serial.println("RemoteSigner::saveConfigToPreferences() - Configuration saved");
    }
    
    void refreshSecretKey() {
        // Generate a random 64 character hex string
        secretKey = "";
        for (int i = 0; i < 64; i++) {
            secretKey += "0123456789abcdef"[esp_random() % 16];
        }
        Serial.println("RemoteSigner::refreshSecretKey() - New secret key generated");
    }
    
    String getBunkerUrl() {
        if (publicKeyHex.length() == 0 || relayUrl.length() == 0) {
            return "";
        }
        
        return "bunker://" + publicKeyHex + "?relay=" + relayUrl + "&secret=" + secretKey;
    }
    
    void connectToRelay() {
        if (!signer_initialized || relayUrl.length() == 0) {
            Serial.println("RemoteSigner::connectToRelay() - Cannot connect: not initialized or no relay URL");
            return;
        }
        
        if (!WiFiManager::isConnected()) {
            Serial.println("RemoteSigner::connectToRelay() - Cannot connect: WiFi not connected");
            return;
        }
        
        if (connection_in_progress) {
            Serial.println("RemoteSigner::connectToRelay() - Connection already in progress");
            return;
        }
        
        Serial.println("RemoteSigner::connectToRelay() - Connecting to relay: " + relayUrl);
        Serial.println("Connection attempt #" + String(reconnection_attempts + 1) + " of " + String(Config::MAX_RECONNECT_ATTEMPTS));
        
        connection_in_progress = true;
        last_connection_attempt = millis();
        
        // Update status display immediately
        displayConnectionStatus(false);
        
        // Extract hostname from relay URL (remove wss:// prefix)
        String hostname = relayUrl;
        hostname.replace("wss://", "");
        hostname.replace("ws://", "");
        
        webSocket.beginSSL(hostname.c_str(), 443, "/");
        webSocket.onEvent(websocketEvent);
        webSocket.setReconnectInterval(Config::MIN_RECONNECT_INTERVAL);
        
        if (status_callback) {
            status_callback(false, "Connecting to relay...");
        }
    }
    
    void disconnect() {
        Serial.println("RemoteSigner::disconnect() - Disconnecting from relay");
        Serial.println("Connection was active for: " + String((millis() - last_connection_attempt) / 1000) + "s");
        
        webSocket.disconnect();
        connection_in_progress = false;
        
        // Update status display immediately
        displayConnectionStatus(false);
        
        if (status_callback) {
            status_callback(false, "Disconnected");
        }
    }
    
    void websocketEvent(WStype_t type, uint8_t* payload, size_t length) {
        switch (type) {
            case WStype_DISCONNECTED:
                Serial.println("RemoteSigner::websocketEvent() - WebSocket Disconnected");
                connection_in_progress = false;
                
                // Update status display immediately
                displayConnectionStatus(false);
                
                if (reconnection_attempts < Config::MAX_RECONNECT_ATTEMPTS) {
                    reconnection_attempts++;
                    Serial.println("RemoteSigner::websocketEvent() - Scheduling reconnection attempt " + String(reconnection_attempts));
                    manual_reconnect_needed = true;
                    
                    if (status_callback) {
                        status_callback(false, "Reconnecting...");
                    }
                } else {
                    Serial.println("RemoteSigner::websocketEvent() - Max reconnection attempts reached");
                    if (status_callback) {
                        status_callback(false, "Connection failed");
                    }
                }
                break;
                
            case WStype_CONNECTED:
                Serial.println("RemoteSigner::websocketEvent() - WebSocket Connected to: " + String((char*)payload));
                connection_in_progress = false;
                reconnection_attempts = 0;
                manual_reconnect_needed = false;
                last_ws_message_received = millis();
                
                // Update status display immediately
                displayConnectionStatus(true);
                
                // Subscribe to NIP-46 events for our public key
                if (publicKeyHex.length() > 0) {
                    String subscription = "[\"REQ\", \"signer\", {\"kinds\":[24133], \"#p\":[\"" + publicKeyHex + "\"], \"limit\":0}]";
                    webSocket.sendTXT(subscription);
                    Serial.println("RemoteSigner::websocketEvent() - Sent subscription: " + subscription);
                }
                
                if (status_callback) {
                    status_callback(true, "Connected");
                }
                break;
                
            case WStype_TEXT:
                Serial.println("RemoteSigner::websocketEvent() - Received text message");
                last_ws_message_received = millis();
                handleWebsocketMessage(nullptr, payload, length);
                break;
                
            case WStype_BIN:
                Serial.println("RemoteSigner::websocketEvent() - Received binary message");
                last_ws_message_received = millis();
                handleWebsocketMessage(nullptr, payload, length);
                break;
                
            case WStype_PING:
                Serial.println("RemoteSigner::websocketEvent() - Received ping");
                last_ws_message_received = millis();
                break;
                
            case WStype_PONG:
                Serial.println("RemoteSigner::websocketEvent() - Received pong");
                last_ws_message_received = millis();
                break;
                
            case WStype_ERROR:
                Serial.println("RemoteSigner::websocketEvent() - WebSocket Error");
                connection_in_progress = false;
                manual_reconnect_needed = true;
                
                if (status_callback) {
                    status_callback(false, "Connection error");
                }
                break;
                
            default:
                break;
        }
    }
    
    void handleWebsocketMessage(void* arg, uint8_t* data, size_t len) {
        String message = String((char*)data);
        
        // Check if this is a NIP-46 signing request (EVENT with kind 24133)
        if (message.indexOf("EVENT") != -1 && message.indexOf("24133") != -1) {
            Serial.println("RemoteSigner::handleWebsocketMessage() - Received signing request");
            handleSigningRequestEvent(data);
        }
    }
    
    void handleSigningRequestEvent(uint8_t* data) {
        String dataStr = String((char*)data);
        Serial.println("RemoteSigner::handleSigningRequestEvent() - Processing signing request");
        
        // Extract sender public key from the event using nostr library
        String requestingPubKey = nostr::getSenderPubKeyHex(dataStr);
        Serial.println("RemoteSigner::handleSigningRequestEvent() - Requesting pubkey: " + requestingPubKey);
        
        // Determine encryption type (NIP-04 vs NIP-44) and decrypt
        String decryptedMessage = "";
        boolean isUsingNip44 = true;
        if (dataStr.indexOf("?iv=") != -1) {
            Serial.println("RemoteSigner::handleSigningRequestEvent() - Using NIP-04 decryption");
            decryptedMessage = nostr::nip04Decrypt(privateKeyHex.c_str(), dataStr);
            isUsingNip44 = false;
        } else {
            Serial.println("RemoteSigner::handleSigningRequestEvent() - Using NIP-44 decryption");
            decryptedMessage = nostr::nip44Decrypt(privateKeyHex.c_str(), dataStr);
        }
        
        if (decryptedMessage.length() == 0) {
            Serial.println("RemoteSigner::handleSigningRequestEvent() - Failed to decrypt message");
            return;
        }
        
        Serial.println("RemoteSigner::handleSigningRequestEvent() - Decrypted message: " + decryptedMessage);
        
        // Parse the decrypted JSON
        DeserializationError error = deserializeJson(eventDoc, decryptedMessage);
        if (error) {
            Serial.println("RemoteSigner::handleSigningRequestEvent() - JSON parsing failed: " + String(error.c_str()));
            return;
        }
        
        String method = eventDoc["method"];
        Serial.println("RemoteSigner::handleSigningRequestEvent() - Method: " + method);
        
        if (method == Methods::CONNECT) {
            Display::turnOnBacklightForSigning();
            handleConnect(eventDoc, requestingPubKey, isUsingNip44);
        } else if (method == Methods::SIGN_EVENT) {
            Display::turnOnBacklightForSigning();
            handleSignEvent(eventDoc, requestingPubKey.c_str(), isUsingNip44);
        } else if (method == Methods::PING) {
            handlePing(eventDoc, requestingPubKey.c_str(), isUsingNip44);
        } else if (method == Methods::GET_PUBLIC_KEY) {
            handleGetPublicKey(eventDoc, requestingPubKey.c_str(), isUsingNip44);
        } else if (method == Methods::NIP04_ENCRYPT) {
            Display::turnOnBacklightForSigning();
            handleNip04Encrypt(eventDoc, requestingPubKey.c_str());
        } else if (method == Methods::NIP04_DECRYPT) {
            Display::turnOnBacklightForSigning();
            handleNip04Decrypt(eventDoc, requestingPubKey.c_str());
        } else if (method == Methods::NIP44_ENCRYPT) {
            Display::turnOnBacklightForSigning();
            handleNip44Encrypt(eventDoc, requestingPubKey.c_str());
        } else if (method == Methods::NIP44_DECRYPT) {
            Display::turnOnBacklightForSigning();
            handleNip44Decrypt(eventDoc, requestingPubKey.c_str());
        } else {
            Serial.println("RemoteSigner::handleSigningRequestEvent() - Unknown method: " + method);
        }
    }
    
    void handleConnect(DynamicJsonDocument& doc, const String& requestingPubKey, bool useNip44Encryption) {
        String requestId = doc["id"];
        String secret = doc["params"][1];
        
        Serial.println("RemoteSigner::handleConnect() - Connect request from: " + requestingPubKey);
        
        if (!checkClientIsAuthorized(requestingPubKey.c_str(), secret.c_str())) {
            Serial.println("RemoteSigner::handleConnect() - Client not authorized");
            return;
        }
        
        // Send acknowledgment
        String responseMsg = secret.length() > 0 ? 
            "{\"id\":\"" + requestId + "\",\"result\":\"" + secret + "\"}" :
            "{\"id\":\"" + requestId + "\",\"result\":\"ack\"}";

        Serial.println("RemoteSigner::handleConnect() - Sending connect response: " + responseMsg);
        
        // Encrypt and send response using NIP-44
        String encryptedResponse = nostr::getEncryptedDm(
            privateKeyHex.c_str(), 
            publicKeyHex.c_str(), 
            requestingPubKey.c_str(), 
            24133, 
            unixTimestamp, 
            responseMsg, 
            useNip44Encryption ? "nip44" : "nip04"
        );
        
        webSocket.sendTXT(encryptedResponse);
        Serial.println("RemoteSigner::handleConnect() - Response sent");
    }

    void handleSignEvent(DynamicJsonDocument& doc, const char* requestingPubKey, bool useNip44Encryption) {
        String requestId = doc["id"];
        
        Serial.println("RemoteSigner::handleSignEvent() - Sign event request from: " + String(requestingPubKey));
        
        if (!isClientAuthorized(requestingPubKey)) {
            Serial.println("RemoteSigner::handleSignEvent() - Client not authorized");
            return;
        }
        
        // Parse event parameters - first parameter contains the event to sign
        String eventParams = doc["params"][0].as<String>();
        
        // Parse the event data from the first parameter
        DeserializationError parseError = deserializeJson(eventParamsDoc, eventParams);
        if (parseError) {
            Serial.println("RemoteSigner::handleSignEvent() - Failed to parse event params: " + String(parseError.c_str()));
            return;
        }
        
        // Extract event details
        uint16_t kind = eventParamsDoc["kind"];
        String content = eventParamsDoc["content"].as<String>();
        String tags = eventParamsDoc["tags"].as<String>();
        unsigned long timestamp = eventParamsDoc["created_at"];
        
        Serial.println("RemoteSigner::handleSignEvent() - Event kind: " + String(kind));
        Serial.println("RemoteSigner::handleSignEvent() - Content: " + content.substring(0, 50) + "...");
        
        // Show signing confirmation on display
        displaySigningRequest("Kind " + String(kind), content.substring(0, 30) + "...");
        
        // Show signing modal
        UI::showSigningModal();
        
        // For now, auto-approve (TODO: Add UI confirmation)
        // Sign the event using nostr library
        String signedEvent = nostr::getNote(
            privateKeyHex.c_str(),
            publicKeyHex.c_str(),
            timestamp,
            content,
            kind,
            tags
        );
        
        // Escape quotes in the signed event for JSON response
        signedEvent.replace("\\", "\\\\");
        signedEvent.replace("\"", "\\\"");
        
        // Create response
        String responseMsg = "{\"id\":\"" + requestId + "\",\"result\":\"" + signedEvent + "\"}";
        
        // Update modal to show broadcasting status
        UI::updateSigningModalText("Broadcasting");
        
        // Encrypt and send response
        String encryptedResponse = nostr::getEncryptedDm(
            privateKeyHex.c_str(),
            publicKeyHex.c_str(),
            requestingPubKey,
            24133,
            unixTimestamp,
            responseMsg,
            useNip44Encryption ? "nip44" : "nip04"
        );
        
        webSocket.sendTXT(encryptedResponse);
        Serial.println("RemoteSigner::handleSignEvent() - Event signed and response sent");
        
        // Hide signing modal after 250ms delay as requested
        UI::hideSigningModalDelayed(250);
        
        // Show notification on device screen
        UI::showEventSignedNotification(String(kind), content);
        
        // Notify UI of successful signing
        if (signing_callback) {
            signing_callback(true);
        }
    }

    void handlePing(DynamicJsonDocument& doc, const char* requestingPubKey, bool useNip44Encryption) {
        String requestId = doc["id"];
        
        if (!isClientAuthorized(requestingPubKey)) {
            return;
        }
        
        String responseMsg = "{\"id\":\"" + requestId + "\",\"result\":\"pong\"}";
        
        String encryptedResponse = nostr::getEncryptedDm(
            privateKeyHex.c_str(),
            publicKeyHex.c_str(),
            requestingPubKey,
            24133,
            unixTimestamp,
            responseMsg,
            useNip44Encryption ? "nip44" : "nip04"
        );
        
        webSocket.sendTXT(encryptedResponse);
        Serial.println("RemoteSigner::handlePing() - Pong sent to: " + String(requestingPubKey));
    }

    void handleGetPublicKey(DynamicJsonDocument& doc, const char* requestingPubKey, bool useNip44Encryption) {
        String requestId = doc["id"];
        
        if (!isClientAuthorized(requestingPubKey)) {
            return;
        }
        
        String responseMsg = "{\"id\":\"" + requestId + "\",\"result\":\"" + publicKeyHex + "\"}";
        
        String encryptedResponse = nostr::getEncryptedDm(
            privateKeyHex.c_str(),
            publicKeyHex.c_str(),
            requestingPubKey,
            24133,
            unixTimestamp,
            responseMsg,
            useNip44Encryption ? "nip44" : "nip04"
        );
        
        webSocket.sendTXT(encryptedResponse);
        Serial.println("RemoteSigner::handleGetPublicKey() - Public key sent to: " + String(requestingPubKey));
    }
    
    void handleNip04Encrypt(DynamicJsonDocument& doc, const char* requestingPubKey) {
        if (!isClientAuthorized(requestingPubKey)) {
            return;
        }
        
        String requestId = doc["id"];
        String thirdPartyPubKey = doc["params"][0];
        String plaintext = doc["params"][1];
        
        String encryptedMessage = nostr::getCipherText(privateKeyHex.c_str(), thirdPartyPubKey.c_str(), plaintext);
        String responseMsg = "{\"id\":\"" + requestId + "\",\"result\":\"" + encryptedMessage + "\"}";
        
        String encryptedResponse = nostr::getEncryptedDm(
            privateKeyHex.c_str(),
            publicKeyHex.c_str(),
            requestingPubKey,
            24133,
            unixTimestamp,
            responseMsg,
            "nip04"
        );
        
        webSocket.sendTXT(encryptedResponse);
        Serial.println("RemoteSigner::handleNip04Encrypt() - NIP-04 encryption completed");
    }
    
    void handleNip04Decrypt(DynamicJsonDocument& doc, const char* requestingPubKey) {
        if (!isClientAuthorized(requestingPubKey)) {
            return;
        }
        
        String requestId = doc["id"];
        String thirdPartyPubKey = doc["params"][0];
        String cipherText = doc["params"][1];
        
        String decryptedMessage = nostr::decryptNip04Ciphertext(cipherText, privateKeyHex, thirdPartyPubKey);
        String responseMsg = "{\"id\":\"" + requestId + "\",\"result\":\"" + decryptedMessage + "\"}";
        
        String encryptedResponse = nostr::getEncryptedDm(
            privateKeyHex.c_str(),
            publicKeyHex.c_str(),
            requestingPubKey,
            24133,
            unixTimestamp,
            responseMsg,
            "nip04"
        );
        
        webSocket.sendTXT(encryptedResponse);
        Serial.println("RemoteSigner::handleNip04Decrypt() - NIP-04 decryption completed");
    }
    
    void handleNip44Encrypt(DynamicJsonDocument& doc, const char* requestingPubKey) {
        if (!isClientAuthorized(requestingPubKey)) {
            return;
        }
        
        String requestId = doc["id"];
        String thirdPartyPubKey = doc["params"][0];
        String plaintext = doc["params"][1];
        
        // Use NIP-44 encryption functions
        String encryptedMessage = executeEncryptMessageNip44(plaintext, privateKeyHex, thirdPartyPubKey);
        String responseMsg = "{\"id\":\"" + requestId + "\",\"result\":\"" + encryptedMessage + "\"}";
        
        String encryptedResponse = nostr::getEncryptedDm(
            privateKeyHex.c_str(),
            publicKeyHex.c_str(),
            requestingPubKey,
            24133,
            unixTimestamp,
            responseMsg,
            "nip44"
        );
        
        webSocket.sendTXT(encryptedResponse);
        Serial.println("RemoteSigner::handleNip44Encrypt() - NIP-44 encryption completed");
    }
    
    void handleNip44Decrypt(DynamicJsonDocument& doc, const char* requestingPubKey) {
        if (!isClientAuthorized(requestingPubKey)) {
            return;
        }
        
        String requestId = doc["id"];
        String thirdPartyPubKey = doc["params"][0];
        String cipherText = doc["params"][1];
        
        // Use NIP-44 decryption functions
        String decryptedMessage = executeDecryptMessageNip44(cipherText, privateKeyHex, thirdPartyPubKey);
        String responseMsg = "{\"id\":\"" + requestId + "\",\"result\":\"" + decryptedMessage + "\"}";
        
        String encryptedResponse = nostr::getEncryptedDm(
            privateKeyHex.c_str(),
            publicKeyHex.c_str(),
            requestingPubKey,
            24133,
            unixTimestamp,
            responseMsg,
            "nip44"
        );
        
        webSocket.sendTXT(encryptedResponse);
        Serial.println("RemoteSigner::handleNip44Decrypt() - NIP-44 decryption completed");
    }
    
    bool isClientAuthorized(const char* clientPubKey) {
        return authorizedClients.indexOf(clientPubKey) != -1;
    }
    
    bool checkClientIsAuthorized(const char* clientPubKey, const char* secret) {
        // Check if client is already authorized
        if (isClientAuthorized(clientPubKey)) {
            return true;
        }
        
        // Check if secret matches current secret key
        String secretTrimmed = String(secret);
        secretTrimmed.trim();
        
        if (secretTrimmed == secretKey) {
            Serial.println("RemoteSigner::checkClientIsAuthorized() - Secret key matches, authorizing client");
            addAuthorizedClient(clientPubKey);
            return true;
        }
        
        // Prompt user for authorization
        return promptUserForAuthorization(clientPubKey);
    }
    
    bool promptUserForAuthorization(const String& requestingNpub) {
        Serial.println("RemoteSigner::promptUserForAuthorization() - Prompting user for: " + requestingNpub);
        
        // TODO: Implement UI prompt for user authorization
        // For now, automatically authorize (should be replaced with actual UI)
        addAuthorizedClient(requestingNpub.c_str());
        return true;
    }
    
    void addAuthorizedClient(const char* clientPubKey) {
        if (authorizedClients.indexOf(clientPubKey) == -1) {
            if (authorizedClients.length() > 0) {
                authorizedClients += "|";
            }
            authorizedClients += clientPubKey;
            saveConfigToPreferences();
            Serial.println("RemoteSigner::addAuthorizedClient() - Client authorized: " + String(clientPubKey));
        }
    }
    
    void processLoop() {
        if (!signer_initialized || WiFiManager::isBackgroundOperationsPaused()) {
            return;
        }
        
        // Only update time and process WebSocket if WiFi is connected
        if (WiFiManager::isConnected()) {
            // Update time
            timeClient.update();
            unixTimestamp = timeClient.getEpochTime();
            
            // Process WebSocket events
            webSocket.loop();
            
            // Send periodic ping
            unsigned long now = millis();
            if (now - last_ws_ping > Config::WS_PING_INTERVAL) {
                sendPing();
                last_ws_ping = now;
            }
        }
        
        unsigned long now = millis();
        
        // Periodic status updates every 5 seconds
        static unsigned long last_status_update = 0;
        if (now - last_status_update > 5000) {
            displayConnectionStatus(isConnected());
            last_status_update = now;
        }
        
        // Only do connection-related checks if WiFi is connected
        if (WiFiManager::isConnected()) {
            // Debug: Log connection health every 30 seconds
            static unsigned long last_debug_log = 0;
            if (now - last_debug_log > 30000) {
                if (isConnected()) {
                    Serial.println("RemoteSigner::processLoop() - Connection healthy. Last message: " + String((now - last_ws_message_received) / 1000) + "s ago");
                } else {
                    Serial.println("RemoteSigner::processLoop() - Not connected. Manual reconnect needed: " + String(manual_reconnect_needed ? "Yes" : "No"));
                }
                last_debug_log = now;
            }
            
            // Check connection health
            if (isConnected() && (now - last_ws_message_received > Config::CONNECTION_TIMEOUT)) {
                Serial.println("RemoteSigner::processLoop() - Connection timeout detected");
                Serial.println("Last message received: " + String((now - last_ws_message_received) / 1000) + "s ago");
                disconnect();
                manual_reconnect_needed = true;
            }
            
            // Handle manual reconnection with exponential backoff
            if (manual_reconnect_needed && !connection_in_progress && !isConnected()) {
                unsigned long backoff_delay = Config::MIN_RECONNECT_INTERVAL * (1 << min(reconnection_attempts, 5)); // Cap at 32x base interval
                
                if (now - last_reconnect_attempt >= backoff_delay) {
                    if (reconnection_attempts < Config::MAX_RECONNECT_ATTEMPTS) {
                        Serial.println("RemoteSigner::processLoop() - Attempting manual reconnection #" + String(reconnection_attempts + 1));
                        Serial.println("Backoff delay was: " + String(backoff_delay) + "ms");
                        
                        connectToRelay();
                        last_reconnect_attempt = now;
                        reconnection_attempts++;
                    } else {
                        Serial.println("RemoteSigner::processLoop() - Max reconnection attempts reached, giving up");
                        // reboot the device
                        ESP.restart();
                        manual_reconnect_needed = false;
                        reconnection_attempts = 0;
                        
                        // Update status display immediately
                        displayConnectionStatus(false);
                        
                        if (status_callback) {
                            status_callback(false, "Connection failed permanently");
                        }
                    }
                }
            }
        }
    }
    
    void sendPing() {
        if (isConnected()) {
            Serial.println("RemoteSigner::sendPing() - Sending ping to relay");
            webSocket.sendPing();
        } else {
            Serial.println("RemoteSigner::sendPing() - Cannot send ping: not connected");
        }
    }
    
    bool isInitialized() {
        return signer_initialized;
    }
    
    bool isConnected() {
        return webSocket.isConnected();
    }
    
    unsigned long getUnixTimestamp() {
        return unixTimestamp;
    }
    
    void setStatusCallback(signer_status_callback_t callback) {
        status_callback = callback;
    }
    
    void displaySigningRequest(const String& eventKind, const String& content) {
        Serial.println("RemoteSigner::displaySigningRequest() - " + eventKind + ": " + content);
    }
    
    void displayConnectionStatus(bool connected) {
        if (status_label && lv_obj_is_valid(status_label)) {
            String statusText;
            uint32_t statusColor;
            
            if (connected) {
                statusText = "Relay: Connected";
                statusColor = 0x00FF00; // Green
            } else if (connection_in_progress) {
                statusText = "Relay: Connecting...";
                statusColor = 0xFFA500; // Orange
            } else if (manual_reconnect_needed && reconnection_attempts > 0) {
                statusText = "Relay: Reconnecting (" + String(reconnection_attempts) + "/" + String(Config::MAX_RECONNECT_ATTEMPTS) + ")";
                statusColor = 0xFFA500; // Orange
            } else if (reconnection_attempts >= Config::MAX_RECONNECT_ATTEMPTS) {
                statusText = "Relay: Failed";
                statusColor = 0xFF0000; // Red
            } else {
                statusText = "Relay: Disconnected";
                statusColor = 0x9E9E9E; // Grey
            }
            
            lv_label_set_text(status_label, statusText.c_str());
            lv_obj_set_style_text_color(status_label, lv_color_hex(statusColor), 0);
        }
    }
    
    // Getters
    String getRelayUrl() { return relayUrl; }
    void setRelayUrl(const String& url) { relayUrl = url; }
    String getPrivateKey() { return privateKeyHex; }
    void setPrivateKey(const String& privKeyHex) { 
        privateKeyHex = privKeyHex; 
        // Derive public key automatically
        if (privKeyHex.length() == 64) {
            try {
                int byteSize = 32;
                byte privateKeyBytes[byteSize];
                fromHex(privKeyHex, privateKeyBytes, byteSize);
                PrivateKey privKey(privateKeyBytes);
                PublicKey pub = privKey.publicKey();
                publicKeyHex = pub.toString();
                // remove leading 2 bytes from public key
                publicKeyHex = publicKeyHex.substring(2);
                Serial.println("RemoteSigner: Derived public key: " + publicKeyHex);
            } catch (...) {
                Serial.println("RemoteSigner: ERROR - Failed to derive public key");
            }
        }
    }
    String getPublicKey() { return publicKeyHex; }
    void setStatusLabel(lv_obj_t* label) { 
        status_label = label; 
        // Update status immediately after setting the label
        displayConnectionStatus(isConnected());
    }
}