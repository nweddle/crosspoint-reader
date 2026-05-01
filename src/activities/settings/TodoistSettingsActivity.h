#pragma once

#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

/**
 * On-device sub-screen for Todoist configuration.
 *
 * Three rows:
 *   - API Token: status display only ("******" / "Not Set"). Token entry is
 *     impractical via the device keyboard (40-char hex); use the web settings.
 *   - Filter: editable via KeyboardEntryActivity.
 *   - View Tasks: launches TodoistActivity to preview the current filter.
 */
class TodoistSettingsActivity final : public Activity {
 public:
  explicit TodoistSettingsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("TodoistSettings", renderer, mappedInput) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  ButtonNavigator buttonNavigator;
  size_t selectedIndex = 0;

  void handleSelection();
};
