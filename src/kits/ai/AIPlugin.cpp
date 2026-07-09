/*
 * Copyright 2026, Fabio Tomat <f.t.public@gmail.com>
 * All rights reserved. Distributed under the terms of the MIT license.
 */

#include <Json.h>
#include <AIPlugin.h>
#include <cstdio>


void extract_json_field(const char* json, const char* key, char* out, size_t out_len) {
    out[0] = '\0';
    if (!json || !key) return;
    char needle[64];
    snprintf(needle, sizeof(needle), "\"%s\":\"", key);
    const char* p = strstr(json, needle);
    if (!p) return;
    p += strlen(needle);
    const char* q = strchr(p, '"');
    if (!q) return;
    size_t len = q - p;
    if (len >= out_len) len = out_len - 1;
    memcpy(out, p, len);
    out[len] = '\0';
}
