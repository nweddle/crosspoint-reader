#include "TodoistClient.h"

#include <ArduinoJson.h>
#include <Logging.h>
#include <esp_crt_bundle.h>
#include <esp_http_client.h>

#include <cstdio>
#include <cstring>

#include "TodoistCredentialStore.h"

int TodoistClient::lastHttpCode = 0;

namespace {
constexpr char API_HOST_PATH[] = "https://api.todoist.com/api/v1/tasks/filter";

// Same TLS-buffer reduction as KOSync — default 16KB OOMs on ESP32-C3.
constexpr int HTTP_BUF_SIZE = 2048;

// Bounds the response buffer to keep heap pressure predictable. A page of
// 20 tasks is typically 8–15KB; cap at 32KB to absorb worst case.
constexpr int RESPONSE_BUF_MAX = 32 * 1024;

struct ResponseBuffer {
  char* data = nullptr;
  int len = 0;
  int capacity = 0;
  bool overflowed = false;

  ~ResponseBuffer() { free(data); }

  bool append(const char* src, int srcLen) {
    if (len + srcLen + 1 > RESPONSE_BUF_MAX) {
      overflowed = true;
      return false;
    }
    int needed = len + srcLen + 1;
    if (needed > capacity) {
      int newCap = capacity ? capacity * 2 : 4096;
      while (newCap < needed) newCap *= 2;
      if (newCap > RESPONSE_BUF_MAX) newCap = RESPONSE_BUF_MAX;
      char* p = static_cast<char*>(realloc(data, newCap));
      if (!p) return false;
      data = p;
      capacity = newCap;
    }
    memcpy(data + len, src, srcLen);
    len += srcLen;
    data[len] = '\0';
    return true;
  }
};

esp_err_t httpEventHandler(esp_http_client_event_t* evt) {
  auto* buf = static_cast<ResponseBuffer*>(evt->user_data);
  if (evt->event_id == HTTP_EVENT_ON_DATA && buf) {
    if (!buf->append(static_cast<const char*>(evt->data), evt->data_len) && !buf->overflowed) {
      LOG_ERR("Todoist", "Response buffer realloc failed (%d bytes)", evt->data_len);
    }
  }
  return ESP_OK;
}

// URL-encode a query parameter value into dst (null-terminated).
// Conservative — only alnum and -_.~ pass through; everything else becomes %XX.
// Returns false if dst is too small.
bool urlEncode(const char* src, char* dst, size_t dstSize) {
  size_t di = 0;
  for (const char* p = src; *p; p++) {
    unsigned char c = static_cast<unsigned char>(*p);
    bool safe = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_' ||
                c == '.' || c == '~';
    if (safe) {
      if (di + 1 >= dstSize) return false;
      dst[di++] = static_cast<char>(c);
    } else {
      if (di + 3 >= dstSize) return false;
      snprintf(dst + di, 4, "%%%02X", c);
      di += 3;
    }
  }
  if (di >= dstSize) return false;
  dst[di] = '\0';
  return true;
}

}  // namespace

TodoistClient::Error TodoistClient::getTasks(const std::string& filterQuery, const std::string& cursor, int limit,
                                             std::vector<TodoistTask>& outTasks, std::string& outNextCursor) {
  lastHttpCode = 0;
  outNextCursor.clear();

  if (!TODOIST_STORE.hasApiToken()) {
    LOG_DBG("Todoist", "No API token configured");
    return NO_TOKEN;
  }

  if (limit < 1) limit = 1;
  if (limit > 200) limit = 200;

  // Build URL: <host>/tasks/filter?query=<encoded>&limit=<n>[&cursor=<encoded>]
  // Filter and cursor strings are user/server-supplied, so URL-encode them.
  char encQuery[512];
  if (!urlEncode(filterQuery.c_str(), encQuery, sizeof(encQuery))) {
    LOG_ERR("Todoist", "Filter query too long");
    return NETWORK_ERROR;
  }

  char encCursor[256];
  encCursor[0] = '\0';
  if (!cursor.empty()) {
    if (!urlEncode(cursor.c_str(), encCursor, sizeof(encCursor))) {
      LOG_ERR("Todoist", "Cursor too long");
      return NETWORK_ERROR;
    }
  }

  char url[1024];
  if (cursor.empty()) {
    snprintf(url, sizeof(url), "%s?query=%s&limit=%d", API_HOST_PATH, encQuery, limit);
  } else {
    snprintf(url, sizeof(url), "%s?query=%s&limit=%d&cursor=%s", API_HOST_PATH, encQuery, limit, encCursor);
  }

  LOG_DBG("Todoist", "GET %s (heap: %u)", url, (unsigned)ESP.getFreeHeap());

  ResponseBuffer buf;

  esp_http_client_config_t config = {};
  config.url = url;
  config.event_handler = httpEventHandler;
  config.user_data = &buf;
  config.method = HTTP_METHOD_GET;
  config.timeout_ms = 15000;
  config.buffer_size = HTTP_BUF_SIZE;
  config.buffer_size_tx = HTTP_BUF_SIZE;
  config.crt_bundle_attach = esp_crt_bundle_attach;

  esp_http_client_handle_t client = esp_http_client_init(&config);
  if (!client) {
    LOG_ERR("Todoist", "esp_http_client_init failed");
    return NETWORK_ERROR;
  }

  // Bearer token. Stack-allocated header buffer to avoid heap thrash.
  char authHeader[128];
  const std::string& token = TODOIST_STORE.getApiToken();
  int n = snprintf(authHeader, sizeof(authHeader), "Bearer %s", token.c_str());
  if (n < 0 || n >= static_cast<int>(sizeof(authHeader))) {
    LOG_ERR("Todoist", "Token too long for header buffer");
    esp_http_client_cleanup(client);
    return NETWORK_ERROR;
  }
  if (esp_http_client_set_header(client, "Authorization", authHeader) != ESP_OK ||
      esp_http_client_set_header(client, "Accept", "application/json") != ESP_OK) {
    LOG_ERR("Todoist", "Failed to set headers");
    esp_http_client_cleanup(client);
    return NETWORK_ERROR;
  }

  esp_err_t err = esp_http_client_perform(client);
  const int httpCode = esp_http_client_get_status_code(client);
  lastHttpCode = httpCode;
  esp_http_client_cleanup(client);

  LOG_DBG("Todoist", "Response: %d (err: %d, %d bytes)", httpCode, err, buf.len);

  if (err != ESP_OK) return NETWORK_ERROR;
  if (httpCode == 401 || httpCode == 403) return AUTH_FAILED;
  if (httpCode == 429) return RATE_LIMITED;
  if (httpCode != 200) return SERVER_ERROR;
  if (buf.overflowed) {
    LOG_ERR("Todoist", "Response exceeded %d byte cap; reduce limit", RESPONSE_BUF_MAX);
    return SERVER_ERROR;
  }
  if (!buf.data) return JSON_ERROR;

  // ArduinoJson filter: parse only the fields we display, drop the rest.
  // Cuts deserialized doc size by ~10x for typical responses.
  JsonDocument filter;
  filter["next_cursor"] = true;
  JsonObject resultsItem = filter["results"][0].to<JsonObject>();
  resultsItem["content"] = true;
  resultsItem["priority"] = true;
  resultsItem["parent_id"] = true;
  resultsItem["due"]["date"] = true;

  JsonDocument doc;
  DeserializationError jerr = deserializeJson(doc, buf.data, DeserializationOption::Filter(filter));
  if (jerr) {
    LOG_ERR("Todoist", "JSON parse failed: %s", jerr.c_str());
    return JSON_ERROR;
  }

  JsonArrayConst results = doc["results"].as<JsonArrayConst>();
  outTasks.reserve(outTasks.size() + results.size());
  for (JsonObjectConst t : results) {
    TodoistTask task;
    const char* content = t["content"] | "";
    task.content.assign(content, strnlen(content, TodoistTask::MAX_CONTENT_LEN));
    task.priority = t["priority"] | 1;
    const char* parentId = t["parent_id"] | "";
    task.parentId = parentId;
    const char* due = t["due"]["date"] | "";
    if (due[0]) task.dueDate.assign(due, strnlen(due, 10));
    outTasks.push_back(std::move(task));
  }

  const char* nextCursor = doc["next_cursor"] | "";
  if (nextCursor[0]) outNextCursor = nextCursor;

  LOG_DBG("Todoist", "Parsed %u tasks, next_cursor=%s", (unsigned)results.size(),
          outNextCursor.empty() ? "(end)" : "...");
  return OK;
}

const char* TodoistClient::errorString(Error error) {
  switch (error) {
    case OK:
      return "Success";
    case NO_TOKEN:
      return "No API token configured";
    case NETWORK_ERROR:
      return "Network error";
    case AUTH_FAILED:
      return "Authentication failed";
    case RATE_LIMITED:
      return "Rate limited (try again later)";
    case SERVER_ERROR:
      return "Server error";
    case JSON_ERROR:
      return "JSON parse error";
    default:
      return "Unknown error";
  }
}
