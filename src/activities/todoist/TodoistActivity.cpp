#include "TodoistActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>
#include <Logging.h>
#include <TodoistCredentialStore.h>
#include <WiFi.h>
#include <esp_sntp.h>

#include <algorithm>
#include <cstdio>
#include <ctime>

#include "MappedInputManager.h"
#include "activities/network/WifiSelectionActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {
constexpr int FETCH_PAGE_LIMIT = 30;

// Today's date as YYYY-MM-DD in local time. Used for Overdue/Today/Tomorrow grouping.
std::string localDateString(int dayOffset = 0) {
  time_t now = time(nullptr);
  now += static_cast<time_t>(dayOffset) * 86400;
  struct tm tmLocal;
  localtime_r(&now, &tmLocal);
  char buf[11];
  snprintf(buf, sizeof(buf), "%04d-%02d-%02d", tmLocal.tm_year + 1900, tmLocal.tm_mon + 1, tmLocal.tm_mday);
  return buf;
}

// Date bucket for sorting: smaller value = renders first.
// Overdue=0, Today=1, Tomorrow=2, Future-with-date=3, NoDate=4.
int dateBucket(const std::string& dueDate, const std::string& today, const std::string& tomorrow) {
  if (dueDate.empty()) return 4;
  if (dueDate < today) return 0;
  if (dueDate == today) return 1;
  if (dueDate == tomorrow) return 2;
  return 3;
}

// Per-row label shown next to the task content. "Overdue" / "Today" / "Tomorrow" / "MMM D" / empty.
std::string dateLabel(const std::string& dueDate, const std::string& today, const std::string& tomorrow) {
  if (dueDate.empty()) return "";
  if (dueDate < today) return std::string(tr(STR_TODOIST_OVERDUE));
  if (dueDate == today) return std::string(tr(STR_TODOIST_TODAY));
  if (dueDate == tomorrow) return std::string(tr(STR_TODOIST_TOMORROW));
  // Future date — show "YYYY-MM-DD" verbatim. Locale-aware month names would need
  // an extra string table; the raw date keeps the implementation small.
  return dueDate;
}

void syncTimeWithNTP() {
  // Date bucketing (Overdue/Today/Tomorrow) needs real wall time. ESP32-C3 has
  // no battery-backed RTC, so time is lost on power-off and must be re-synced
  // on each WiFi session.
  if (esp_sntp_enabled()) esp_sntp_stop();
  esp_sntp_setoperatingmode(ESP_SNTP_OPMODE_POLL);
  esp_sntp_setservername(0, "pool.ntp.org");
  esp_sntp_init();

  for (int retry = 0; retry < 50 && sntp_get_sync_status() != SNTP_SYNC_STATUS_COMPLETED; retry++) {
    vTaskDelay(100 / portTICK_PERIOD_MS);
  }
}

void wifiOff() {
  if (esp_sntp_enabled()) esp_sntp_stop();
  WiFi.disconnect(false);
  delay(100);
  WiFi.mode(WIFI_OFF);
  delay(100);
}
}  // namespace

void TodoistActivity::onEnter() {
  Activity::onEnter();

  if (!TODOIST_STORE.hasApiToken()) {
    state = NO_TOKEN;
    requestUpdate();
    return;
  }

  if (WiFi.status() == WL_CONNECTED) {
    LOG_DBG("Todoist", "WiFi already up, fetching");
    onWifiSelectionComplete(true);
    return;
  }

  state = WIFI_SELECTION;
  startActivityForResult(std::make_unique<WifiSelectionActivity>(renderer, mappedInput),
                         [this](const ActivityResult& result) { onWifiSelectionComplete(!result.isCancelled); });
}

void TodoistActivity::onExit() {
  Activity::onExit();
  wifiOff();
}

void TodoistActivity::onWifiSelectionComplete(bool success) {
  if (!success) {
    state = ERROR_STATE;
    statusMessage = tr(STR_CONNECTION_FAILED);
    requestUpdate(true);
    return;
  }
  performFetch();
}

void TodoistActivity::performFetch() {
  {
    RenderLock lock(*this);
    state = LOADING;
  }
  requestUpdateAndWait();

  syncTimeWithNTP();

  tasks.clear();
  std::string nextCursor;
  const auto err =
      TodoistClient::getTasks(TODOIST_STORE.getEffectiveFilterQuery(), "", FETCH_PAGE_LIMIT, tasks, nextCursor);

  RenderLock lock(*this);
  if (err != TodoistClient::OK) {
    state = ERROR_STATE;
    statusMessage = TodoistClient::errorString(err);
    requestUpdate(true);
    return;
  }
  if (tasks.empty()) {
    state = EMPTY;
    requestUpdate(true);
    return;
  }
  sortTasksForDisplay();
  selectedIndex = 0;
  state = SHOWING_TASKS;
  requestUpdate(true);
}

void TodoistActivity::sortTasksForDisplay() {
  const std::string today = localDateString(0);
  const std::string tomorrow = localDateString(1);

  // Stable sort: bucket asc, then priority desc (API priority 4 = urgent, must
  // appear first), then due date asc within the future bucket.
  std::stable_sort(tasks.begin(), tasks.end(), [&](const TodoistTask& a, const TodoistTask& b) {
    const int ba = dateBucket(a.dueDate, today, tomorrow);
    const int bb = dateBucket(b.dueDate, today, tomorrow);
    if (ba != bb) return ba < bb;
    if (a.priority != b.priority) return a.priority > b.priority;
    return a.dueDate < b.dueDate;
  });
}

void TodoistActivity::loop() {
  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    finish();
    return;
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    if (state == SHOWING_TASKS || state == ERROR_STATE || state == EMPTY) {
      performFetch();
    }
    return;
  }

  if (state != SHOWING_TASKS || tasks.empty()) return;

  buttonNavigator.onNext([this] {
    selectedIndex = (selectedIndex + 1) % tasks.size();
    requestUpdate();
  });
  buttonNavigator.onPrevious([this] {
    selectedIndex = (selectedIndex + tasks.size() - 1) % tasks.size();
    requestUpdate();
  });
}

void TodoistActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_TODOIST_TASKS));

  if (state == NO_TOKEN) {
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 - 20, tr(STR_TODOIST_NO_TOKEN), true, EpdFontFamily::BOLD);
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 + 10, tr(STR_TODOIST_SETUP_HINT));
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    renderer.displayBuffer();
    return;
  }

  if (state == LOADING || state == WIFI_SELECTION) {
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2, tr(STR_TODOIST_LOADING), true, EpdFontFamily::BOLD);
    renderer.displayBuffer();
    return;
  }

  if (state == ERROR_STATE) {
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2, statusMessage.c_str(), true, EpdFontFamily::BOLD);
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_TODOIST_REFRESH), "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    renderer.displayBuffer();
    return;
  }

  if (state == EMPTY) {
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2, tr(STR_TODOIST_NO_TASKS), true, EpdFontFamily::BOLD);
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_TODOIST_REFRESH), "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    renderer.displayBuffer();
    return;
  }

  // SHOWING_TASKS
  const std::string today = localDateString(0);
  const std::string tomorrow = localDateString(1);

  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentHeight = pageHeight - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing * 2;

  GUI.drawList(
      renderer, Rect{0, contentTop, pageWidth, contentHeight}, static_cast<int>(tasks.size()),
      static_cast<int>(selectedIndex),
      [this](int i) {
        // Single-level subtask indent: prefix child tasks with "  ↳ ".
        const auto& t = tasks[i];
        if (!t.parentId.empty()) return std::string("  > ") + t.content;
        return t.content;
      },
      [this, &today, &tomorrow](int i) { return dateLabel(tasks[i].dueDate, today, tomorrow); }, nullptr,
      [this](int i) {
        // Show "p1"/"p2"/"p3" only for high-priority tasks. Skip the lowest (API priority 1).
        const uint8_t p = tasks[i].priority;
        if (p <= 1) return std::string("");
        // API: 4=urgent (display p1), 3=high (p2), 2=medium (p3).
        char buf[4];
        snprintf(buf, sizeof(buf), "p%d", 5 - p);
        return std::string(buf);
      },
      true);

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_TODOIST_REFRESH), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}
