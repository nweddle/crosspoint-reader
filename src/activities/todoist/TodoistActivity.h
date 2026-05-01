#pragma once
#include <TodoistClient.h>

#include <string>
#include <vector>

#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

/**
 * Activity that fetches and displays a single page of Todoist tasks.
 *
 * Flow:
 *   1. If no API token configured -> NO_TOKEN screen
 *   2. Connect WiFi via WifiSelectionActivity if not already connected
 *   3. GET /api/v1/tasks/filter -> SHOWING_TASKS or ERROR
 *   4. Confirm key triggers a re-fetch.
 */
class TodoistActivity final : public Activity {
 public:
  explicit TodoistActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("Todoist", renderer, mappedInput) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  enum State { NO_TOKEN, WIFI_SELECTION, LOADING, SHOWING_TASKS, ERROR_STATE, EMPTY };

  ButtonNavigator buttonNavigator;

  State state = LOADING;
  std::string statusMessage;       // shown in ERROR_STATE
  std::vector<TodoistTask> tasks;  // sorted for display
  size_t selectedIndex = 0;

  void onWifiSelectionComplete(bool success);
  void performFetch();
  void sortTasksForDisplay();
};
