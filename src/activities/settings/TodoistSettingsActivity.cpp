#include "TodoistSettingsActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>
#include <TodoistCredentialStore.h>

#include "MappedInputManager.h"
#include "activities/todoist/TodoistActivity.h"
#include "activities/util/KeyboardEntryActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {
constexpr int MENU_ITEMS = 4;
const StrId menuNames[MENU_ITEMS] = {StrId::STR_TODOIST_API_TOKEN, StrId::STR_TODOIST_FILTER_QUERY,
                                     StrId::STR_TODOIST_WALLPAPER, StrId::STR_TODOIST_VIEW_TASKS};

const char* wallpaperModeLabel(TodoistWallpaperMode mode) {
  switch (mode) {
    case TodoistWallpaperMode::ROTATION:
      return tr(STR_TODOIST_WALLPAPER_ROTATION);
    case TodoistWallpaperMode::ALWAYS:
      return tr(STR_TODOIST_WALLPAPER_ALWAYS);
    case TodoistWallpaperMode::OFF:
    default:
      return tr(STR_TODOIST_WALLPAPER_OFF);
  }
}
}  // namespace

void TodoistSettingsActivity::onEnter() {
  Activity::onEnter();
  selectedIndex = 0;
  requestUpdate();
}

void TodoistSettingsActivity::onExit() { Activity::onExit(); }

void TodoistSettingsActivity::loop() {
  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    finish();
    return;
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    handleSelection();
    return;
  }

  buttonNavigator.onNext([this] {
    selectedIndex = (selectedIndex + 1) % MENU_ITEMS;
    requestUpdate();
  });
  buttonNavigator.onPrevious([this] {
    selectedIndex = (selectedIndex + MENU_ITEMS - 1) % MENU_ITEMS;
    requestUpdate();
  });
}

void TodoistSettingsActivity::handleSelection() {
  if (selectedIndex == 0) {
    // API Token — display only, not editable on device
    return;
  }
  if (selectedIndex == 1) {
    // Filter query — editable
    startActivityForResult(
        std::make_unique<KeyboardEntryActivity>(renderer, mappedInput, tr(STR_TODOIST_FILTER_QUERY),
                                                TODOIST_STORE.getFilterQuery(), 256, InputType::Text),
        [this](const ActivityResult& result) {
          if (!result.isCancelled) {
            const auto& kb = std::get<KeyboardResult>(result.data);
            TODOIST_STORE.setFilterQuery(kb.text);
            TODOIST_STORE.saveToFile();
          }
        });
    return;
  }
  if (selectedIndex == 2) {
    // Wallpaper mode — cycle Off / Rotation / Always
    const auto cur = static_cast<uint8_t>(TODOIST_STORE.getWallpaperMode());
    const auto next = (cur + 1) % 3;
    TODOIST_STORE.setWallpaperMode(static_cast<TodoistWallpaperMode>(next));
    TODOIST_STORE.saveToFile();
    requestUpdate();
    return;
  }
  if (selectedIndex == 3) {
    // View Tasks
    startActivityForResult(std::make_unique<TodoistActivity>(renderer, mappedInput), [](const ActivityResult&) {});
  }
}

void TodoistSettingsActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_TODOIST_SETTINGS));

  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentHeight = pageHeight - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing * 2;
  GUI.drawList(
      renderer, Rect{0, contentTop, pageWidth, contentHeight}, static_cast<int>(MENU_ITEMS),
      static_cast<int>(selectedIndex), [](int index) { return std::string(I18N.get(menuNames[index])); },
      [](int index) {
        // Subtitle: warn that "Always" overwrites the user's /sleep.bmp.
        if (index == 2 && TODOIST_STORE.getWallpaperMode() == TodoistWallpaperMode::ALWAYS) {
          return std::string(tr(STR_TODOIST_WALLPAPER_HINT_OVERWRITE));
        }
        return std::string("");
      },
      nullptr,
      [](int index) {
        if (index == 0) {
          return TODOIST_STORE.hasApiToken() ? std::string("******") : std::string(tr(STR_NOT_SET));
        }
        if (index == 1) {
          const auto& filter = TODOIST_STORE.getFilterQuery();
          return filter.empty() ? std::string(tr(STR_DEFAULT_VALUE)) : filter;
        }
        if (index == 2) {
          return std::string(wallpaperModeLabel(TODOIST_STORE.getWallpaperMode()));
        }
        return std::string("");
      },
      true);

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}
