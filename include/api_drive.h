#pragma once

#include <ESPAsyncWebServer.h>
#include <WString.h>

void registerDriveRoutes(AsyncWebServer& server);
bool executeManualCommand(const String& raw);
