#include "nostr.h"
#include "nip44/nip44.h"

namespace nostr
{
    // ECDH caching for performance optimization
    struct ECDHCacheEntry {
        String privateKeyHex;
        String publicKeyHex;
        byte sharedSecret[32];
        unsigned long timestamp;
    };
    
    const int ECDH_CACHE_SIZE = 8;
    const unsigned long ECDH_CACHE_TTL_MS = 300000; // 5 minutes
    ECDHCacheEntry ecdhCache[ECDH_CACHE_SIZE];
    int ecdhCacheIndex = 0;
    bool ecdhCacheInitialized = false;
    
    // Key object caching for performance optimization
    struct KeyCacheEntry {
        String keyHex;
        PrivateKey* privateKey;
        PublicKey* publicKey;
        bool isPrivateKey;
        unsigned long timestamp;
    };
    
    const int KEY_CACHE_SIZE = 6;
    const unsigned long KEY_CACHE_TTL_MS = 300000; // 5 minutes
    KeyCacheEntry keyCache[KEY_CACHE_SIZE];
    int keyCacheIndex = 0;
    bool keyCacheInitialized = false;
    
    void initECDHCache() {
        if (!ecdhCacheInitialized) {
            for (int i = 0; i < ECDH_CACHE_SIZE; i++) {
                ecdhCache[i].privateKeyHex = "";
                ecdhCache[i].publicKeyHex = "";
                ecdhCache[i].timestamp = 0;
            }
            ecdhCacheInitialized = true;
        }
    }
    
    void initKeyCache() {
        if (!keyCacheInitialized) {
            for (int i = 0; i < KEY_CACHE_SIZE; i++) {
                keyCache[i].keyHex = "";
                keyCache[i].privateKey = nullptr;
                keyCache[i].publicKey = nullptr;
                keyCache[i].isPrivateKey = false;
                keyCache[i].timestamp = 0;
            }
            keyCacheInitialized = true;
        }
    }
    
    PrivateKey* getCachedPrivateKey(const String& keyHex) {
        initKeyCache();
        unsigned long currentTime = millis();
        
        for (int i = 0; i < KEY_CACHE_SIZE; i++) {
            if (keyCache[i].keyHex == keyHex && 
                keyCache[i].isPrivateKey &&
                keyCache[i].privateKey != nullptr &&
                (currentTime - keyCache[i].timestamp) < KEY_CACHE_TTL_MS) {
                return keyCache[i].privateKey;
            }
        }
        return nullptr;
    }
    
    PublicKey* getCachedPublicKey(const String& keyHex) {
        initKeyCache();
        unsigned long currentTime = millis();
        
        for (int i = 0; i < KEY_CACHE_SIZE; i++) {
            if (keyCache[i].keyHex == keyHex && 
                !keyCache[i].isPrivateKey &&
                keyCache[i].publicKey != nullptr &&
                (currentTime - keyCache[i].timestamp) < KEY_CACHE_TTL_MS) {
                return keyCache[i].publicKey;
            }
        }
        return nullptr;
    }
    
    void storePrivateKeyInCache(const String& keyHex, PrivateKey* privateKey) {
        initKeyCache();
        
        // Clean up old entry if overwriting
        if (keyCache[keyCacheIndex].privateKey != nullptr) {
            delete keyCache[keyCacheIndex].privateKey;
        }
        if (keyCache[keyCacheIndex].publicKey != nullptr) {
            delete keyCache[keyCacheIndex].publicKey;
        }
        
        keyCache[keyCacheIndex].keyHex = keyHex;
        keyCache[keyCacheIndex].privateKey = privateKey;
        keyCache[keyCacheIndex].publicKey = nullptr;
        keyCache[keyCacheIndex].isPrivateKey = true;
        keyCache[keyCacheIndex].timestamp = millis();
        
        keyCacheIndex = (keyCacheIndex + 1) % KEY_CACHE_SIZE;
    }
    
    void storePublicKeyInCache(const String& keyHex, PublicKey* publicKey) {
        initKeyCache();
        
        // Clean up old entry if overwriting
        if (keyCache[keyCacheIndex].privateKey != nullptr) {
            delete keyCache[keyCacheIndex].privateKey;
        }
        if (keyCache[keyCacheIndex].publicKey != nullptr) {
            delete keyCache[keyCacheIndex].publicKey;
        }
        
        keyCache[keyCacheIndex].keyHex = keyHex;
        keyCache[keyCacheIndex].privateKey = nullptr;
        keyCache[keyCacheIndex].publicKey = publicKey;
        keyCache[keyCacheIndex].isPrivateKey = false;
        keyCache[keyCacheIndex].timestamp = millis();
        
        keyCacheIndex = (keyCacheIndex + 1) % KEY_CACHE_SIZE;
    }
    
    bool getECDHFromCache(const String& privateKeyHex, const String& publicKeyHex, byte* sharedSecret) {
        initECDHCache();
        unsigned long currentTime = millis();
        
        for (int i = 0; i < ECDH_CACHE_SIZE; i++) {
            if (ecdhCache[i].privateKeyHex == privateKeyHex && 
                ecdhCache[i].publicKeyHex == publicKeyHex &&
                (currentTime - ecdhCache[i].timestamp) < ECDH_CACHE_TTL_MS) {
                memcpy(sharedSecret, ecdhCache[i].sharedSecret, 32);
                return true;
            }
        }
        return false;
    }
    
    void storeECDHInCache(const String& privateKeyHex, const String& publicKeyHex, const byte* sharedSecret) {
        initECDHCache();
        
        ecdhCache[ecdhCacheIndex].privateKeyHex = privateKeyHex;
        ecdhCache[ecdhCacheIndex].publicKeyHex = publicKeyHex;
        memcpy(ecdhCache[ecdhCacheIndex].sharedSecret, sharedSecret, 32);
        ecdhCache[ecdhCacheIndex].timestamp = millis();
        
        ecdhCacheIndex = (ecdhCacheIndex + 1) % ECDH_CACHE_SIZE;
    }

    DynamicJsonDocument nostrEventDoc(0);
    byte *encryptedMessageBin;

    unsigned long timer = 0;
    void _startTimer(const char *timedEvent)
    {
        timer = millis();
        // Serial.print("Starting timer for ");
        // Serial.println(timedEvent);
    }

    void _stopTimer(const char *timedEvent)
    {
        unsigned long elapsedTime = millis() - timer;
        Serial.print(elapsedTime);
        Serial.print(" ms - ");
        Serial.println(timedEvent);
        timer = millis();
    }

    void initMemorySpace(size_t nostrEventDocCapacity, size_t encryptedMessageBinSize)
    {
        nostrEventDoc = DynamicJsonDocument(nostrEventDocCapacity);
        encryptedMessageBin = (byte *)malloc(encryptedMessageBinSize);
    }

    void _logToSerialWithTitle(String title, String message)
    {
        Serial.println(title + ": " + message);
    }

    void _logOkWithHeapSize(const char *message)
    {
        Serial.print(message);
        Serial.print(" OK.");
        Serial.print(" Free heap size: ");
        Serial.println(esp_get_free_heap_size());
    }

    String decryptData(byte key[32], byte iv[16], byte *encryptedMessageBin, int byteSize)
    {
        if (!encryptedMessageBin)
        {
            Serial.println("Invalid encryptedMessageBin");
            return ""; // Handle invalid input
        }

        // The encryptedMessageBin is already a byte pointer to the binary data
        // and byteSize is the size of this binary data

        AES_ctx ctx;
        AES_init_ctx_iv(&ctx, key, iv);
        AES_CBC_decrypt_buffer(&ctx, encryptedMessageBin, byteSize); // Use byteSize directly

        // //_logOkWithHeapSize("Decrypted AES data");

        // Convert the decrypted data to a String. Ensure that the data is null-terminated if treating as a C-string.
        // If the decrypted data may not be null-terminated or might contain null bytes within,
        // you'll need a different strategy to create the String object correctly.
        String decryptedData;
        for (int i = 0; i < byteSize; i++)
        {
            if (encryptedMessageBin[i] != '\0') // Assuming the decrypted text does not include null characters
                decryptedData += (char)encryptedMessageBin[i];
            else
                break; // Stop if a null character is found
        }

        // Note: No need to free encryptedMessageBin here if it's managed outside this function

        return decryptedData;
    }

    String decryptNip04Ciphertext(String &cipherText, String privateKeyHex, String senderPubKeyHex)
    {
        _startTimer("decryptNip04Ciphertext");
        String encryptedMessage = cipherText.substring(0, cipherText.indexOf("?iv="));
        // //_logOkWithHeapSize("Got encryptedMessage");
        int encryptedMessageSize = (encryptedMessage.length() * 3) / 4;
        fromBase64(encryptedMessage, encryptedMessageBin, encryptedMessageSize); // Assuming fromBase64 modifies encryptedMessageSize to actual decoded size

        String iv = cipherText.substring(cipherText.indexOf("?iv=") + 4);
        // calculate the ivSize for ivBin based on the iv size
        int ivSize = (iv.length() * 3) / 4;
        byte ivBin[ivSize];
        fromBase64(iv, ivBin, ivSize);
        _logToSerialWithTitle("iv", iv);
        _stopTimer("decryptNip04Ciphertext: Got ivBin");

        PrivateKey* cachedPrivateKey = getCachedPrivateKey(privateKeyHex);
        PrivateKey privateKey;
        
        if (cachedPrivateKey != nullptr) {
            privateKey = *cachedPrivateKey;
            _stopTimer("decryptNip04Ciphertext: Got privateKey (cached)");
        } else {
            int byteSize = 32;
            byte privateKeyBytes[byteSize];
            fromHex(privateKeyHex, privateKeyBytes, byteSize);
            privateKey = PrivateKey(privateKeyBytes);
            // Store in cache for future use
            storePrivateKeyInCache(privateKeyHex, new PrivateKey(privateKeyBytes));
            _stopTimer("decryptNip04Ciphertext: Got privateKey (computed)");
        }

        _logToSerialWithTitle("senderPubKeyHex", senderPubKeyHex);
        String fullPubKeyHex = "02" + String(senderPubKeyHex);
        PublicKey* cachedPublicKey = getCachedPublicKey(fullPubKeyHex);
        PublicKey senderPublicKey;
        
        if (cachedPublicKey != nullptr) {
            senderPublicKey = *cachedPublicKey;
            _stopTimer("decryptNip04Ciphertext: Got senderPublicKey (cached)");
        } else {
            byte senderPublicKeyBin[64];
            fromHex(fullPubKeyHex, senderPublicKeyBin, 64);
            senderPublicKey = PublicKey(senderPublicKeyBin);
            // Store in cache for future use
            storePublicKeyInCache(fullPubKeyHex, new PublicKey(senderPublicKeyBin));
            _stopTimer("decryptNip04Ciphertext: Got senderPublicKey (computed)");
        }
        _logToSerialWithTitle("senderPublicKey.toString() is", senderPublicKey.toString());

        byte sharedPointX[32];
        
        // Try to get ECDH result from cache first
        if (getECDHFromCache(privateKeyHex, senderPubKeyHex, sharedPointX)) {
            _stopTimer("decryptNip04Ciphertext: Got sharedPointX (cached)");
        } else {
            privateKey.ecdh(senderPublicKey, sharedPointX, false);
            // Store result in cache for future use
            storeECDHInCache(privateKeyHex, senderPubKeyHex, sharedPointX);
            _stopTimer("decryptNip04Ciphertext: Got sharedPointX (computed)");
        }
        
        String sharedPointXHex = toHex(sharedPointX, sizeof(sharedPointX));
        _logToSerialWithTitle("sharedPointXHex is", sharedPointXHex);

        String message = decryptData(sharedPointX, ivBin, encryptedMessageBin, encryptedMessageSize);
        message.trim();
        _stopTimer("decryptNip04Ciphertext: Got message");

        _logToSerialWithTitle("message", message);

        return message;
    }

    String getContent(const String &serialisedJson)
    {
        DeserializationError error = deserializeJson(nostrEventDoc, serialisedJson);
        if (error)
        {
            Serial.print(F("deserializeJson() failed: "));
            Serial.println(error.c_str());
        }
        return nostrEventDoc[2]["content"];
    }

    String getSenderPubKeyHex(const String &serialisedJson)
    {
        DeserializationError error = deserializeJson(nostrEventDoc, serialisedJson);
        if (error)
        {
            Serial.print(F("deserializeJson() failed: "));
            Serial.println(error.c_str());
        }
        return nostrEventDoc[2]["pubkey"];
    }

    std::pair<String, String> getPubKeyAndContent(const String &serialisedJson)
    {
        DeserializationError error = deserializeJson(nostrEventDoc, serialisedJson);
        if (error)
        {
            Serial.print(F("deserializeJson() failed: "));
            Serial.println(error.c_str());
            return std::make_pair("", "");
        }

        // Extract the sender public key and content from the document
        String senderPubKeyHex = nostrEventDoc[2]["pubkey"];
        String content = nostrEventDoc[2]["content"];

        // Return both values as a pair
        return std::make_pair(senderPubKeyHex, content);
    }

    String nip04Decrypt(const char *privateKeyHex, String serialisedJson)
    {
        _startTimer("nip04Decrypt: nip04Decrypt");
        auto result = getPubKeyAndContent(serialisedJson);
        String senderPubKeyHex = result.first;
        String content = result.second;
        _stopTimer("nip04Decrypt: Got result from getPubKeyAndContent");

        int ivIndex = content.indexOf("?iv=");
        if (ivIndex == -1)
        {
            Serial.println("IV not found in content");
            return "";
        }

        // Extracting encryptedMessage directly from content without using String for the intermediate steps
        int encryptedMessageLength = ivIndex; // Length of the encrypted message part
        const char *encryptedMessage = content.c_str(); // Use the content directly
        if (!encryptedMessage)
        {
            Serial.println("Failed to allocate PSRAM for encryptedMessage");
            return "";
        }

        int encryptedMessageSize = encryptedMessageLength / 4 * 3; // Base64 encoded size is 4/3 of the binary size

        fromBase64(encryptedMessage, encryptedMessageBin, encryptedMessageSize);
        _stopTimer("nip04Decrypt: Got encryptedMessageBin");

        _logToSerialWithTitle("encryptedMessage", encryptedMessage);

        PrivateKey* cachedPrivateKey = getCachedPrivateKey(String(privateKeyHex));
        PrivateKey privateKey;
        
        if (cachedPrivateKey != nullptr) {
            privateKey = *cachedPrivateKey;
            _stopTimer("nip04Decrypt: Got privateKey (cached)");
        } else {
            int byteSize = 32;
            byte privateKeyBytes[byteSize];
            fromHex(privateKeyHex, privateKeyBytes, byteSize);
            privateKey = PrivateKey(privateKeyBytes);
            // Store in cache for future use
            storePrivateKeyInCache(String(privateKeyHex), new PrivateKey(privateKeyBytes));
            _stopTimer("nip04Decrypt: Got privateKey (computed)");
        }

        _logToSerialWithTitle("senderPubKeyHex", senderPubKeyHex);
        String fullPubKeyHex = "02" + String(senderPubKeyHex);
        PublicKey* cachedPublicKey = getCachedPublicKey(fullPubKeyHex);
        PublicKey senderPublicKey;
        
        if (cachedPublicKey != nullptr) {
            senderPublicKey = *cachedPublicKey;
            _stopTimer("nip04Decrypt: Got senderPublicKey (cached)");
        } else {
            byte senderPublicKeyBin[64];
            fromHex(fullPubKeyHex, senderPublicKeyBin, 64);
            senderPublicKey = PublicKey(senderPublicKeyBin);
            // Store in cache for future use
            storePublicKeyInCache(fullPubKeyHex, new PublicKey(senderPublicKeyBin));
            _stopTimer("nip04Decrypt: Got senderPublicKey (computed)");
        }
        _logToSerialWithTitle("senderPublicKey.toString() is", senderPublicKey.toString());

        return decryptNip04Ciphertext(content, privateKeyHex, senderPubKeyHex);
    }

    String nip44Decrypt(const char *privateKeyHex, String serialisedJson)
    {
        _startTimer("nip44Decrypt: nip44Decrypt");
        auto result = getPubKeyAndContent(serialisedJson);
        Serial.println("nip44Decrypt: result is: " + result.first + " " + result.second);
        String senderPubKeyHex = result.first;
        Serial.println("nip44Decrypt: senderPubKeyHex is: " + senderPubKeyHex);
        String content = result.second;
        Serial.println("nip44Decrypt: content is: " + content);
        _stopTimer("nip44Decrypt: Got result from getPubKeyAndContent");

        return executeDecryptMessageNip44(content, privateKeyHex, senderPubKeyHex);
    }

    String nip44Encrypt(const char *privateKeyHex, String serialisedJson)
    {
        _startTimer("nip44Encrypt: nip44Encrypt");
        auto result = getPubKeyAndContent(serialisedJson);
        String senderPubKeyHex = result.first;
        String content = result.second;
        _stopTimer("nip44Encrypt: Got result from getPubKeyAndContent");
        return executeEncryptMessageNip44(content, privateKeyHex, senderPubKeyHex);
    }

    /**
     * @brief Get a Note object
     *
     * @param privateKeyHex
     * @param pubKeyHex
     * @param timestamp
     * @param content
     * @param kind
     * @param tags
     * @return String
     */
    String getNote(char const *privateKeyHex, char const *pubKeyHex, unsigned long timestamp, String &content, uint16_t kind, String tags)
    {
        _startTimer("getNote");
        // convert
        // log timestamp
        _logToSerialWithTitle("timestamp is: ", String(timestamp));
        // escape any double quotes in content
        content.replace("\"", "\\\"");
        // replace new lines with \n
        content.replace("\n", "\\n");
        // replace carriage returns with \r
        content.replace("\r", "\\r");
        // replace tabs with \t
        content.replace("\t", "\\t");
        String message = "[0,\"" + String(pubKeyHex) + "\"," + String(timestamp) + "," + String(kind) + "," + tags + ",\"" + content + "\"]";
        _logToSerialWithTitle("message is: ", message);

        // sha256 of message converted to hex, assign to msghash
        byte hash[64] = {0}; // hash
        int hashLen = 0;

        // Get the sha256 hash of the message
        hashLen = sha256(message, hash);
        _stopTimer("get sha256 hash of message");
        String msgHash = toHex(hash, hashLen);
        _stopTimer("get msgHash as hex");
        _logToSerialWithTitle("SHA-256: ", msgHash);

        // Create the private key object
        PrivateKey* cachedPrivateKey = getCachedPrivateKey(String(privateKeyHex));
        PrivateKey privateKey;
        
        if (cachedPrivateKey != nullptr) {
            privateKey = *cachedPrivateKey;
            _stopTimer("create privateKey object (cached)");
        } else {
            int byteSize = 32;
            byte privateKeyBytes[byteSize];
            fromHex(privateKeyHex, privateKeyBytes, byteSize);
            _stopTimer("convert privateKeyHex to byte array");
            privateKey = PrivateKey(privateKeyBytes);
            // Store in cache for future use
            storePrivateKeyInCache(String(privateKeyHex), new PrivateKey(privateKeyBytes));
            _stopTimer("create privateKey object (computed)");
        }

        // Generate the schnorr sig of the messageHash
        int byteSize = 32;
        byte messageBytes[byteSize];
        fromHex(msgHash, messageBytes, byteSize);
        _stopTimer("convert msgHash to byte array");
        SchnorrSignature signature = privateKey.schnorr_sign(messageBytes);
        _stopTimer("generate schnorr sig");
        String signatureHex = String(signature);
        _logToSerialWithTitle("Schnorr sig is: ", signatureHex);

        // Device the public key and verify the schnorr sig is valid
        PublicKey pub = privateKey.publicKey();
        _stopTimer("get public key");

        // if (pub.schnorr_verify(signature, messageBytes))
        // {
        //     _logToSerialWithTitle("All good, signature is valid", "");
        // }
        // else
        // {
        //     _logToSerialWithTitle("Something went wrong, signature is invalid", "");
        // }
        _stopTimer("verify schnorr sig");

        String serialisedDataString = "{\"id\":\"" + msgHash + "\",\"pubkey\":\"" + String(pubKeyHex) + "\",\"created_at\":" + String(timestamp) + ",\"kind\":" + String(kind) + ",\"tags\":" + tags + ",\"content\":\"" + content + "\",\"sig\":\"" + signatureHex + "\"}";
        _logToSerialWithTitle("serialisedEventDataString is: ", String(serialisedDataString));

        // Print the JSON to the serial monitor
        _logToSerialWithTitle("Event JSON", serialisedDataString);
        return serialisedDataString;
    }

    /**
     * @brief Convert a string to a byte array
     *
     * @param input
     * @param padding_diff
     * @param output
     */
    void stringToByteArray(const char *input, int padding_diff, byte *output)
    {
        int i = 0;
        // remove end-of-string char
        while (input[i] != '\0')
        {
            output[i] = input[i];
            i++;
        }

        // pad between 1 and 16 bytes
        for (int j = 0; j < padding_diff; j++)
        {
            output[i + j] = padding_diff;
        }
    }

    /**
     * @brief encrypt data using AES-256-CBC
     *
     * @param key
     * @param iv
     * @param msg
     * @return String
     */
    String encryptData(byte key[32], byte iv[16], String &msg)
    {
        // message has to be padded at the end so it is a multiple of 16
        int padding_diff = msg.length() % 16 == 0 ? 16 : 16 - (msg.length() % 16);

        int byteSize = msg.length() + padding_diff;
        byte *messageBin = (byte *)malloc(byteSize);

        if (messageBin == nullptr)
        {
            Serial.println("Failed to allocate PSRAM");
            return "";
        }

        stringToByteArray(msg.c_str(), padding_diff, messageBin);

        AES_ctx ctx;
        AES_init_ctx_iv(&ctx, key, iv);

        AES_CBC_encrypt_buffer(&ctx, messageBin, byteSize);

        return toHex(messageBin, byteSize);
    }

    /**
     * @brief Get the cipher text for a nip4 message
     *
     * @param privateKeyHex
     * @param recipientPubKeyHex
     * @param content
     * @return String
     */
    String getCipherText(const char *privateKeyHex, const char *recipientPubKeyHex, String &content)
    {
        _startTimer("getCipherText");
        // Get shared point
        // Create the private key object
        PrivateKey* cachedPrivateKey = getCachedPrivateKey(String(privateKeyHex));
        PrivateKey privateKey;
        
        if (cachedPrivateKey != nullptr) {
            privateKey = *cachedPrivateKey;
            _stopTimer("getCipherText: create privateKey object (cached)");
        } else {
            int byteSize = 32;
            byte privateKeyBytes[byteSize];
            fromHex(privateKeyHex, privateKeyBytes, byteSize);
            privateKey = PrivateKey(privateKeyBytes);
            // Store in cache for future use
            storePrivateKeyInCache(String(privateKeyHex), new PrivateKey(privateKeyBytes));
            _stopTimer("getCipherText: create privateKey object (computed)");
        }

        String fullRecipientPubKeyHex = "02" + String(recipientPubKeyHex);
        PublicKey* cachedPublicKey = getCachedPublicKey(fullRecipientPubKeyHex);
        PublicKey otherDhPublicKey;
        
        if (cachedPublicKey != nullptr) {
            otherDhPublicKey = *cachedPublicKey;
            _stopTimer("getCipherText: create otherDhPublicKey object (cached)");
        } else {
            byte publicKeyBin[64];
            fromHex(fullRecipientPubKeyHex, publicKeyBin, 64);
            otherDhPublicKey = PublicKey(publicKeyBin);
            // Store in cache for future use
            storePublicKeyInCache(fullRecipientPubKeyHex, new PublicKey(publicKeyBin));
            _stopTimer("getCipherText: create otherDhPublicKey object (computed)");
        }
        _logToSerialWithTitle("otherDhPublicKey.toString() is", otherDhPublicKey.toString());

        byte sharedPointX[32];
        
        // Try to get ECDH result from cache first
        if (getECDHFromCache(String(privateKeyHex), String(recipientPubKeyHex), sharedPointX)) {
            _stopTimer("getCipherText: get sharedPointX (cached)");
        } else {
            privateKey.ecdh(otherDhPublicKey, sharedPointX, false);
            // Store result in cache for future use
            storeECDHInCache(String(privateKeyHex), String(recipientPubKeyHex), sharedPointX);
            _stopTimer("getCipherText: get sharedPointX (computed)");
        }
        
        String sharedPointXHex = toHex(sharedPointX, sizeof(sharedPointX));
        _logToSerialWithTitle("sharedPointXHex is", sharedPointXHex);

        // Create the initialization vector
        uint8_t iv[16];
        esp_random() % 256;
        for (int i = 0; i < sizeof(iv); i++)
        {
            iv[i] = esp_random() % 256;
        }
        _stopTimer("getCipherText: create iv");

        String ivHex = toHex(iv, 16);
        String ivBase64 = hexToBase64(ivHex);
        _stopTimer("getCipherText: get ivBase64");

        String encryptedMessageHex = encryptData(sharedPointX, iv, content);
        _stopTimer("getCipherText: get encryptedMessageHex");

        // divide the length of the hex string 2 to get the size of the byte array, since each byte consists of 2 hexadecimal characters.
        int encryptedMessageSize = encryptedMessageHex.length() / 2;
        // Allocate memory for the encrypted message array
        uint8_t *encryptedMessage = (uint8_t *)malloc(encryptedMessageSize);
        if (encryptedMessage == nullptr)
        {
            Serial.println("Failed to allocate PSRAM for encryptedMessage");
        }
        fromHex(encryptedMessageHex, encryptedMessage, encryptedMessageSize);
        _stopTimer("getCipherText: get encryptedMessage fromHex");

        String encryptedMessageBase64 = hexToBase64(encryptedMessageHex);
        _stopTimer("getCipherText: get encryptedMessageBase64");

        encryptedMessageBase64 += "?iv=" + ivBase64;
        return encryptedMessageBase64;
    }

    /**
     * @brief Get the Serialised Encrypted Dm Object
     *
     * @param pubKeyHex
     * @param recipientPubKeyHex
     * @param kind
     * @param msgHash
     * @param timestamp
     * @param encryptedMessageWithIv
     * @param schnorrSig
     * @return String
     */
    String getSerialisedEncryptedDmObject(const char *pubKeyHex, const char *recipientPubKeyHex, uint16_t kind, String &msgHash, int timestamp, String &encryptedMessageWithIv, String &schnorrSig)
    {
        // parse a JSON array
        String serialisedTagsArray = "[[\"p\",\"" + String(recipientPubKeyHex) + "\"]]";
        // _logToSerialWithTitle("serialisedTagsArray is: ", serialisedTagsArray);

        // create the serialised fullEvent string using sprintf instead of the Arduino JSON library
        return "[\"EVENT\",{\"id\":\"" + msgHash + "\",\"pubkey\":\"" + pubKeyHex + "\",\"created_at\":" + String(timestamp) + ",\"kind\":" + String(kind) + ",\"tags\":" + serialisedTagsArray + ",\"content\":\"" + encryptedMessageWithIv + "\",\"sig\":\"" + schnorrSig + "\"}]";
    }

    String getSerialisedEncryptedDmArray(char const *pubKeyHex, char const *recipientPubKeyHex, uint16_t kind, int timestamp, String &encryptedMessageWithIv)
    {
        String serialisedTagsArray = "[[\"p\",\"" + String(recipientPubKeyHex) + "\"]]";
        String message = "[0,\"" + String(pubKeyHex) + "\"," + String(timestamp) + "," + String(kind) + "," + serialisedTagsArray + ",\"" + encryptedMessageWithIv + "\"]";

        // _logToSerialWithTitle("message is: ", message);

        return message;
    }

    /**
     * @brief Get the Encrypted Dm object
     *
     * @param privateKeyHex
     * @param pubKeyHex
     * @param recipientPubKeyHex
     * @param kind
     * @param timestamp
     * @param content
     * @param type "nip44" or "nip04"
     * @return String
     */
    String getEncryptedDm(char const *privateKeyHex, char const *pubKeyHex, char const *recipientPubKeyHex, uint16_t kind, unsigned long timestamp, String content, String type)
    {
        String encryptedMessageBase64 = "";
        if (type == "nip44") {
            _startTimer("getEncrypted NIP44 Dm");
            encryptedMessageBase64 = executeEncryptMessageNip44(content, privateKeyHex, recipientPubKeyHex);
            Serial.println("NIP44 encrypted message: " + encryptedMessageBase64);
            _stopTimer("executeEncryptMessageNip44");
        } else {
            _startTimer("getEncrypted NIP44 Dm");
            encryptedMessageBase64 = getCipherText(privateKeyHex, recipientPubKeyHex, content);
            _stopTimer("getCipherText");
        }

        String message = nostr::getSerialisedEncryptedDmArray(pubKeyHex, recipientPubKeyHex, kind, timestamp, encryptedMessageBase64);
        _stopTimer("get serialised encrypted dm array");

        byte hash[64] = {0}; // hash
        int hashLen = 0;

        // Get the sha256 hash of the message
        hashLen = sha256(message, hash);
        String msgHash = toHex(hash, hashLen);
        _logToSerialWithTitle("SHA-256:", msgHash);
        _stopTimer("get sha256 hash of message");

        PrivateKey* cachedPrivateKey = getCachedPrivateKey(String(privateKeyHex));
        PrivateKey privateKey;
        
        if (cachedPrivateKey != nullptr) {
            privateKey = *cachedPrivateKey;
            _stopTimer("create privateKey object (cached)");
        } else {
            int byteSize = 32;
            byte privateKeyBytes[byteSize];
            fromHex(privateKeyHex, privateKeyBytes, byteSize);
            _stopTimer("get privateKeyBytes from hex");
            privateKey = PrivateKey(privateKeyBytes);
            // Store in cache for future use
            storePrivateKeyInCache(String(privateKeyHex), new PrivateKey(privateKeyBytes));
            _stopTimer("create privateKey object (computed)");
        }
        // Generate the schnorr sig of the messageHash
        SchnorrSignature signature = privateKey.schnorr_sign(hash);
        _stopTimer("generate schnorr sig");
        String signatureHex = String(signature);
        _logToSerialWithTitle("Schnorr sig is: ", signatureHex);

        String serialisedEventData = nostr::getSerialisedEncryptedDmObject(pubKeyHex, recipientPubKeyHex, kind, msgHash, timestamp, encryptedMessageBase64, signatureHex);
        _stopTimer("get serialised encrypted dm object");
        // _logToSerialWithTitle("serialisedEventData is", serialisedEventData);
        return serialisedEventData;
    }
}