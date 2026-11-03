/*
 * Copyright (c) 2026 Michele Dipace <michele.dipace@kaffeine.net>
 * SPDX-License-Identifier: MIT
 *
 * support.h - what the two preferences editors show under Project:
 * About... and Support development.... Both are only ever opened from
 * the menu: nothing here is shown at start or at any other time.
 *
 * A requester's text cannot be selected, and no URL opener is assumed
 * on any of the three systems, so the link is handed to the clipboard
 * instead, for the user to paste into a browser.
 */

#ifndef EDGESNAP_SUPPORT_H
#define EDGESNAP_SUPPORT_H

#define ESP_REPO_URL    "https://github.com/kaffeine1/edgesnap"
#define ESP_SUPPORT_URL "https://www.patreon.com/c/kaffeine1"
#define ESP_KOFI_URL    "https://ko-fi.com/kaffeine1"

#define ESP_SUPPORT_TEXT \
    "Support my work on Patreon. Contributions are voluntary;\n" \
    "no subscription is required to use this software."

#define ESP_KOFI_TEXT \
    "If you prefer a one-time contribution, you can\n" \
    "support my work on Ko-fi."

/* The whole requester text: Patreon first, then Ko-fi. */
#define ESP_SUPPORT_BODY \
    ESP_SUPPORT_TEXT "\n\n" ESP_SUPPORT_URL "\n\n" \
    ESP_KOFI_TEXT "\n\n" ESP_KOFI_URL

/* Put `text` on the clipboard (unit 0) as IFF FTXT: 1 done, 0 not. */
int esp_clip_text(const char *text);

#endif /* EDGESNAP_SUPPORT_H */
