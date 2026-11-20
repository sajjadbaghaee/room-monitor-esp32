#pragma once

namespace FilesystemManager
{
// Mounts SPIFFS for web assets. It deliberately does not auto-format on a
// mount error because formatting would silently destroy the dashboard files.
bool begin();

// Unmounts SPIFFS. OtaManager uses this before replacing the filesystem
// partition so no file remains open while flash is being rewritten.
void end();

// True when SPIFFS is mounted and available to WebManager/migration code.
bool isReady();
} // namespace FilesystemManager
