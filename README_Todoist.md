# Todoist Integration for CrossPoint Reader

A read-only on-device view of your Todoist tasks, available from the Home menu and Settings → System → Todoist Settings. Optionally writes the rendered task list as a sleep-screen wallpaper so your tasks stay visible while the device is powered off.

This is a personal-use fork feature. It is not part of upstream CrossPoint Reader.

## Setup

### 1. Get a Todoist API token

In any Todoist client (web app or mobile), open Settings → Integrations → Developer (or `https://todoist.com/app/settings/integrations/developer`) and copy your **API token** (40-character hex string).

### 2. Enter the token via web settings

The on-device keyboard is impractical for a 40-char token, so token entry is web-only.

1. On the device, open Settings → System → File Transfer (or whichever screen exposes the device's web server).
2. From a computer or phone on the same network (or the device's hotspot), open the device IP in a browser.
3. Navigate to the Settings page and find the **Todoist** section.
4. Paste your token into **Todoist API Token**. The field is masked once typed.
5. (Optional) Set **Todoist Filter** — see [Filter syntax](#filter-syntax) below. Empty defaults to `today`.
6. (Optional) Set **Todoist Wallpaper** — see [Wallpaper mode](#wallpaper-mode) below.
7. Save.

The token is stored MAC-XOR-obfuscated and base64-encoded in `/.crosspoint/todoist.json` on the SD card — same scheme as KOSync and WiFi credentials. It's protection against casual SD-card-on-PC reading, not a cryptographic guarantee.

## Usage

### Home menu

A new **Todoist Tasks** entry appears in the home menu next to Browse Files / Recent Books / File Transfer / Settings. Selecting it:

1. Connects to WiFi (uses the existing WifiSelectionActivity if not already connected)
2. Syncs time via NTP (date bucketing needs real wall time — ESP32-C3 has no battery-backed RTC)
3. Fetches tasks from `GET /api/v1/tasks/filter?query=<your-filter>&limit=30`
4. Renders the list

Buttons in the list view:

| Button    | Action                 |
|-----------|------------------------|
| Up / Down | Move selection         |
| Confirm   | Re-fetch the task list |
| Back      | Exit (returns to Home) |

### Settings sub-screen

Settings → System → **Todoist Settings** has three rows:

| Row               | Behavior                                                                                    |
|-------------------|---------------------------------------------------------------------------------------------|
| Todoist API Token | Status display only (`******` / `Not Set`). Press Confirm: nothing — set the token via web. |
| Todoist Filter    | Press Confirm to edit via the on-device keyboard.                                           |
| Todoist Wallpaper | Press Confirm to cycle through Off / Rotation / Always.                                     |
| View Tasks        | Shortcut to launch the task list (same as the Home entry).                                  |

### Display

Tasks are grouped by due date and sorted within group by priority (urgent first):

- **Overdue** (any task with a due date earlier than today)
- **Today**
- **Tomorrow**
- **Future** (sorted by date, label shown as `YYYY-MM-DD`)
- **No due date** (subtitle blank)

Subtasks (tasks with a `parent_id` that's also in the result set) render with a `>` indent prefix.

Priority shown on the right as `p1` / `p2` / `p3` for high-priority tasks; lowest priority is unmarked.

## Filter syntax

The filter string is sent verbatim to Todoist's `/tasks/filter` endpoint and uses Todoist's full filter language. Some practical patterns:

| You want                           | Filter                           |
|------------------------------------|----------------------------------|
| Today's tasks                      | `today`                          |
| Today + overdue                    | `today \| overdue`               |
| Just overdue                       | `overdue`                        |
| Next 7 days                        | `7 days`                         |
| High-priority only                 | `p1`                             |
| Today + urgent only                | `today & p1`                     |
| Tasks with a label                 | `@home`                          |
| Multiple labels (any)              | `(@home \| @errands)`            |
| Inbox tasks                        | `#Inbox`                         |
| Specific project                   | `#Work`                          |
| Assigned to me                     | `assigned to: me`                |
| Unassigned                         | `!assigned`                      |
| (today or overdue) and not waiting | `(today \| overdue) & !@waiting` |

Operators: `&` (AND), `|` (OR), `!` (NOT), parens for grouping.

**Priority gotcha**: Todoist's user-facing labels say p1 (urgent) → p4 (low). The API stores them inverted (p1 → API value 4). Use the **user-facing labels** in your filter; the device renders priorities consistently with what you'd expect from the Todoist app.

Full reference: [todoist.com/help/articles/introduction-to-filters-V98wIH](https://www.todoist.com/help/articles/introduction-to-filters-V98wIH).

## Wallpaper mode

When set to anything other than Off, after each successful task fetch the rendered list is captured from the framebuffer and saved as a 1-bit BMP. The CrossPoint sleep-screen system can then display it when the device sleeps — e-ink retains the last-rendered image without power, so your tasks stay visible.

| Mode     | File path             | Behavior                                                                                                                     |
|----------|-----------------------|------------------------------------------------------------------------------------------------------------------------------|
| Off      | (no file written)     | Default. Wallpaper feature disabled.                                                                                         |
| Rotation | `/.sleep/todoist.bmp` | Image joins your existing wallpaper rotation. CrossPoint's "Custom" sleep mode picks one BMP at random from `/.sleep/*.bmp`. |
| Always   | `/sleep.bmp`          | Image overwrites the SD-root single wallpaper file. **Destructive** if you have your own `/sleep.bmp`.                       |

For the wallpaper to actually display, you must also set:

> Settings → Display → **Sleep Screen** → **Custom**

The "Always" mode shows an overwrite-warning subtitle directly under its row. Future versions could add a confirmation prompt (option 2 in the original design discussion), but for personal use the inline hint is sufficient.

## Architecture

| Concern                                   | File                                                                        |
|-------------------------------------------|-----------------------------------------------------------------------------|
| Persisted token / filter / wallpaper mode | `lib/Todoist/TodoistCredentialStore.{h,cpp}`                                |
| HTTPS client + JSON parsing               | `lib/Todoist/TodoistClient.{h,cpp}`                                         |
| Task list activity                        | `src/activities/todoist/TodoistActivity.{h,cpp}`                            |
| Settings sub-screen                       | `src/activities/settings/TodoistSettingsActivity.{h,cpp}`                   |
| Home menu wiring                          | `src/activities/home/HomeActivity.{h,cpp}`                                  |
| Web settings rows                         | `src/SettingsList.h`                                                        |
| JSON save/load helpers                    | `src/JsonSettingsIO.{h,cpp}`                                                |
| i18n strings                              | `lib/I18n/translations/english.yaml` (regenerate via `scripts/gen_i18n.py`) |
| Boot init                                 | `src/main.cpp` (`TODOIST_STORE.loadFromFile()`)                             |

### Network stack

Mirrors `KOReaderSyncClient` exactly — `esp_http_client` + `esp_crt_bundle_attach` + 2KB `buffer_size` and `buffer_size_tx`. The default 16KB TLS buffers OOM during the handshake on ESP32-C3 (this was the OOM fix in upstream commit `14e1ce2`).

The CA trust anchor (Amazon Root CA 1, valid until 2037) is included in ESP-IDF's bundled CA store, so no custom PEM is embedded.

### JSON parsing

ArduinoJson with a `DeserializationOption::Filter` extracts only the four fields needed for display (`content`, `priority`, `parent_id`, `due.date`) and the pagination cursor (`next_cursor`). The other ~20 fields per task are dropped before allocation. A typical 30-task response is ~10KB compressed → ~2KB resident in `std::vector<TodoistTask>`.

Wire field names are Todoist's **legacy** names (`added_by_uid`, `child_order`, `responsible_uid`) — not the renamed aliases used by the official Python and TypeScript SDKs.

## Limitations

- **One page only** (limit=30 per fetch). No pagination UI. If your filter regularly returns more, narrow the filter.
- **Read-only**. No marking complete, editing, or creating tasks.
- **Manual refresh.** Press Confirm to re-fetch.
- **English only.** Other 21 translations fall back to English (the i18n script flags the gaps).
- **ASCII subtask indent** (`>`). Could be Unicode `↳` if the loaded font has the glyph.
- **Token exposed plaintext over the local web JSON API.** Consistent with how KOSync's password is handled — acceptable since the web is only reachable on the device's hotspot or LAN.
- **No retry / backoff on errors.** 429 / network failures display an error and stop. Press Confirm to retry.

## Version history

| Tag              | Highlights                                                                                                                                                         |
|------------------|--------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| `todoist-v1.0`   | Initial integration: credential store, HTTPS client, activity, settings entry, i18n. Token-field masking on the web settings page (generic improvement).           |
| `todoist-v1.1`   | Home menu entry; `TodoistSettingsActivity` sub-screen with on-device filter editing. Settings → Todoist now opens the sub-screen instead of the activity directly. |
| `todoist-v1.2`   | Optional sleep-screen wallpaper export (Off / Rotation / Always tri-state). _Broken — RenderLock held across `requestUpdateAndWait()`. Don't flash this tag._      |
| `todoist-v1.2.1` | Fix the v1.2 crash by releasing RenderLock before the blocking render. Use this as the v1.2 release.                                                               |

## References

- Todoist API v1: [developer.todoist.com/api/v1](https://developer.todoist.com/api/v1/)
- Todoist filter language: [todoist.com/help/articles/introduction-to-filters-V98wIH](https://www.todoist.com/help/articles/introduction-to-filters-V98wIH)
- Personal API token: [todoist.com/app/settings/integrations/developer](https://todoist.com/app/settings/integrations/developer)
