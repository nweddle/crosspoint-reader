#pragma once
#include <cstdint>
#include <string>

class TodoistCredentialStore;
namespace JsonSettingsIO {
bool saveTodoist(const TodoistCredentialStore& store, const char* path);
bool loadTodoist(TodoistCredentialStore& store, const char* json);
}  // namespace JsonSettingsIO

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
};

#define TODOIST_STORE TodoistCredentialStore::getInstance()
