// Settings persistence (ESP32 flash) and the JSON shape used by the setup page.
#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>

namespace ov {

void settings_load();
void settings_save();
void settings_to_json(JsonObject o);
// Applies the fields present in `in`, clamped to safe ranges. Returns "" or an error message.
String settings_apply_json(JsonObjectConst in);

// Secrets live in their own namespace and never appear in JSON.
String secret_setup_password();          // generated once per device; also the default admin password
String secret_admin_password();
void secret_set_admin_password(const String& pw);

String rgb_to_hex(uint32_t rgb);
bool hex_to_rgb(const char* s, uint32_t& out);

}  // namespace ov
