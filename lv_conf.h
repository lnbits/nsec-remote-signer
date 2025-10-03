#ifndef LV_CONF_H
#define LV_CONF_H

/* ==== CORE ==== */
#define LV_COLOR_DEPTH 16
#define LV_COLOR_16_SWAP 0

/* Panel resolution (adjust if different) */
#define LV_HOR_RES_MAX 480
#define LV_VER_RES_MAX 320

/* ==== INPUT / INDEV ==== */
#define LV_USE_INDEV 1   /* ensure input devices (touch) are compiled */

/* ==== FEATURES YOU USE ==== */
/* QR code widget (provides lv_qrcode_create/update) */
#define LV_USE_QRCODE 1

/* Fonts you reference in code */
#define LV_FONT_MONTSERRAT_12 1
#define LV_FONT_MONTSERRAT_16 1
#define LV_FONT_MONTSERRAT_24 1

/* Default font */
#define LV_FONT_DEFAULT &lv_font_montserrat_16

/* ==== NICE-TO-HAVES (optional) ==== */
#define LV_USE_LOG 0
#define LV_USE_ASSERT_NULL 0

#endif /* LV_CONF_H */
