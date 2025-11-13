#include "settings.h"
#include "app.h"
#include <Arduino.h>

#include "ui.h"
#include "wifi_manager.h"

namespace Settings {
    // Global preferences instance
    static Preferences preferences;
    
    // Settings data
    static String ap_password = "GoodMorning21";
    static String current_pin = "1234";
    
    // UI element references
    static lv_obj_t *settings_pin_btn = NULL;
    static lv_obj_t *settings_save_btn = NULL;
    static lv_obj_t *ap_password_textarea = NULL;
    
    // PIN management UI elements
    static lv_obj_t *pin_management_screen = NULL;
    static lv_obj_t *pin_verification_screen = NULL;
    static lv_obj_t *pin_current_textarea = NULL;
    static lv_obj_t *pin_new_textarea = NULL;
    static lv_obj_t *pin_verify_textarea = NULL;
    static lv_obj_t *pin_verification_textarea = NULL;
    static lv_obj_t *pin_keyboard = NULL;
    static lv_obj_t *pin_verification_keyboard = NULL;
    static lv_obj_t *pin_verification_status = NULL;
    
    // QR PIN verification UI elements
    static lv_obj_t *pin_verification_qr_screen = NULL;
    static lv_obj_t *pin_verification_qr_textarea = NULL;
    static lv_obj_t *pin_verification_qr_keyboard = NULL;
    static lv_obj_t *pin_verification_qr_status = NULL;
    
    void init() {
        loadFromPreferences();
    }
    
    void cleanup() {
        // Close any open preferences
        preferences.end();
        Serial.println("Settings module cleaned up");
    }
    
    void resetToDefaults() {
        preferences.begin("config", false);
        preferences.clear();
        ap_password = "GoodMorning21";
        current_pin = "1234";
        saveToPreferences();
        preferences.end();
        Serial.println("Settings reset to defaults");
    }
        
    String getAPPassword() {
        return ap_password;
    }
    
    void setAPPassword(const String& password) {
        ap_password = password;
    }
    
    void setCurrentPin(const String& pin) {
        current_pin = pin;
        preferences.begin("pin-config", false);
        preferences.putString("pin", pin);
        preferences.end();
        Serial.println("PIN saved to preferences");
    }
    
    bool verifyPin(const String& pin) {
        return pin == current_pin;
    }
        
    // Persistence
    void loadFromPreferences() {
        // Load shop settings
        preferences.begin("shop-config", true);
        ap_password = preferences.getString("ap_password", "GoodMorning21");
        preferences.end();
        
        // Load preferences
        preferences.begin("config", true);
        
        // Load PIN
        preferences.begin("pin-config", true);
        current_pin = preferences.getString("pin", "1234");
        preferences.end();
        Serial.println("PIN loaded from preferences");
    }
    
    void saveToPreferences() {
        preferences.begin("shop-config", false);
        if (ap_password_textarea != NULL) {
            ap_password = String(lv_textarea_get_text(ap_password_textarea));
        }
        preferences.putString("ap_password", ap_password);
        preferences.end();
        Serial.println("Saved settings");
    }
    
    // UI element setters
    void setSettingsUIElements(lv_obj_t *pin_btn, lv_obj_t *save_btn) {
        settings_pin_btn = pin_btn;
        settings_save_btn = save_btn;
    }
    
    void setAPPasswordTextArea(lv_obj_t *textarea) {
        ap_password_textarea = textarea;
    }
    
    // Settings event handlers
    void settingsSaveEventHandler(lv_event_t *e) {
        lv_event_code_t code = lv_event_get_code(e);
        if (code == LV_EVENT_CLICKED) {
            // Get the current shop name from the text area
            
            saveToPreferences();
            UI::showMessage("Settings Saved", "Shop settings have been saved successfully.");
        }
    }
    
    void settingsBackEventHandler(lv_event_t *e) {
        lv_event_code_t code = lv_event_get_code(e);
        if (code == LV_EVENT_CLICKED) {
            // This would need to be handled by the UI module
            // For now, just log that back was pressed
            Serial.println("Settings back button pressed");
        }
    }
    
    void apPasswordKBEventHandler(lv_event_t *e) {
        lv_event_code_t code = lv_event_get_code(e);
        lv_obj_t *kb = lv_event_get_target(e);
        
        if (code == LV_EVENT_READY) {
            lv_obj_t *ta = lv_keyboard_get_textarea(kb);
            const char *text = lv_textarea_get_text(ta);
            ap_password = String(text);
            
            Serial.println("AP password changed to: " + ap_password);
            
            lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);
            // Show the PIN and Save buttons when keyboard is hidden
            if (settings_pin_btn != NULL && lv_obj_is_valid(settings_pin_btn)) {
                lv_obj_clear_flag(settings_pin_btn, LV_OBJ_FLAG_HIDDEN);
            }
            if (settings_save_btn != NULL && lv_obj_is_valid(settings_save_btn)) {
                lv_obj_clear_flag(settings_save_btn, LV_OBJ_FLAG_HIDDEN);
            }
        } else if (code == LV_EVENT_CANCEL) {
            lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);
            // Show the PIN and Save buttons when keyboard is hidden
            if (settings_pin_btn != NULL && lv_obj_is_valid(settings_pin_btn)) {
                lv_obj_clear_flag(settings_pin_btn, LV_OBJ_FLAG_HIDDEN);
            }
            if (settings_save_btn != NULL && lv_obj_is_valid(settings_save_btn)) {
                lv_obj_clear_flag(settings_save_btn, LV_OBJ_FLAG_HIDDEN);
            }
        }
    }
    
    // PIN Management UI
    void showPinManagementScreen() {
        if (pin_management_screen != NULL) {
            lv_obj_del(pin_management_screen);
        }
        
        pin_management_screen = lv_obj_create(lv_scr_act());
        lv_obj_set_size(pin_management_screen, lv_pct(100), lv_pct(100));
        lv_obj_set_style_bg_color(pin_management_screen, lv_color_hex(0x000000), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(pin_management_screen, LV_OPA_100, LV_PART_MAIN);
        
        // Title
        lv_obj_t * title = lv_label_create(pin_management_screen);
        lv_label_set_text(title, "PIN Management");
        lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 20);
        lv_obj_set_style_text_font(title, &lv_font_montserrat_16, LV_PART_MAIN);
        
        // Current PIN field
        lv_obj_t * current_pin_label = lv_label_create(pin_management_screen);
        lv_label_set_text(current_pin_label, "Current PIN:");
        lv_obj_align(current_pin_label, LV_ALIGN_TOP_LEFT, 20, 80);
        lv_obj_set_style_text_color(current_pin_label, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
        
        pin_current_textarea = lv_textarea_create(pin_management_screen);
        lv_textarea_set_password_mode(pin_current_textarea, true);
        lv_textarea_set_one_line(pin_current_textarea, true);
        lv_obj_set_size(pin_current_textarea, 120, 40);
        lv_obj_align(pin_current_textarea, LV_ALIGN_TOP_RIGHT, -20, 70);
        lv_obj_add_event_cb(pin_current_textarea, pinCurrentKBEventHandler, LV_EVENT_CLICKED, NULL);
        
        // New PIN field
        lv_obj_t * new_pin_label = lv_label_create(pin_management_screen);
        lv_label_set_text(new_pin_label, "New PIN:");
        lv_obj_align(new_pin_label, LV_ALIGN_TOP_LEFT, 20, 140);
        lv_obj_set_style_text_color(new_pin_label, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
        
        pin_new_textarea = lv_textarea_create(pin_management_screen);
        lv_textarea_set_password_mode(pin_new_textarea, true);
        lv_textarea_set_one_line(pin_new_textarea, true);
        lv_obj_set_size(pin_new_textarea, 120, 40);
        lv_obj_align(pin_new_textarea, LV_ALIGN_TOP_RIGHT, -20, 130);
        lv_obj_add_event_cb(pin_new_textarea, pinNewKBEventHandler, LV_EVENT_CLICKED, NULL);
        
        // Verify PIN field
        lv_obj_t * verify_pin_label = lv_label_create(pin_management_screen);
        lv_label_set_text(verify_pin_label, "Verify PIN:");
        lv_obj_align(verify_pin_label, LV_ALIGN_TOP_LEFT, 20, 200);
        lv_obj_set_style_text_color(verify_pin_label, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
        
        pin_verify_textarea = lv_textarea_create(pin_management_screen);
        lv_textarea_set_password_mode(pin_verify_textarea, true);
        lv_textarea_set_one_line(pin_verify_textarea, true);
        lv_obj_set_size(pin_verify_textarea, 120, 40);
        lv_obj_align(pin_verify_textarea, LV_ALIGN_TOP_RIGHT, -20, 190);
        lv_obj_add_event_cb(pin_verify_textarea, pinVerifyKBEventHandler, LV_EVENT_CLICKED, NULL);
        
        // Save button
        lv_obj_t * save_btn = lv_btn_create(pin_management_screen);
        lv_obj_set_size(save_btn, 100, 50);
        lv_obj_align(save_btn, LV_ALIGN_TOP_MID, -55, 260);
        lv_obj_add_event_cb(save_btn, pinSaveEventHandler, LV_EVENT_CLICKED, NULL);
        lv_obj_set_style_bg_color(save_btn, lv_color_hex(0x4CAF50), LV_PART_MAIN);
        lv_obj_set_style_text_color(save_btn, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
        
        lv_obj_t * save_label = lv_label_create(save_btn);
        lv_label_set_text(save_label, "Save");
        lv_obj_center(save_label);
        
        // Cancel button - fix: create as child of pin_management_screen, not lv_scr_act()
        lv_obj_t * cancel_btn = lv_btn_create(pin_management_screen);
        lv_obj_set_size(cancel_btn, 100, 50);
        lv_obj_align(cancel_btn, LV_ALIGN_TOP_MID, 55, 260);
        lv_obj_add_event_cb(cancel_btn, pinCancelEventHandler, LV_EVENT_CLICKED, NULL);
        lv_obj_set_style_bg_color(cancel_btn, lv_color_hex(0x9E9E9E), LV_PART_MAIN);
        lv_obj_set_style_text_color(cancel_btn, lv_color_hex(0x000000), LV_PART_MAIN);
        
        lv_obj_t * cancel_label = lv_label_create(cancel_btn);
        lv_label_set_text(cancel_label, "Cancel");
        lv_obj_center(cancel_label);
        
        // Create keyboard
        pin_keyboard = lv_keyboard_create(lv_scr_act());
        lv_keyboard_set_mode(pin_keyboard, LV_KEYBOARD_MODE_NUMBER);
        lv_obj_add_flag(pin_keyboard, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_event_cb(pin_keyboard, [](lv_event_t *e) {
            lv_event_code_t code = lv_event_get_code(e);
            if (code == LV_EVENT_READY || code == LV_EVENT_CANCEL) {
                lv_obj_add_flag(pin_keyboard, LV_OBJ_FLAG_HIDDEN);
            }
        }, LV_EVENT_ALL, NULL);
    }
    
    void hidePinManagementScreen() {
        if (pin_management_screen != NULL) {
            lv_obj_del(pin_management_screen);
            pin_management_screen = NULL;
        }
        pin_keyboard = NULL;
        pin_current_textarea = NULL;
        pin_new_textarea = NULL;
        pin_verify_textarea = NULL;
    }
    
    void showPinVerificationScreen() {
        if (pin_verification_screen != NULL) {
            lv_obj_del(pin_verification_screen);
        }
        
        pin_verification_screen = lv_obj_create(lv_scr_act());
        lv_obj_set_size(pin_verification_screen, lv_pct(100), lv_pct(100));
        lv_obj_set_style_bg_color(pin_verification_screen, lv_color_hex(0x000000), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(pin_verification_screen, LV_OPA_100, LV_PART_MAIN);
        
        // Title
        lv_obj_t * title = lv_label_create(pin_verification_screen);
        lv_label_set_text(title, "Enter PIN");
        lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 20);
        lv_obj_set_style_text_font(title, &lv_font_montserrat_16, LV_PART_MAIN);
        
        // PIN input field
        pin_verification_textarea = lv_textarea_create(pin_verification_screen);
        lv_textarea_set_password_mode(pin_verification_textarea, true);
        lv_textarea_set_one_line(pin_verification_textarea, true);
        lv_obj_set_size(pin_verification_textarea, 200, 50);
        lv_obj_align(pin_verification_textarea, LV_ALIGN_TOP_MID, 0, 80);
        lv_obj_add_event_cb(pin_verification_textarea, pinVerificationKBEventHandler, LV_EVENT_CLICKED, NULL);
        lv_obj_add_event_cb(pin_verification_textarea, [](lv_event_t *e) {
            lv_event_code_t code = lv_event_get_code(e);
            if (code == LV_EVENT_VALUE_CHANGED) {
                if (pin_verification_status != NULL) {
                    lv_obj_add_flag(pin_verification_status, LV_OBJ_FLAG_HIDDEN);
                }
            }
        }, LV_EVENT_VALUE_CHANGED, NULL);
        
        // Status label
        pin_verification_status = lv_label_create(pin_verification_screen);
        lv_label_set_text(pin_verification_status, "");
        lv_obj_align(pin_verification_status, LV_ALIGN_TOP_MID, 0, 140);
        lv_obj_set_style_text_color(pin_verification_status, lv_color_hex(0xFF0000), LV_PART_MAIN);
        lv_obj_add_flag(pin_verification_status, LV_OBJ_FLAG_HIDDEN);
        
        // Launch button
        lv_obj_t * launch_btn = lv_btn_create(pin_verification_screen);
        lv_obj_set_size(launch_btn, 100, 50);
        lv_obj_align(launch_btn, LV_ALIGN_TOP_MID, -60, 150);
        lv_obj_add_event_cb(launch_btn, [](lv_event_t *e) {
            lv_event_code_t code = lv_event_get_code(e);
            if (code == LV_EVENT_CLICKED) {
                // Reset activity timer on PIN verification
                App::resetActivityTimer();
                
                const char* entered_pin = lv_textarea_get_text(pin_verification_textarea);
                if (Settings::verifyPin(String(entered_pin))) {
                    if (pin_verification_screen != NULL) {
                        lv_obj_del(pin_verification_screen);
                        pin_verification_screen = NULL;
                    }
                    WiFiManager::startAPMode();
                } else {
                    lv_textarea_set_text(pin_verification_textarea, "");
                    lv_label_set_text(pin_verification_status, "Incorrect PIN");
                    lv_obj_clear_flag(pin_verification_status, LV_OBJ_FLAG_HIDDEN);
                }
            }
        }, LV_EVENT_CLICKED, NULL);
        
        lv_obj_t * launch_label = lv_label_create(launch_btn);
        lv_label_set_text(launch_label, "Launch");
        lv_obj_center(launch_label);
        
        // Style for Launch button (Green)
        lv_obj_set_style_bg_color(launch_btn, lv_color_hex(0x4CAF50), LV_PART_MAIN);
        lv_obj_set_style_text_color(launch_btn, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
        lv_obj_set_style_bg_color(launch_btn, lv_color_hex(0x45A049), LV_STATE_PRESSED);
        
        // Cancel button
        lv_obj_t * cancel_btn = lv_btn_create(pin_verification_screen);
        lv_obj_set_size(cancel_btn, 100, 50);
        lv_obj_align(cancel_btn, LV_ALIGN_TOP_MID, 60, 150);
        lv_obj_add_event_cb(cancel_btn, pinVerificationCancelEventHandler, LV_EVENT_CLICKED, NULL);
        
        lv_obj_t * cancel_label = lv_label_create(cancel_btn);
        lv_label_set_text(cancel_label, "Cancel");
        lv_obj_center(cancel_label);
        
        // Style for Cancel button (Grey)
        lv_obj_set_style_bg_color(cancel_btn, lv_color_hex(0x9E9E9E), LV_PART_MAIN);
        lv_obj_set_style_text_color(cancel_btn, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
        lv_obj_set_style_bg_color(cancel_btn, lv_color_hex(0x616161), LV_STATE_PRESSED);
        
        // Create keyboard
        pin_verification_keyboard = lv_keyboard_create(lv_scr_act());
        lv_keyboard_set_mode(pin_verification_keyboard, LV_KEYBOARD_MODE_NUMBER);
        lv_obj_clear_flag(pin_verification_keyboard, LV_OBJ_FLAG_HIDDEN);
        lv_keyboard_set_textarea(pin_verification_keyboard, pin_verification_textarea);
        lv_obj_add_event_cb(pin_verification_keyboard, [](lv_event_t *e) {
            lv_event_code_t code = lv_event_get_code(e);
            if (code == LV_EVENT_READY || code == LV_EVENT_CANCEL) {
                lv_obj_add_flag(pin_verification_keyboard, LV_OBJ_FLAG_HIDDEN);
            }
        }, LV_EVENT_ALL, NULL);
    }
    
    // PIN event handlers
    void pinCurrentKBEventHandler(lv_event_t *e) {
        lv_event_code_t code = lv_event_get_code(e);
        if (code == LV_EVENT_CLICKED) {
            if (pin_keyboard != NULL) {
                lv_keyboard_set_textarea(pin_keyboard, pin_current_textarea);
                lv_obj_clear_flag(pin_keyboard, LV_OBJ_FLAG_HIDDEN);
            }
        } else if (code == LV_EVENT_READY || code == LV_EVENT_CANCEL) {
            if (pin_keyboard != NULL) {
                lv_obj_add_flag(pin_keyboard, LV_OBJ_FLAG_HIDDEN);
            }
        }
    }
    
    void pinNewKBEventHandler(lv_event_t *e) {
        lv_event_code_t code = lv_event_get_code(e);
        if (code == LV_EVENT_CLICKED) {
            if (pin_keyboard != NULL) {
                lv_keyboard_set_textarea(pin_keyboard, pin_new_textarea);
                lv_obj_clear_flag(pin_keyboard, LV_OBJ_FLAG_HIDDEN);
            }
        } else if (code == LV_EVENT_READY || code == LV_EVENT_CANCEL) {
            if (pin_keyboard != NULL) {
                lv_obj_add_flag(pin_keyboard, LV_OBJ_FLAG_HIDDEN);
            }
        }
    }
    
    void pinVerifyKBEventHandler(lv_event_t *e) {
        lv_event_code_t code = lv_event_get_code(e);
        if (code == LV_EVENT_CLICKED) {
            if (pin_keyboard != NULL) {
                lv_keyboard_set_textarea(pin_keyboard, pin_verify_textarea);
                lv_obj_clear_flag(pin_keyboard, LV_OBJ_FLAG_HIDDEN);
            }
        } else if (code == LV_EVENT_READY || code == LV_EVENT_CANCEL) {
            if (pin_keyboard != NULL) {
                lv_obj_add_flag(pin_keyboard, LV_OBJ_FLAG_HIDDEN);
            }
        }
    }
    
    void pinSaveEventHandler(lv_event_t *e) {
        lv_event_code_t code = lv_event_get_code(e);
        if (code == LV_EVENT_CLICKED) {
            const char* current_pin_entered = lv_textarea_get_text(pin_current_textarea);
            const char* new_pin = lv_textarea_get_text(pin_new_textarea);
            const char* verify_pin = lv_textarea_get_text(pin_verify_textarea);
            
            if (!verifyPin(String(current_pin_entered))) {
                UI::showMessage("Error", "Current PIN is incorrect");
                return;
            }
            
            if (String(new_pin) != String(verify_pin)) {
                UI::showMessage("Error", "New PINs do not match");
                return;
            }
            
            if (strlen(new_pin) < 4) {
                UI::showMessage("Error", "PIN must be at least 4 digits");
                return;
            }
            
            setCurrentPin(String(new_pin));
            UI::showMessage("Success", "PIN updated successfully");
            hidePinManagementScreen();
        }
    }
    
    void pinCancelEventHandler(lv_event_t *e) {
        lv_event_code_t code = lv_event_get_code(e);
        if (code == LV_EVENT_CLICKED) {
            hidePinManagementScreen();
        }
    }
    
    void pinVerificationKBEventHandler(lv_event_t *e) {
        lv_event_code_t code = lv_event_get_code(e);
        if (code == LV_EVENT_READY) {
            const char* entered_pin = lv_textarea_get_text(pin_verification_textarea);
            if (verifyPin(String(entered_pin))) {
                if (pin_verification_screen != NULL) {
                    lv_obj_del(pin_verification_screen);
                    pin_verification_screen = NULL;
                }
                WiFiManager::startAPMode();
            } else {
                lv_textarea_set_text(pin_verification_textarea, "");
                lv_label_set_text(pin_verification_status, "Incorrect PIN");
                lv_obj_clear_flag(pin_verification_status, LV_OBJ_FLAG_HIDDEN);
            }
        }
    }
    
    void pinVerificationCancelEventHandler(lv_event_t *e) {
        lv_event_code_t code = lv_event_get_code(e);
        if (code == LV_EVENT_CLICKED) {
            if (pin_verification_screen != NULL) {
                lv_obj_del(pin_verification_screen);
                pin_verification_screen = NULL;
            }
        }
    }
    
    void showPinVerificationScreenForQR() {
        if (pin_verification_qr_screen != NULL) {
            lv_obj_del(pin_verification_qr_screen);
        }
        
        pin_verification_qr_screen = lv_obj_create(lv_scr_act());
        lv_obj_set_size(pin_verification_qr_screen, lv_pct(100), lv_pct(100));
        lv_obj_set_style_bg_color(pin_verification_qr_screen, lv_color_hex(0x000000), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(pin_verification_qr_screen, LV_OPA_100, LV_PART_MAIN);
        
        // Title
        lv_obj_t * title = lv_label_create(pin_verification_qr_screen);
        lv_label_set_text(title, "Enter PIN to Show Pairing QR");
        lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 20);
        lv_obj_set_style_text_font(title, &lv_font_montserrat_16, LV_PART_MAIN);
        lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
        
        // PIN input field
        pin_verification_qr_textarea = lv_textarea_create(pin_verification_qr_screen);
        lv_textarea_set_password_mode(pin_verification_qr_textarea, true);
        lv_textarea_set_one_line(pin_verification_qr_textarea, true);
        lv_obj_set_size(pin_verification_qr_textarea, 200, 50);
        lv_obj_align(pin_verification_qr_textarea, LV_ALIGN_TOP_MID, 0, 80);
        lv_obj_add_event_cb(pin_verification_qr_textarea, pinVerificationQRKBEventHandler, LV_EVENT_CLICKED, NULL);
        lv_obj_add_event_cb(pin_verification_qr_textarea, [](lv_event_t *e) {
            lv_event_code_t code = lv_event_get_code(e);
            if (code == LV_EVENT_VALUE_CHANGED) {
                if (pin_verification_qr_status != NULL) {
                    lv_obj_add_flag(pin_verification_qr_status, LV_OBJ_FLAG_HIDDEN);
                }
            }
        }, LV_EVENT_VALUE_CHANGED, NULL);
        
        // Status label
        pin_verification_qr_status = lv_label_create(pin_verification_qr_screen);
        lv_label_set_text(pin_verification_qr_status, "");
        lv_obj_align(pin_verification_qr_status, LV_ALIGN_TOP_MID, 0, 140);
        lv_obj_set_style_text_color(pin_verification_qr_status, lv_color_hex(0xFF0000), LV_PART_MAIN);
        lv_obj_add_flag(pin_verification_qr_status, LV_OBJ_FLAG_HIDDEN);
        
        // Show QR button
        lv_obj_t * show_qr_btn = lv_btn_create(pin_verification_qr_screen);
        lv_obj_set_size(show_qr_btn, 120, 50);
        lv_obj_align(show_qr_btn, LV_ALIGN_TOP_MID, -70, 150);
        lv_obj_add_event_cb(show_qr_btn, [](lv_event_t *e) {
            lv_event_code_t code = lv_event_get_code(e);
            if (code == LV_EVENT_CLICKED) {
                // Reset activity timer on PIN verification
                App::resetActivityTimer();
                
                const char* entered_pin = lv_textarea_get_text(pin_verification_qr_textarea);
                if (Settings::verifyPin(String(entered_pin))) {
                    // Hide keyboard first before deleting the screen
                    if (pin_verification_qr_keyboard != NULL && lv_obj_is_valid(pin_verification_qr_keyboard)) {
                        lv_obj_del(pin_verification_qr_keyboard);
                        pin_verification_qr_keyboard = NULL;
                    }
                    
                    if (pin_verification_qr_screen != NULL) {
                        lv_obj_del(pin_verification_qr_screen);
                        pin_verification_qr_screen = NULL;
                    }
                    // Show the pairing QR code
                    UI::showPairingQRCode();
                } else {
                    lv_textarea_set_text(pin_verification_qr_textarea, "");
                    lv_label_set_text(pin_verification_qr_status, "Incorrect PIN");
                    lv_obj_clear_flag(pin_verification_qr_status, LV_OBJ_FLAG_HIDDEN);
                }
            }
        }, LV_EVENT_CLICKED, NULL);
        
        lv_obj_t * show_qr_label = lv_label_create(show_qr_btn);
        lv_label_set_text(show_qr_label, "Show QR");
        lv_obj_center(show_qr_label);
        
        // Style for Show QR button (Blue)
        lv_obj_set_style_bg_color(show_qr_btn, lv_color_hex(0x2196F3), LV_PART_MAIN);
        lv_obj_set_style_text_color(show_qr_btn, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
        lv_obj_set_style_bg_color(show_qr_btn, lv_color_hex(0x1976D2), LV_STATE_PRESSED);
        
        // Cancel button
        lv_obj_t * cancel_btn = lv_btn_create(pin_verification_qr_screen);
        lv_obj_set_size(cancel_btn, 100, 50);
        lv_obj_align(cancel_btn, LV_ALIGN_TOP_MID, 70, 150);
        lv_obj_add_event_cb(cancel_btn, pinVerificationQRCancelEventHandler, LV_EVENT_CLICKED, NULL);
        
        lv_obj_t * cancel_label = lv_label_create(cancel_btn);
        lv_label_set_text(cancel_label, "Cancel");
        lv_obj_center(cancel_label);
        
        // Style for Cancel button (Grey)
        lv_obj_set_style_bg_color(cancel_btn, lv_color_hex(0x9E9E9E), LV_PART_MAIN);
        lv_obj_set_style_text_color(cancel_btn, lv_color_hex(0x000000), LV_PART_MAIN);
        lv_obj_set_style_bg_color(cancel_btn, lv_color_hex(0x757575), LV_STATE_PRESSED);
        
        // Create keyboard
        pin_verification_qr_keyboard = lv_keyboard_create(lv_scr_act());
        lv_keyboard_set_mode(pin_verification_qr_keyboard, LV_KEYBOARD_MODE_NUMBER);
        lv_obj_clear_flag(pin_verification_qr_keyboard, LV_OBJ_FLAG_HIDDEN);
        lv_keyboard_set_textarea(pin_verification_qr_keyboard, pin_verification_qr_textarea);
        lv_obj_add_event_cb(pin_verification_qr_keyboard, [](lv_event_t *e) {
            lv_event_code_t code = lv_event_get_code(e);
            if (code == LV_EVENT_READY || code == LV_EVENT_CANCEL) {
                lv_obj_add_flag(pin_verification_qr_keyboard, LV_OBJ_FLAG_HIDDEN);
            }
        }, LV_EVENT_ALL, NULL);
    }
    
    void pinVerificationQRKBEventHandler(lv_event_t *e) {
        lv_event_code_t code = lv_event_get_code(e);
        if (code == LV_EVENT_READY) {
            const char* entered_pin = lv_textarea_get_text(pin_verification_qr_textarea);
            if (verifyPin(String(entered_pin))) {
                // Hide keyboard first before deleting the screen
                if (pin_verification_qr_keyboard != NULL && lv_obj_is_valid(pin_verification_qr_keyboard)) {
                    lv_obj_del(pin_verification_qr_keyboard);
                    pin_verification_qr_keyboard = NULL;
                }
                
                if (pin_verification_qr_screen != NULL) {
                    lv_obj_del(pin_verification_qr_screen);
                    pin_verification_qr_screen = NULL;
                }
                // Show the pairing QR code
                UI::showPairingQRCode();
            } else {
                lv_textarea_set_text(pin_verification_qr_textarea, "");
                lv_label_set_text(pin_verification_qr_status, "Incorrect PIN");
                lv_obj_clear_flag(pin_verification_qr_status, LV_OBJ_FLAG_HIDDEN);
            }
        }
    }
    
    void pinVerificationQRCancelEventHandler(lv_event_t *e) {
        lv_event_code_t code = lv_event_get_code(e);
        if (code == LV_EVENT_CLICKED) {
            // Hide keyboard first before deleting the screen
            if (pin_verification_qr_keyboard != NULL && lv_obj_is_valid(pin_verification_qr_keyboard)) {
                lv_obj_del(pin_verification_qr_keyboard);
                pin_verification_qr_keyboard = NULL;
            }
            
            if (pin_verification_qr_screen != NULL) {
                lv_obj_del(pin_verification_qr_screen);
                pin_verification_qr_screen = NULL;
            }
        }
    }
    
    void cleanupPinVerificationQRKeyboard() {
        // Clean up any remaining PIN verification QR keyboard
        if (pin_verification_qr_keyboard != NULL && lv_obj_is_valid(pin_verification_qr_keyboard)) {
            lv_obj_del(pin_verification_qr_keyboard);
            pin_verification_qr_keyboard = NULL;
        }
        
        // Clean up any remaining PIN verification QR screen
        if (pin_verification_qr_screen != NULL && lv_obj_is_valid(pin_verification_qr_screen)) {
            lv_obj_del(pin_verification_qr_screen);
            pin_verification_qr_screen = NULL;
        }
    }
}