#include "TodoistCredentialStore.h"

#include <HalStorage.h>
#include <Logging.h>

#include "../../src/JsonSettingsIO.h"

TodoistCredentialStore TodoistCredentialStore::instance;

namespace {
constexpr char TODOIST_FILE_JSON[] = "/.crosspoint/todoist.json";
constexpr char DEFAULT_FILTER_QUERY[] = "today";
}  // namespace

bool TodoistCredentialStore::saveToFile() const {
  Storage.mkdir("/.crosspoint");
  return JsonSettingsIO::saveTodoist(*this, TODOIST_FILE_JSON);
}

bool TodoistCredentialStore::loadFromFile() {
  if (!Storage.exists(TODOIST_FILE_JSON)) {
    LOG_DBG("TDS", "No Todoist credentials file found");
    return false;
  }
  String json = Storage.readFile(TODOIST_FILE_JSON);
  if (json.isEmpty()) return false;
  return JsonSettingsIO::loadTodoist(*this, json.c_str());
}

void TodoistCredentialStore::setApiToken(const std::string& token) { apiToken = token; }

void TodoistCredentialStore::clearApiToken() {
  apiToken.clear();
  saveToFile();
}

void TodoistCredentialStore::setFilterQuery(const std::string& query) { filterQuery = query; }

std::string TodoistCredentialStore::getEffectiveFilterQuery() const {
  return filterQuery.empty() ? std::string(DEFAULT_FILTER_QUERY) : filterQuery;
}
