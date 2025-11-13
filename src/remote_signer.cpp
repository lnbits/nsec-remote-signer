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
    
    // User's keypair (for signing actual events)
    static String userPrivateKeyHex = "";
    static String userPublicKeyHex = "";
    
    // Device/Signer keypair (for NIP-46 communication, generated once and immutable)
    static String devicePrivateKeyHex = "";
    static String devicePublicKeyHex = "";
    
    static String secretKey = "";
    static String authorizedClients = "";
    
    // Client management constants
    static const int MAX_AUTHORIZED_CLIENTS = 30;  // Limit to prevent NVS overflow
    
    // Forward declarations for helper functions
    void generateDeviceKeypair();
    
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
    static unsigned long lastTimeUpdate = 0;
    
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
        
        relayUrl = prefs.getString("relay_url", "wss://relay.nostrconnect.com");
        
        // Load user's keypair (migrate from old "private_key" if needed)
        userPrivateKeyHex = prefs.getString("user_private_key", "");
        if (userPrivateKeyHex.length() == 0) {
            // Migration: check for old "private_key" preference
            userPrivateKeyHex = prefs.getString("private_key", "");
        }
        
        if (userPrivateKeyHex.length() == 64) {
            try {
                int byteSize = 32;
                byte privateKeyBytes[byteSize];
                fromHex(userPrivateKeyHex, privateKeyBytes, byteSize);
                PrivateKey privKey(privateKeyBytes);
                PublicKey pub = privKey.publicKey();
                userPublicKeyHex = pub.toString();
                // remove leading 2 bytes from public key
                userPublicKeyHex = userPublicKeyHex.substring(2);
            } catch (...) {
                Serial.println("RemoteSigner: ERROR - Failed to derive user public key");
            }
        }
        
        devicePrivateKeyHex = prefs.getString("dev_priv_key", "");
        Serial.println("RemoteSigner: Loaded devicePrivateKeyHex: " + devicePrivateKeyHex);
        
        if (devicePrivateKeyHex.length() != 64) {
            Serial.println("RemoteSigner: No valid device keypair found, generating new one");
            prefs.end();
            
            generateDeviceKeypair();
            
            Preferences writePrefs;
            if (writePrefs.begin("signer", false)) {
                Serial.println("RemoteSigner: Saving device_private_key length: " + String(devicePrivateKeyHex.length()));
                writePrefs.putString("dev_priv_key", devicePrivateKeyHex);
                writePrefs.putString("dev_pub_key", devicePublicKeyHex);
                writePrefs.end();
                Serial.println("RemoteSigner: Device keypair saved (immutable)");
            } else {
                Serial.println("RemoteSigner: ERROR - Failed to save device keypair");
            }
            
            prefs.begin("signer", true);
        } else {
            try {
                int byteSize = 32;
                byte privateKeyBytes[byteSize];
                fromHex(devicePrivateKeyHex, privateKeyBytes, byteSize);
                Serial.println("devicePrivateKeyHex loaded from prefs: " + devicePrivateKeyHex);
                PrivateKey privKey(privateKeyBytes);
                PublicKey pub = privKey.publicKey();
                devicePublicKeyHex = pub.toString();
                devicePublicKeyHex = devicePublicKeyHex.substring(2);
            } catch (...) {
                Serial.println("RemoteSigner: ERROR - Failed to derive device public key");
            }
        }
        
        authorizedClients = prefs.getString("auth_clients", "");
        
        prefs.end();
        
    }
    
    void saveConfigToPreferences() {
        if (!trySaveConfigToPreferences()) {
            Serial.println("RemoteSigner::saveConfigToPreferences() - Save failed!");
        }
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
        if (devicePublicKeyHex.length() == 0 || relayUrl.length() == 0) {
            return "";
        }
        
        return "bunker://" + devicePublicKeyHex + "?relay=" + relayUrl + "&secret=" + secretKey;
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
                
                // Subscribe to NIP-46 events for our device public key
                if (devicePublicKeyHex.length() > 0) {
                    String subscription = "[\"REQ\", \"signer\", {\"kinds\":[24133], \"#p\":[\"" + devicePublicKeyHex + "\"], \"limit\":0}]";
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
        long handleWsStartTime = millis();
        Serial.println("handleWebsocketMessageStartTime: " + String(handleWsStartTime));
        String message = String((char*)data);
        
        // Check if this is a NIP-46 signing request (EVENT with kind 24133)
        if (message.indexOf("EVENT") != -1 && message.indexOf("24133") != -1) {
            Serial.println("RemoteSigner::handleWebsocketMessage() - Received signing request");
            handleSigningRequestEvent(data);
        }
        // log time taken to process message
        unsigned long handleWsEndTime = millis();
        Serial.println("handleWebsocketMessageEndTime: " + String(handleWsEndTime));
        Serial.println("RemoteSigner::handleWebsocketMessage() - Time taken to process message: " + String(handleWsEndTime - handleWsStartTime) + " ms");
    }

    void handleSigningRequestEvent(uint8_t* data) {
        String dataStr = String((char*)data);
        Serial.println("RemoteSigner::handleSigningRequestEvent() - Processing signing request");
        
        // Extract sender public key from the event using nostr library
        String requestingPubKey = nostr::getSenderPubKeyHex(dataStr);
        Serial.println("RemoteSigner::handleSigningRequestEvent() - Requesting pubkey: " + requestingPubKey);
        
        // Determine encryption type (NIP-04 vs NIP-44) and decrypt using device keypair
        String decryptedMessage = "";
        if (dataStr.indexOf("?iv=") != -1) {
            Serial.println("RemoteSigner::handleSigningRequestEvent() - Using NIP-04 decryption");
            decryptedMessage = nostr::nip04Decrypt(devicePrivateKeyHex.c_str(), dataStr);
        } else {
            Serial.println("RemoteSigner::handleSigningRequestEvent() - Using NIP-44 decryption");
            decryptedMessage = nostr::nip44Decrypt(devicePrivateKeyHex.c_str(), dataStr);
        }
        
        if (decryptedMessage.length() == 0) {
            Serial.println("RemoteSigner::handleSigningRequestEvent() - Failed to decrypt message");
            UI::showErrorToast("Message decryption failed");
            return;
        }
        
        Serial.println("RemoteSigner::handleSigningRequestEvent() - Decrypted message: " + decryptedMessage);
        
        // Parse the decrypted JSON
        DeserializationError error = deserializeJson(eventDoc, decryptedMessage);
        if (error) {
            Serial.println("RemoteSigner::handleSigningRequestEvent() - JSON parsing failed: " + String(error.c_str()));
            UI::showErrorToast("Invalid request format");
            return;
        }
        
        String method = eventDoc["method"];
        Serial.println("RemoteSigner::handleSigningRequestEvent() - Method: " + method);
        
        if (method == Methods::CONNECT) {
            Display::turnOnBacklightForSigning();
            handleConnect(eventDoc, requestingPubKey);
        } else if (method == Methods::SIGN_EVENT) {
            Display::turnOnBacklightForSigning();
            handleSignEvent(eventDoc, requestingPubKey.c_str());
        } else if (method == Methods::PING) {
            handlePing(eventDoc, requestingPubKey.c_str());
        } else if (method == Methods::GET_PUBLIC_KEY) {
            handleGetPublicKey(eventDoc, requestingPubKey.c_str());
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
    
    void handleConnect(DynamicJsonDocument& doc, const String& requestingPubKey) {
        String requestId = doc["id"];
        String secret = doc["params"][1];
        
        Serial.println("RemoteSigner::handleConnect() - Connect request from: " + requestingPubKey);
        
        if (!checkClientIsAuthorized(requestingPubKey.c_str(), secret.c_str())) {
            Serial.println("RemoteSigner::handleConnect() - Client not authorized");
            UI::showErrorToast("Client not authorised");
            return;
        }
        
        // Send acknowledgment
        String responseMsg = secret.length() > 0 ? 
            "{\"id\":\"" + requestId + "\",\"result\":\"" + secret + "\"}" :
            "{\"id\":\"" + requestId + "\",\"result\":\"ack\"}";

        Serial.println("RemoteSigner::handleConnect() - Sending connect response: " + responseMsg);
        
        // Encrypt and send response using device keypair for NIP-46 communication
        String encryptedResponse = nostr::getEncryptedDm(
            devicePrivateKeyHex.c_str(), 
            devicePublicKeyHex.c_str(), 
            requestingPubKey.c_str(), 
            24133, 
            unixTimestamp, 
            responseMsg, 
            "nip44"
        );
        
        webSocket.sendTXT(encryptedResponse);
        Serial.println("RemoteSigner::handleConnect() - Response sent");
        UI::showSuccessToast("Client connected");
    }
    
    void handleSignEvent(DynamicJsonDocument& doc, const char* requestingPubKey) {
        String requestId = doc["id"];
        
        Serial.println("RemoteSigner::handleSignEvent() - Sign event request from: " + String(requestingPubKey));
        
        if (!isClientAuthorized(requestingPubKey)) {
            Serial.println("RemoteSigner::handleSignEvent() - Client not authorized");
            UI::showErrorToast("Unauthorized signing request");
            return;
        }
        
        // Parse event parameters - first parameter contains the event to sign
        String eventParams = doc["params"][0].as<String>();
        
        // Parse the event data from the first parameter
        DeserializationError parseError = deserializeJson(eventParamsDoc, eventParams);
        if (parseError) {
            Serial.println("RemoteSigner::handleSignEvent() - Failed to parse event params: " + String(parseError.c_str()));
            UI::showErrorToast("Invalid event format");
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
        // UI::showSigningModal();
        
        // Sign the event using user's keypair (not device keypair)
        String signedEvent = nostr::getNote(
            userPrivateKeyHex.c_str(),
            userPublicKeyHex.c_str(),
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
        // UI::updateSigningModalText("Broadcasting");
        
        // Encrypt and send response using device keypair for NIP-46 communication
        String encryptedResponse = nostr::getEncryptedDm(
            devicePrivateKeyHex.c_str(),
            devicePublicKeyHex.c_str(),
            requestingPubKey,
            24133,
            unixTimestamp,
            responseMsg,
            "nip44"
        );
        
        webSocket.sendTXT(encryptedResponse);
        Serial.println("RemoteSigner::handleSignEvent() - Event signed and response sent");
        
        // Hide signing modal after 250ms delay as requested
        // UI::hideSigningModalDelayed(250);
        
        // Show notification on device screen
        UI::showEventSignedNotification(String(kind), content);
        
        // Notify UI of successful signing
        if (signing_callback) {
            signing_callback(true);
        }
    }
    
    void handlePing(DynamicJsonDocument& doc, const char* requestingPubKey) {
        String requestId = doc["id"];
        
        if (!isClientAuthorized(requestingPubKey)) {
            return;
        }
        
        String responseMsg = "{\"id\":\"" + requestId + "\",\"result\":\"pong\"}";
        
        String encryptedResponse = nostr::getEncryptedDm(
            devicePrivateKeyHex.c_str(),
            devicePublicKeyHex.c_str(),
            requestingPubKey,
            24133,
            unixTimestamp,
            responseMsg,
            "nip44"
        );
        
        webSocket.sendTXT(encryptedResponse);
        Serial.println("RemoteSigner::handlePing() - Pong sent to: " + String(requestingPubKey));
    }
    
    void handleGetPublicKey(DynamicJsonDocument& doc, const char* requestingPubKey) {
        String requestId = doc["id"];
        
        if (!isClientAuthorized(requestingPubKey)) {
            return;
        }
        
        String responseMsg = "{\"id\":\"" + requestId + "\",\"result\":\"" + userPublicKeyHex + "\"}";
        
        String encryptedResponse = nostr::getEncryptedDm(
            devicePrivateKeyHex.c_str(),
            devicePublicKeyHex.c_str(),
            requestingPubKey,
            24133,
            unixTimestamp,
            responseMsg,
            "nip44"
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
        
        String encryptedMessage = nostr::getCipherText(userPrivateKeyHex.c_str(), thirdPartyPubKey.c_str(), plaintext);
        String responseMsg = "{\"id\":\"" + requestId + "\",\"result\":\"" + encryptedMessage + "\"}";
        
        String encryptedResponse = nostr::getEncryptedDm(
            devicePrivateKeyHex.c_str(),
            devicePublicKeyHex.c_str(),
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
        
        String decryptedMessage = nostr::decryptNip04Ciphertext(cipherText, userPrivateKeyHex, thirdPartyPubKey);
        String responseMsg = "{\"id\":\"" + requestId + "\",\"result\":\"" + decryptedMessage + "\"}";
        
        String encryptedResponse = nostr::getEncryptedDm(
            devicePrivateKeyHex.c_str(),
            devicePublicKeyHex.c_str(),
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
        String encryptedMessage = executeEncryptMessageNip44(plaintext, userPrivateKeyHex, thirdPartyPubKey);
        String responseMsg = "{\"id\":\"" + requestId + "\",\"result\":\"" + encryptedMessage + "\"}";
        
        String encryptedResponse = nostr::getEncryptedDm(
            devicePrivateKeyHex.c_str(),
            devicePublicKeyHex.c_str(),
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
        String decryptedMessage = executeDecryptMessageNip44(cipherText, userPrivateKeyHex, thirdPartyPubKey);
        String responseMsg = "{\"id\":\"" + requestId + "\",\"result\":\"" + decryptedMessage + "\"}";
        
        String encryptedResponse = nostr::getEncryptedDm(
            devicePrivateKeyHex.c_str(),
            devicePublicKeyHex.c_str(),
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
        bool isAuthorised = authorizedClients.indexOf(clientPubKey) != -1;
        if(!isAuthorised) {
            Serial.println("RemoteSigner::isClientAuthorized() - Client not found in authorized list: " + String(clientPubKey));
            UI::showErrorToast("Client not authorised");
        }
        return isAuthorised;
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
            UI::showSuccessToast("Client authorised");
            return true;
        }
        
        // Prompt user for authorization
        return promptUserForAuthorization(clientPubKey);
    }
    
    bool promptUserForAuthorization(const String& requestingNpub) {
        Serial.println("RemoteSigner::promptUserForAuthorization() - Prompting user for: " + requestingNpub);
        
        // For now, reject all clients that dont have the correct secret
        // TODO: Implement UI prompt for user approval
        return false;
    }
    
    void addAuthorizedClient(const char* clientPubKey) {
        if (authorizedClients.indexOf(clientPubKey) == -1) {
            // Check if we need to make space
            int currentCount = getAuthorizedClientCount();
            
            if (currentCount >= MAX_AUTHORIZED_CLIENTS) {
                // Remove oldest client (LRU - first one in the list)
                removeOldestClient();
                Serial.println("RemoteSigner::addAuthorizedClient() - Removed oldest client to make space");
            }
            
            if (authorizedClients.length() > 0) {
                authorizedClients += "|";
            }
            authorizedClients += clientPubKey;
            saveConfigToPreferences();
            Serial.println("RemoteSigner::addAuthorizedClient() - Client authorized: " + String(clientPubKey));
            Serial.println("Total authorized clients: " + String(getAuthorizedClientCount()));
        }
    }
    
    int getAuthorizedClientCount() {
        if (authorizedClients.length() == 0) return 0;
        
        int count = 1;
        for (int i = 0; i < authorizedClients.length(); i++) {
            if (authorizedClients.charAt(i) == '|') {
                count++;
            }
        }
        return count;
    }
    
    void removeOldestClient() {
        if (authorizedClients.length() == 0) return;
        
        int firstSeparator = authorizedClients.indexOf('|');
        if (firstSeparator == -1) {
            // Only one client, remove it entirely
            authorizedClients = "";
        } else {
            // Remove first client and its separator
            authorizedClients = authorizedClients.substring(firstSeparator + 1);
        }
        
        Serial.println("RemoteSigner::removeOldestClient() - Removed oldest client");
    }
    
    bool trySaveConfigToPreferences() {
        Preferences prefs;
        if (!prefs.begin("signer", false)) { // Read-write
            Serial.println("RemoteSigner::trySaveConfigToPreferences() - Failed to open preferences");
            return false;
        }
        
        bool success = true;
        
        // Try to save each setting
        if (prefs.putString("relay_url", relayUrl) == 0) {
            Serial.println("RemoteSigner::trySaveConfigToPreferences() - Failed to save relay_url");
            success = false;
        }
        
        if (prefs.putString("user_private_key", userPrivateKeyHex) == 0) {
            Serial.println("RemoteSigner::trySaveConfigToPreferences() - Failed to save user_private_key");
            success = false;
        }
        
        if (prefs.putString("user_public_key", userPublicKeyHex) == 0) {
            Serial.println("RemoteSigner::trySaveConfigToPreferences() - Failed to save user_public_key");
            success = false;
        }
        
        if (prefs.putString("auth_clients", authorizedClients) == 0) {
            Serial.println("RemoteSigner::trySaveConfigToPreferences() - Failed to save auth_clients (size: " + String(authorizedClients.length()) + " chars)");
            success = false;
        }
        
        prefs.end();
        
        if (success) {
            Serial.println("RemoteSigner::trySaveConfigToPreferences() - Configuration saved successfully");
        }
        
        return success;
    }
    
    void clearAllAuthorizedClients() {
        authorizedClients = "";
        saveConfigToPreferences();
        Serial.println("RemoteSigner::clearAllAuthorizedClients() - All authorized clients cleared");
    }
    
    unsigned long wsLoopCounter = 0;
    void processLoop() {
        if (!signer_initialized || WiFiManager::isBackgroundOperationsPaused()) {
            return;
        }
        
        // Only update time and process WebSocket if WiFi is connected
        if (WiFiManager::isConnected()) {
            // Update time only every 30 seconds
            unsigned long now = millis();
            if (lastTimeUpdate == 0 || now - lastTimeUpdate >= 30000) {
                timeClient.update();
                unixTimestamp = timeClient.getEpochTime();
                lastTimeUpdate = now;
            }
            
            // Process WebSocket events
            webSocket.loop();
            // Serial.println("WS loop iteration: " + String(wsLoopCounter++));
            
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
    // Legacy compatibility functions (map to user keypair)
    String getPrivateKey() { return userPrivateKeyHex; }
    void setPrivateKey(const String& privKeyHex) { setUserPrivateKey(privKeyHex); }
    String getPublicKey() { return userPublicKeyHex; }
    void setStatusLabel(lv_obj_t* label) { 
        status_label = label; 
        // Update status immediately after setting the label
        displayConnectionStatus(isConnected());
    }
    
    // New keypair management functions
    void generateDeviceKeypair() {
        // Generate random 32-byte private key
        devicePrivateKeyHex = "";
        for (int i = 0; i < 64; i++) {
            devicePrivateKeyHex += "0123456789abcdef"[esp_random() % 16];
        }
        
        // Derive public key from private key
        try {
            int byteSize = 32;
            byte privateKeyBytes[byteSize];
            fromHex(devicePrivateKeyHex, privateKeyBytes, byteSize);
            PrivateKey privKey(privateKeyBytes);
            PublicKey pub = privKey.publicKey();
            devicePublicKeyHex = pub.toString();
            // remove leading 2 bytes from public key
            devicePublicKeyHex = devicePublicKeyHex.substring(2);
            Serial.println("RemoteSigner::generateDeviceKeypair() - Generated device keypair");
            Serial.println("Device public key: " + devicePublicKeyHex);
        } catch (...) {
            Serial.println("RemoteSigner: ERROR - Failed to generate device keypair");
        }
    }
    
    // Updated getter/setter functions
    String getUserPrivateKey() { return userPrivateKeyHex; }
    void setUserPrivateKey(const String& privKeyHex) { 
        userPrivateKeyHex = privKeyHex; 
        // Derive public key automatically
        if (privKeyHex.length() == 64) {
            try {
                int byteSize = 32;
                byte privateKeyBytes[byteSize];
                fromHex(privKeyHex, privateKeyBytes, byteSize);
                PrivateKey privKey(privateKeyBytes);
                PublicKey pub = privKey.publicKey();
                userPublicKeyHex = pub.toString();
                // remove leading 2 bytes from public key
                userPublicKeyHex = userPublicKeyHex.substring(2);
                Serial.println("RemoteSigner: Derived user public key: " + userPublicKeyHex);
            } catch (...) {
                Serial.println("RemoteSigner: ERROR - Failed to derive user public key");
            }
        }
    }
    String getUserPublicKey() { return userPublicKeyHex; }
    String getDevicePublicKey() { return devicePublicKeyHex; }
}