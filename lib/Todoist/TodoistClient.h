#pragma once
#include <cstdint>
#include <string>
#include <vector>

/**
 * One Todoist task, trimmed to the fields needed for display.
 * Wire field names from the API (legacy aliases): content, priority, due.date,
 * parent_id, child_order. See Todoist API v1 /tasks/filter response shape.
 */
struct TodoistTask {
  std::string content;   // truncated to TodoistTask::MAX_CONTENT_LEN at parse time
  std::string dueDate;   // "YYYY-MM-DD" — first 10 chars of due.date, empty if no due
  std::string parentId;  // empty if top-level
  uint8_t priority = 1;  // API priority: 1 = none, 4 = urgent. Sort descending for display.

  static constexpr size_t MAX_CONTENT_LEN = 96;
};

/**
 * HTTP client for Todoist's REST API v1.
 *
 * Auth: Bearer <personal API token> from TodoistCredentialStore.
 * Endpoint: GET https://api.todoist.com/api/v1/tasks/filter?query=<filter>&limit=<n>[&cursor=<c>]
 *
 * Mirrors KOReaderSyncClient: esp_http_client + esp_crt_bundle_attach + 2KB TLS buffers.
 */
class TodoistClient {
 public:
  enum Error {
    OK = 0,
    NO_TOKEN,
    NETWORK_ERROR,
    AUTH_FAILED,
    RATE_LIMITED,
    SERVER_ERROR,
    JSON_ERROR,
  };

  /**
   * Fetch a single page of filtered tasks.
   * @param filterQuery Todoist filter language (e.g. "today", "overdue | today", "p1 & @home").
   *                    If empty, the credential store's effective filter is used.
   * @param cursor Pagination cursor; pass empty string for the first page.
   * @param limit Max tasks per page (1..200). Caller should keep this small (~20) to bound RAM.
   * @param outTasks Output: parsed tasks appended (not cleared).
   * @param outNextCursor Output: cursor for the next page, or empty string when done.
   * @return OK on success, error code otherwise.
   */
  static Error getTasks(const std::string& filterQuery, const std::string& cursor, int limit,
                        std::vector<TodoistTask>& outTasks, std::string& outNextCursor);

  static const char* errorString(Error error);

  /** HTTP status code from the last request. */
  static int lastHttpCode;
};
