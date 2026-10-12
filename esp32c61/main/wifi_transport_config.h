#pragma once
/* Compile-time SDIO frame transport only. Raw PSRAM selftest stays UDP. */
#define WIFI_FRAME_UDP 0
#define WIFI_FRAME_TCP 1
#ifndef WIFI_FRAME_TRANSPORT
#define WIFI_FRAME_TRANSPORT WIFI_FRAME_TCP
#endif
#if WIFI_FRAME_TRANSPORT != WIFI_FRAME_UDP && WIFI_FRAME_TRANSPORT != WIFI_FRAME_TCP
#error "WIFI_FRAME_TRANSPORT must be WIFI_FRAME_UDP or WIFI_FRAME_TCP"
#endif
