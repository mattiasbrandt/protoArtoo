#pragma once

#include <stdint.h>

class AsyncWebServer;
class String;

void registerDriveRoutes(AsyncWebServer& server);
bool executeManualCommand(const String& raw);