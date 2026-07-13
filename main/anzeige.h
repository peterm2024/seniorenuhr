/*
 * anzeige.h — Bring-up des 7"-RGB-Displays (800x480) mit LVGL und Touch.
 *
 * Nach anzeige_start() laeuft LVGL; UI-Aenderungen immer zwischen
 * lvgl_port_lock(0) und lvgl_port_unlock() vornehmen.
 */
#ifndef ANZEIGE_H
#define ANZEIGE_H

#include "esp_err.h"

esp_err_t anzeige_start(void);

#endif
