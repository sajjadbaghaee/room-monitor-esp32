#pragma once

namespace TelemetryService
{
void queuePing();
void queueAlive();
void queueDeviceInfo();
void queueFullData();
void queueConfiguration();
void processDeadbands();
} // namespace TelemetryService
