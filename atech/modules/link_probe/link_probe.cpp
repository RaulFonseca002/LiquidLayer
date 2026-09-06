#include "link_probe.h"
// Genuinely link the stacks (the S1 builds had them): touch symbols from the
// WiFi library, lwIP sockets and esp_ping without starting anything.
static void* volatile g_keep[3];
void LinkProbe::begin() {
    _mode = (int)WiFi.getMode();            // WiFi lib: returns NULL mode, no init
    WiFiUDP udp; udp.stop();                // lwIP socket class, never bound
    g_keep[0] = (void*)&esp_ping_new_session;
    g_keep[1] = (void*)&esp_wifi_init;
    g_keep[2] = (void*)&esp_wifi_set_csi;
}
