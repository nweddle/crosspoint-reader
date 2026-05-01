#pragma once
#include <cstdint>
#include <string>

class TodoistCredentialStore;
namespace JsonSettingsIO {
bool saveTodoist(const TodoistCredentialStore& store, const char* path);
bool loadTodoist(TodoistCredentialStore& store, const char* json);
}  // namespace JsonSettingsIO

// Wallpaper output modes — controls whether and where TodoistActivity writes
// the rendered task list as a BMP for the device's sleep screen.
enum class TodoistWallpaperMode : uint8_t {
  OFF = 0,       // No wallpaper file written
  ROTATION = 1,  // Write to /.sleep/todoist.bmp — joins user's existing rotation
  ALWAYS = 2,    // Write to /sleep.bmp — overwrites any user-managed root sleep image
};

/**
 * Singleton store for Todoist API credentials and per-user filter query.
 * apiToken is XOR-obfuscated with the device MAC and base64-encoded on disk
 * (same scheme as KOReader and WiFi credentials — not cryptographically secure,
 * but ties the secret to the specific device).
 */
class TodoistCredentialStore {
 private:
  static TodoistCredentialStore instance;
  std::string apiToken;
  std::string filterQuery;  // e.g. "today | overdue", "p1", "@home"
  TodoistWallpaperMode wallpaperMode = TodoistWallpaperMode::OFF;

  TodoistCredentialStore() = default;

  friend bool JsonSettingsIO::saveTodoist(const TodoistCredentialStore&, const char*);
  friend bool JsonSettingsIO::loadTodoist(TodoistCredentialStore&, const char*);

 public:
  TodoistCredentialStore(const TodoistCredentialStore&) = delete;
  TodoistCredentialStore& operator=(const TodoistCredentialStore&) = delete;

  static TodoistCredentialStore& getInstance() { return instance; }

  bool saveToFile() const;
  bool loadFromFile();

  void setApiToken(const std::string& token);
  const std::string& getApiToken() const { return apiToken; }
  bool hasApiToken() const { return !apiToken.empty(); }
  void clearApiToken();

  void setFilterQuery(const std::string& query);
  const std::string& getFilterQuery() const { return filterQuery; }

  // The filter to send to /api/v1/tasks/filter — falls back to "today" if unset.
  std::string getEffectiveFilterQuery() const;

  void setWallpaperMode(TodoistWallpaperMode mode) { wallpaperMode = mode; }
  TodoistWallpaperMode getWallpaperMode() const { return wallpaperMode; }
};

#define TODOIST_STORE TodoistCredentialStore::getInstance()
