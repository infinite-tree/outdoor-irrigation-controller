#pragma once

#include <Arduino.h>
#include <WebServer.h>

#define ZONE1           1
#define ZONE2           2

extern WebServer server;
extern bool zone1On;
extern bool zone2On;
extern bool webAction;

void open_solenoid(uint8_t zone);
void close_solenoid(uint8_t zone);
void web_server_init();
void web_server_poll();
