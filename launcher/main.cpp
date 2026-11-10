// Player launcher: edits the gamertag, avatar and onevs100.toml next to the
// game, then starts it.
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_sdlrenderer3.h>
#include <misc/cpp/imgui_stdlib.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <expected>
#include <filesystem>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "avatar_editor.h"
#include "content_check.h"
#include "files.h"
#include "game_config.h"
#include "install_location.h"
#include "player_files.h"
#include "ui_style.h"
#include "update_flow.h"
#include "updater.h"

namespace fs = std::filesystem;

namespace {

constexpr const char* kDiscordUrl = "https://discord.gg/tV3ajhwFTf";
constexpr const char* kWebsiteUrl = "https://1vs100.fans";
constexpr const char* kDocsUrl = "https://docs.1vs100.fans";
constexpr const char* kGithubUrl = "https://github.com/1vs100";
constexpr const char* kAvatarEditorUrl = "https://360sona.com";
constexpr float kTurnStep = 3.14159265f / 12.0f;  // 15 degrees per arrow press

struct App;
void RefreshFiles(App& app);

std::expected<void, Error> LaunchGame(const Paths& paths) {
  std::error_code ec;
  if (!fs::is_regular_file(paths.game, ec)) return std::unexpected(Error{ErrorKind::GameMissing, paths.game, {}});
  const std::string game = PathText(paths.game);
  const std::string base = PathText(paths.base);
  const char* args[] = {game.c_str(), nullptr};
  SDL_PropertiesID props = SDL_CreateProperties();
  SDL_SetPointerProperty(props, SDL_PROP_PROCESS_CREATE_ARGS_POINTER, args);
  SDL_SetStringProperty(props, SDL_PROP_PROCESS_CREATE_WORKING_DIRECTORY_STRING, base.c_str());
  SDL_SetBooleanProperty(props, SDL_PROP_PROCESS_CREATE_BACKGROUND_BOOLEAN, true);
  SDL_Process* process = SDL_CreateProcessWithProperties(props);
  SDL_DestroyProperties(props);
  if (!process) return std::unexpected(Error{ErrorKind::LaunchFailed, paths.game, SDL_GetError()});
  // Releases the handle only. A background process keeps running.
  SDL_DestroyProcess(process);
  return {};
}

int GamertagFilter(ImGuiInputTextCallbackData* data) {
  return GamertagChar(static_cast<unsigned char>(data->EventChar)) ? 0 : 1;
}

struct Status {
  std::string text;
  bool error = false;
};

struct App {
  Paths paths;
  // Buffers are deliberately larger than the validated limits so overlong
  // input reaches validation instead of being silently truncated by ImGui.
  std::array<char, 64> gamertag{};
  std::array<char, 512> host{};
  bool windowed = false;
  bool keyboard = false;
  bool mouse_look = false;
  bool native_renderer = false;
  bool allow_invalid_fetch_constants = true;
  std::optional<LogLevel> log_level;
  bool lua_print_log = false;
  Status status;
  // False while onevs100.toml could not be read. Launch then must not write
  // the defaults the controls show over the file.
  bool settingsLoaded = false;
  SDL_Window* window = nullptr;
  // SDL's user event for file dialog results; none when SDL ran out of event types.
  std::optional<Uint32> dialog_event;
  FilesCheck files;
  std::chrono::steady_clock::time_point filesChecked;
  std::string contentSetting = "content";
  std::vector<ConfigEntry> others;
  std::vector<std::string> removed;
  bool advanced = false;
  std::array<char, 64> newKey{};
  std::array<char, 512> newValue{};
  AvatarEditor avatar;
  std::array<char, 128> xboxGamertag{};
  updater::Flow update;
  void Report(const Error& error) { status = {Describe(error), true}; }
  void Report(std::string text) { status = {std::move(text), false}; }
  void ReportError(std::string text) { status = {std::move(text), true}; }
  void Show(const updater::Notice& notice) {
    if (notice.kind == updater::NoticeKind::Background && !status.text.empty()) return;
    status = {notice.text, notice.kind == updater::NoticeKind::Error};
  }
  void Show(const AvatarEditor::Message& message) {
    status = {message.text, message.severity == AvatarEditor::Message::Severity::Error};
  }

  bool Busy() { return avatar.Loading(); }
  // Background work whose result the UI waits for.
  bool NeedsFastPoll() { return Busy() || update.Busy(); }

  void Load() {
    if (auto name = LoadGamertag(paths.gamertag)) SDL_strlcpy(gamertag.data(), name->c_str(), gamertag.size());
    if (auto prepared = PrepareDataFolder(paths); !prepared) Report(prepared.error());
    SDL_strlcpy(host.data(), kDefaultHost, host.size());
    if (auto loaded = LoadConfig(); !loaded) Report(loaded.error());
    RefreshFiles(*this);
  }

  // Reads onevs100.toml into the controls. On failure they keep their values.
  std::expected<void, Error> LoadConfig() {
    auto settings = LoadSettings(paths.config);
    if (!settings) return std::unexpected(ToError(settings.error()));
    SDL_strlcpy(host.data(), settings->host.c_str(), host.size());
    windowed = settings->windowed;
    contentSetting = settings->content;
    keyboard = settings->keyboard;
    mouse_look = settings->mouse_look;
    native_renderer = settings->native_renderer;
    allow_invalid_fetch_constants = settings->allow_invalid_fetch_constants;
    log_level = settings->log_level;
    lua_print_log = settings->lua_print_log;
    others = std::move(settings->others);
    removed.clear();
    settingsLoaded = true;
    return {};
  }

  // The player may have fixed the file in an editor meanwhile.
  void RetryConfig() {
    if (settingsLoaded) return;
    if (auto loaded = LoadConfig(); !loaded) {
      Report(loaded.error());
      return;
    }
    Report("Loaded " + PathText(paths.config) + ".");
  }

  void SetContentFolder(std::string folder) {
    contentSetting = std::move(folder);
    RefreshFiles(*this);
    Report("Launch saves the content folder.");
  }

  std::expected<void, Error> Save() {
    if (!settingsLoaded) {
      if (auto loaded = LoadConfig(); !loaded) return loaded;
      RefreshFiles(*this);
      return std::unexpected(Error{ErrorKind::ConfigReread, paths.config, {}});
    }
    if (auto saved = SaveGamertag(paths, gamertag.data()); !saved) return saved;
    if (auto saved = avatar.Save(); !saved) return saved;
    Settings out;
    out.host = host.data();
    out.windowed = windowed;
    out.content = contentSetting;
    out.keyboard = keyboard;
    out.mouse_look = mouse_look;
    out.native_renderer = native_renderer;
    out.allow_invalid_fetch_constants = allow_invalid_fetch_constants;
    out.log_level = log_level;
    out.lua_print_log = lua_print_log;
    out.others = others;
    out.removed = removed;
    if (auto saved = SaveSettings(paths.config, out); !saved) return std::unexpected(ToError(saved.error()));
    removed.clear();
    return {};
  }
};

void RefreshFiles(App& app) {
  app.filesChecked = std::chrono::steady_clock::now();
  FilesCheck check = CheckContent(ResolveContentDir(app.paths, app.contentSetting), app.paths.shipped / "manifest.txt");
  // A load that failed may work with what is there now, such as a package that finished copying.
  if (check.avatarPacks != app.files.avatarPacks) app.avatar.PacksChanged();
  app.files = std::move(check);
}

enum class DialogKind : intptr_t { OpenAvatar, ExportAvatar, ContentFolder };

struct DialogRequest {
  App* app;
  DialogKind kind;
};

void SDLCALL OnFileChosen(void* userdata, const char* const* files, int) {
  auto* request = static_cast<DialogRequest*>(userdata);
  SDL_Event event{};
  event.type = *request->app->dialog_event;
  // A null list is an error, an empty list a cancel.
  event.user.code = files ? 0 : 1;
  event.user.data1 = (files && files[0]) ? SDL_strdup(files[0]) : nullptr;
  event.user.data2 = reinterpret_cast<void*>(static_cast<intptr_t>(request->kind));
  SDL_PushEvent(&event);
}

DialogRequest open_request{nullptr, DialogKind::OpenAvatar};
DialogRequest export_request{nullptr, DialogKind::ExportAvatar};
DialogRequest folder_request{nullptr, DialogKind::ContentFolder};

void HandleFileChosen(App& app, const SDL_UserEvent& event) {
  if (event.code) {
    const char* sdl_error = SDL_GetError();
    app.ReportError(std::string("File dialog failed.") +
                    ((sdl_error && *sdl_error) ? std::string(" ") + sdl_error : std::string()));
    return;
  }
  auto* chosen = static_cast<char*>(event.data1);
  if (!chosen) return;
  const fs::path path = PathFromUtf8(chosen);
  SDL_free(chosen);
  const auto kind = static_cast<DialogKind>(reinterpret_cast<intptr_t>(event.data2));
  if (kind == DialogKind::ExportAvatar) {
    if (auto message = app.avatar.Export(path)) app.Show(*message);
    return;
  }
  if (kind == DialogKind::ContentFolder) {
    app.SetContentFolder(PathText(path));
    return;
  }
  auto bytes = ReadBytes(path);
  if (!bytes) {
    app.Report(bytes.error());
    return;
  }
  if (auto message = app.avatar.Import(std::move(*bytes))) app.Show(*message);
}

void DrawPlayerTab(App& app) {
  SectionLabel("GAMERTAG");
  ImGui::SetNextItemWidth(-1);
  ImGui::InputTextWithHint("##gamertag", "Your name in the game", app.gamertag.data(), app.gamertag.size(),
                           ImGuiInputTextFlags_CallbackCharFilter, GamertagFilter);
  const std::string_view name(app.gamertag.data());
  if (!name.empty() && !ValidGamertag(name)) {
    ImGui::TextColored(kError, "1-15 chars: letters, numbers, spaces, _ or -; no leading/trailing space.");
  } else {
    SectionLabel("1-15 letters, numbers, spaces, _ or -");
  }
}

void DrawAvatarTab(App& app) {
  app.avatar.StartCatalog(app.files.avatarPacks);
  const ImGuiStyle& style = ImGui::GetStyle();
  const float preview_width = kPreviewWidth * ImGui::GetFontSize() / 18.0f;
  const float preview_height = preview_width * kPreviewHeight / kPreviewWidth;
  const float left_width = ImGui::GetContentRegionAvail().x - preview_width - style.ItemSpacing.x * 3;

  ImGui::BeginGroup();
  ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + left_width);
  static constexpr const char* kSteps[] = {
      "Create an account at 360sona.com and sign in.",
      "In the editor, click My Sonas, select your avatar and click Import / Export.",
      "Copy the 360sona avatar text and paste it below. Or click Save the description (.bin) and open that "
      "file here. Or enter your Xbox gamertag below, press Download and open the saved file.",
      "Check the preview, then press Launch.",
  };
  for (size_t i = 0; i < std::size(kSteps); ++i) {
    ImGui::Text("%zu.", i + 1);
    ImGui::SameLine();
    ImGui::TextUnformatted(kSteps[i]);
  }
  ImGui::PopTextWrapPos();
  if (ImGui::Button("Open 360sona.com")) SDL_OpenURL(kAvatarEditorUrl);

  ImGui::Dummy(ImVec2(0, 4));
  SectionLabel("SONA CODE");
  const bool ready = app.avatar.Ready();
  // A complete code shows at once, whether typed, pasted with Ctrl+V or with the button.
  bool changed = ImGui::InputTextMultiline("##code", app.avatar.code.data(), app.avatar.code.size(),
                                           ImVec2(left_width, ImGui::GetTextLineHeight() * 4));
  ImGui::BeginDisabled(!ready);
  if (ImGui::Button("Paste")) {
    char* clipboard = SDL_GetClipboardText();
    SDL_strlcpy(app.avatar.code.data(), clipboard ? clipboard : "", app.avatar.code.size());
    SDL_free(clipboard);
    changed = true;
  }
  ImGui::EndDisabled();
  if (changed && ready) {
    if (auto message = app.avatar.ImportCode()) app.Show(*message);
  }
  ImGui::SameLine();
  ImGui::BeginDisabled(!ready || !app.dialog_event);
  if (ImGui::Button("Open file...")) {
    static const SDL_DialogFileFilter filters[] = {{"Avatar manifest or sona code", "bin;txt"}};
    open_request.app = &app;
    SDL_ShowOpenFileDialog(OnFileChosen, &open_request, app.window, filters, 1, nullptr, false);
  }
  ImGui::EndDisabled();

  ImGui::Dummy(ImVec2(0, 4));
  SectionLabel("XBOX GAMERTAG");
  const float download_width = ImGui::CalcTextSize("Download").x + style.FramePadding.x * 2;
  ImGui::SetNextItemWidth(left_width - download_width - style.ItemSpacing.x);
  ImGui::InputTextWithHint("##xboxgamertag", "Your Xbox gamertag", app.xboxGamertag.data(), app.xboxGamertag.size());
  ImGui::SameLine();
  const auto xbox_gamertag = XboxGamertag::Parse(app.xboxGamertag.data());
  ImGui::BeginDisabled(!xbox_gamertag);
  if (ImGui::Button("Download")) {
    if (SDL_OpenURL(SonaManifestUrl(*xbox_gamertag).c_str())) {
      app.Report("Save the file from the browser, then open it with Open file...");
    } else {
      const char* sdl_error = SDL_GetError();
      app.ReportError(std::string("Cannot open the browser.") +
                      ((sdl_error && *sdl_error) ? std::string(" ") + sdl_error : std::string()));
    }
  }
  ImGui::EndDisabled();

  ImGui::Dummy(ImVec2(0, 4));
  SectionLabel("YOUR AVATAR");
  ImGui::BeginDisabled(!app.avatar.Shown() || !app.dialog_event);
  if (ImGui::Button("Export...")) {
    static const SDL_DialogFileFilter filters[] = {{"Avatar manifest", "bin"}};
    export_request.app = &app;
    const std::string location = PathText(app.paths.data / "avatarmanifest.bin");
    SDL_ShowSaveFileDialog(OnFileChosen, &export_request, app.window, filters, 1, location.c_str());
  }
  ImGui::EndDisabled();
  ImGui::SameLine();
  if (!app.files.hasAvatar) {
    ImGui::TextColored(kError, "The avatar pack table is missing. See the Files tab.");
  } else if (app.Busy()) {
    ImGui::TextUnformatted("Loading the avatar pack...");
  } else if (!app.avatar.Shown()) {
    ImGui::TextColored(kError, "No avatar yet. Paste a code or open a file.");
  } else if (app.avatar.Unsaved()) {
    ImGui::TextColored(kAccent, "New. Launch saves it.");
  } else {
    ImGui::TextUnformatted("Saved.");
  }
  ImGui::EndGroup();

  ImGui::SameLine(0, style.ItemSpacing.x * 3);
  ImGui::BeginGroup();
  const ImVec2 at = ImGui::GetCursorScreenPos();
  ImGui::GetWindowDrawList()->AddRectFilled(at, ImVec2(at.x + preview_width, at.y + preview_height),
                                            ImGui::GetColorU32(Hex(0x262930)), style.FrameRounding);
  if (SDL_Texture* preview = app.avatar.Preview()) {
    ImGui::Image(reinterpret_cast<ImTextureID>(preview), ImVec2(preview_width, preview_height));
    // Dragging across the preview turns the avatar too.
    if (ImGui::IsItemActive() || (ImGui::IsItemHovered() && ImGui::IsMouseDragging(ImGuiMouseButton_Left))) {
      const float delta = ImGui::GetIO().MouseDelta.x;
      if (delta != 0.0f) app.avatar.Turn(delta * 0.02f);
    }
  } else {
    ImGui::Dummy(ImVec2(preview_width, preview_height));
    if (app.Busy()) Spinner(ImVec2(at.x + preview_width * 0.5f, at.y + preview_height * 0.5f));
  }
  const float arrow = ImGui::GetFrameHeight();
  ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (preview_width - arrow * 2 - style.ItemSpacing.x) * 0.5f);
  ImGui::BeginDisabled(!app.avatar.Shown());
  ImGui::PushItemFlag(ImGuiItemFlags_ButtonRepeat, true);
  if (ImGui::ArrowButton("##left", ImGuiDir_Left)) app.avatar.Turn(-kTurnStep);
  ImGui::SameLine();
  if (ImGui::ArrowButton("##right", ImGuiDir_Right)) app.avatar.Turn(kTurnStep);
  ImGui::PopItemFlag();
  ImGui::EndDisabled();
  ImGui::EndGroup();
}

// Every other key in onevs100.toml. Saved with the rest when you press Launch.
void DrawAdvancedSettings(App& app) {
  ImGui::TextWrapped("Other keys in onevs100.toml. They are saved when you press Launch.");
  LinkText("What each setting does", kDocsUrl);
  const ImGuiStyle& style = ImGui::GetStyle();
  const float remove_width = ImGui::CalcTextSize("Remove").x + style.FramePadding.x * 2;
  std::optional<EntryError> new_value_error;
  if (ImGui::BeginTable("##others", 3, ImGuiTableFlags_SizingStretchProp)) {
    ImGui::TableSetupColumn("key", ImGuiTableColumnFlags_WidthStretch, 0.4f);
    ImGui::TableSetupColumn("value", ImGuiTableColumnFlags_WidthStretch, 0.6f);
    ImGui::TableSetupColumn("remove", ImGuiTableColumnFlags_WidthFixed, remove_width);
    std::optional<size_t> remove;
    for (size_t i = 0; i < app.others.size(); ++i) {
      ConfigEntry& entry = app.others[i];
      ImGui::PushID(static_cast<int>(i));
      ImGui::TableNextRow();
      ImGui::TableNextColumn();
      ImGui::AlignTextToFramePadding();
      ImGui::TextUnformatted(entry.key.c_str());
      ImGui::TableNextColumn();
      ImGui::SetNextItemWidth(-1);
      switch (entry.kind) {
        case ConfigEntry::Kind::Flag: ImGui::Checkbox("##value", &entry.flag); break;
        case ConfigEntry::Kind::Integer: {
          ImGui::InputScalar("##value", ImGuiDataType_S64, &entry.integer);
          break;
        }
        case ConfigEntry::Kind::Number: ImGui::InputDouble("##value", &entry.number); break;
        case ConfigEntry::Kind::Text:
          ImGui::InputText("##value", &entry.text);
          break;
      }
      ImGui::TableNextColumn();
      if (ImGui::Button("Remove")) remove = i;
      ImGui::PopID();
    }
    if (remove) {
      app.removed.push_back(app.others[*remove].key);
      app.others.erase(app.others.begin() + static_cast<std::ptrdiff_t>(*remove));
    }
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::SetNextItemWidth(-1);
    ImGui::InputTextWithHint("##newkey", "name", app.newKey.data(), app.newKey.size(),
                             ImGuiInputTextFlags_CharsNoBlank);
    ImGui::TableNextColumn();
    ImGui::SetNextItemWidth(-1);
    ImGui::InputTextWithHint("##newvalue", "value", app.newValue.data(), app.newValue.size());
    ImGui::TableNextColumn();
    const std::string_view key(app.newKey.data());
    const bool taken = ManagedKey(key) || std::any_of(app.others.begin(), app.others.end(),
                                                      [&](const ConfigEntry& entry) { return entry.key == key; });
    const auto entry = EntryFromText(std::string(key), app.newValue.data());
    if (!entry) new_value_error = entry.error();
    ImGui::BeginDisabled(!ValidConfigKey(key) || taken || !entry);
    if (ImGui::Button("Add", ImVec2(remove_width, 0))) {
      app.others.push_back(*entry);
      std::erase(app.removed, std::string(key));
      app.newKey.fill(0);
      app.newValue.fill(0);
    }
    ImGui::EndDisabled();
    ImGui::EndTable();
  }
  if (ManagedKey(app.newKey.data())) SectionLabel("That setting has its own control above.");
  if (new_value_error) ImGui::TextColored(kError, "%s", Describe(*new_value_error).c_str());
}

void DrawSettingsTab(App& app) {
  SectionLabel("GAMESERVER HOST");
  const float reset_width = ImGui::CalcTextSize("Reset").x + ImGui::GetStyle().FramePadding.x * 2;
  ImGui::SetNextItemWidth(-(reset_width + ImGui::GetStyle().ItemSpacing.x));
  ImGui::InputTextWithHint("##host", kDefaultHost, app.host.data(), app.host.size(),
                           ImGuiInputTextFlags_CharsNoBlank);
  ImGui::SameLine();
  ImGui::BeginDisabled(std::strcmp(app.host.data(), kDefaultHost) == 0);
  if (ImGui::Button("Reset")) SDL_strlcpy(app.host.data(), kDefaultHost, app.host.size());
  ImGui::EndDisabled();
  SectionLabel("Default: 1vs100.fans. Hostname or IPv4, no spaces, : or /.");
  if (!std::string_view(app.host.data()).empty() && !ValidHost(app.host.data()))
    ImGui::TextColored(kError, "That host will not resolve: use a plain hostname or IPv4 address.");

  ImGui::Dummy(ImVec2(0, 6));
  SectionLabel("DISPLAY");
  ImGui::Checkbox("Windowed mode", &app.windowed);
  if (kHasNativeRenderer) ImGui::Checkbox("Native renderer", &app.native_renderer);
  ImGui::Checkbox("Allow invalid fetch constants", &app.allow_invalid_fetch_constants);

  ImGui::Dummy(ImVec2(0, 6));
  SectionLabel("CONTROLS");
  ImGui::Checkbox("Keyboard controls (WASD + arrows, Space = A)", &app.keyboard);
  ImGui::BeginDisabled(!app.keyboard);
  ImGui::Checkbox("Mouse aims the right stick", &app.mouse_look);
  ImGui::EndDisabled();

  ImGui::Dummy(ImVec2(0, 6));
  SectionLabel("LOGS");
  ImGui::SetNextItemWidth(ImGui::CalcTextSize("default (info)").x * 2);
  if (ImGui::BeginCombo("Log level", app.log_level ? LogLevelName(*app.log_level) : "default (info)")) {
    if (ImGui::Selectable("default (info)", !app.log_level)) app.log_level.reset();
    for (const LogLevel level : kLogLevels) {
      if (ImGui::Selectable(LogLevelName(level), app.log_level == level)) app.log_level = level;
    }
    ImGui::EndCombo();
  }
  ImGui::Checkbox("Write the game's Lua output to the log", &app.lua_print_log);
  SectionLabel("debug and trace make the log much larger. Turn them on to report a problem.");
  ImGui::TextWrapped("%s", PathText(app.paths.data / "logs").c_str());

  if (app.update.IsOpen()) {
    ImGui::Dummy(ImVec2(0, 6));
    SectionLabel("UPDATES");
    bool check = app.update.CheckOnStart();
    if (ImGui::Checkbox("Check for updates when the launcher starts", &check)) {
      if (auto failed = app.update.SetCheckOnStart(check)) app.Show(*failed);
    }
    SectionLabel("Asks GitHub for the newest release. Nothing is installed until you press Update.");
  }

  ImGui::Dummy(ImVec2(0, 6));
  SectionLabel("DATA FOLDER");
  ImGui::TextWrapped("%s", PathText(app.paths.data).c_str());
  const fs::path player = app.paths.profiles.parent_path();
  if (player == app.paths.data / "userdata") {
    SectionLabel("Holds onevs100.toml, userdata and the default content folder.");
  } else {
    SectionLabel("Holds onevs100.toml and the default content folder. Player data (ONEVS100_PLAYER_DATA):");
    ImGui::TextWrapped("%s", PathText(player).c_str());
  }

  ImGui::Dummy(ImVec2(0, 6));
  ImGui::Checkbox("Show all settings", &app.advanced);
  if (app.advanced) DrawAdvancedSettings(app);
}

const char* RoleLabel(FileRole role) {
  switch (role) {
    case FileRole::Game: return "Game package";
    case FileRole::Launch: return "Launch content";
    case FileRole::Show: return "Show pack";
    case FileRole::Episode: return "Episode pack";
    case FileRole::ShowEpisode: return "Show / episode pack";
    case FileRole::Avatar: return "Avatar pack table";
    case FileRole::Other: return "Other content";
    case FileRole::Invalid: return "Not a package";
  }
  return "";
}

void FileRow(Indicator indicator, const char* label, const std::string& detail) {
  StatusIcon(indicator);
  ImGui::SameLine();
  ImGui::TextUnformatted(label);
  if (!detail.empty()) {
    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
    ImGui::TextUnformatted(detail.c_str());
    ImGui::PopStyleColor();
  }
}

void ContentRow(const ContentFile& file) {
  if (!file.present) {
    FileRow(Indicator::Missing, RoleLabel(file.role),
            file.name + (file.role == FileRole::Avatar ? " - missing, or add the other avatar file" : " - missing"));
  } else if (!file.valid) {
    FileRow(Indicator::Caution, RoleLabel(file.role), file.name + " - " + file.note);
  } else {
    FileRow(Indicator::Ok, RoleLabel(file.role), file.name);
  }
}

// Files copied in while the launcher is in front show up within a few seconds.
// Package headers are read again only when the folder listing changed.
void CheckFiles(App& app) {
  const auto now = std::chrono::steady_clock::now();
  if (now - app.filesChecked < std::chrono::seconds(2)) return;
  app.filesChecked = now;
  if (ListFolder(app.files.dir) != app.files.listing) RefreshFiles(app);
}

void DrawFilesTab(App& app) {
  const FilesCheck& check = app.files;
  ImGui::TextWrapped("Copy the packages from your Xbox 360 into the content folder: the game, its launch content, the "
                     "show and episode packs, and the avatar pack (FFFE07DF00000002 or AvatarAssetPack.toc). Put "
                     "them straight in the folder, untouched and without subfolders. This list updates by itself, "
                     "and Launch unlocks once every row is green.");
  ImGui::Dummy(ImVec2(0, 4));
  SectionLabel("CONTENT FOLDER");
  ImGui::TextWrapped("%s", PathText(check.dir).c_str());
  ImGui::BeginDisabled(!app.dialog_event);
  if (ImGui::Button("Set folder...")) {
    folder_request.app = &app;
    const std::string location = PathText(check.dir);
    SDL_ShowOpenFolderDialog(OnFileChosen, &folder_request, app.window, location.c_str(), false);
  }
  ImGui::EndDisabled();
  ImGui::SameLine();
  ImGui::BeginDisabled(app.contentSetting == kDefaultContent);
  if (ImGui::Button("Reset")) app.SetContentFolder(kDefaultContent);
  ImGui::EndDisabled();
  ImGui::SameLine();
  SectionLabel("Default: the content folder next to onevs100.toml.");
  if (!check.dirOk) {
    FileRow(Indicator::Missing, "Folder missing", "create it and add the game packages");
  } else if (check.files.empty()) {
    FileRow(Indicator::Missing, "No files", "copy the untouched game, show and episode packages here");
  }
  auto drawRole = [&](FileRole role) {
    for (const auto& file : check.files) {
      if (file.role == role) ContentRow(file);
    }
  };
  auto hasRole = [&](FileRole role) {
    for (const auto& file : check.files) {
      if (file.role == role) return true;
    }
    return false;
  };
  drawRole(FileRole::Game);
  if (check.dirOk && !hasRole(FileRole::Game)) FileRow(Indicator::Missing, "Game package", "not found");
  drawRole(FileRole::Launch);
  if (check.dirOk && !hasRole(FileRole::Launch)) FileRow(Indicator::Missing, "Launch content", "not found");
  // One pack is enough to play, so a partial set is a caution, not an error.
  auto packSummary = [&](const char* label, FileRole role, size_t found) {
    const size_t expected = static_cast<size_t>(
        std::count_if(check.files.begin(), check.files.end(), [&](const ContentFile& file) { return file.role == role && !file.duplicate; }));
    const std::string detail = std::to_string(found) + " of " + std::to_string(expected) + " found";
    FileRow(found == expected ? Indicator::Ok : found ? Indicator::Caution : Indicator::Missing, label, detail);
    drawRole(role);
  };
  if (check.manifestOk) {
    packSummary("Show packs", FileRole::Show, check.showCount);
    packSummary("Episode packs", FileRole::Episode, check.episodeCount);
  } else {
    char packCount[64] = {};
    SDL_snprintf(packCount, sizeof(packCount), "%zu found", check.showEpisodeCount);
    if (check.showEpisodeCount > 0)
      FileRow(Indicator::Ok, "Show / episode packs", packCount);
    else
      FileRow(Indicator::Missing, "Show / episode packs", "none found");
    drawRole(FileRole::ShowEpisode);
  }
  drawRole(FileRole::Other);
  drawRole(FileRole::Invalid);
  ImGui::Dummy(ImVec2(0, 6));
  SectionLabel("AVATAR DATA");
  // Either the pack table or the package holding it is enough, so once one is there the other is not missing.
  for (const auto& file : check.files) {
    if (file.role == FileRole::Avatar && (file.present || !check.hasAvatar)) ContentRow(file);
  }
  if (check.dirOk && !hasRole(FileRole::Avatar))
    FileRow(Indicator::Missing, "Avatar pack table", "add AvatarAssetPack.toc or the FFFE07DF00000002 package");
}

// Returns true when the launcher should close.
bool DrawFrame(App& app, const Fonts& fonts) {
  const updater::PollResult polled = app.update.Poll();
  if (polled.notice) app.Show(*polled.notice);
  bool close = polled.exit;
  if (auto message = app.avatar.PollCatalog()) app.Show(*message);
  CheckFiles(app);
  const ImGuiViewport* viewport = ImGui::GetMainViewport();
  ImGui::SetNextWindowPos(viewport->WorkPos);
  ImGui::SetNextWindowSize(viewport->WorkSize);
  ImGui::Begin("##launcher", nullptr,
               ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                   ImGuiWindowFlags_NoBringToFrontOnFocus);

  ImGui::PushFont(fonts.title);
  ImGui::TextUnformatted("1 vs 100");
  ImGui::PopFont();
  SectionLabel("PC launcher");
  {
    const ImVec2 at = ImGui::GetCursorScreenPos();
    const float width = ImGui::GetContentRegionAvail().x;
    ShowGradient(ImGui::GetWindowDrawList(), at, ImVec2(at.x + width, at.y + 3));
    ImGui::Dummy(ImVec2(width, 3));
  }
  ImGui::Dummy(ImVec2(0, 4));

  const ImGuiStyle& style = ImGui::GetStyle();
  const float line = ImGui::GetTextLineHeightWithSpacing();
  const float launch_height = ImGui::GetFrameHeight() * 1.6f;
  const float footer = launch_height + line * 5 + style.ItemSpacing.y * 5;
  if (ImGui::BeginTabBar("##tabs")) {
    if (ImGui::BeginTabItem("Player")) {
      ImGui::BeginChild("##player", ImVec2(0, -footer), ImGuiChildFlags_AlwaysUseWindowPadding);
      DrawPlayerTab(app);
      ImGui::EndChild();
      ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("Avatar")) {
      ImGui::BeginChild("##avatar", ImVec2(0, -footer), ImGuiChildFlags_AlwaysUseWindowPadding);
      DrawAvatarTab(app);
      ImGui::EndChild();
      ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("Settings")) {
      ImGui::BeginChild("##settings", ImVec2(0, -footer), ImGuiChildFlags_AlwaysUseWindowPadding);
      DrawSettingsTab(app);
      ImGui::EndChild();
      ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("Files")) {
      ImGui::BeginChild("##files", ImVec2(0, -footer), ImGuiChildFlags_AlwaysUseWindowPadding);
      DrawFilesTab(app);
      ImGui::EndChild();
      ImGui::EndTabItem();
    }
    ImGui::EndTabBar();
  }

  // Two lines are reserved for the status so the layout does not shift.
  const float status_top = ImGui::GetCursorPosY();
  ImGui::PushStyleColor(ImGuiCol_Text, app.status.error ? kError
                                                        : style.Colors[ImGuiCol_TextDisabled]);
  ImGui::TextWrapped("%s", app.status.text.c_str());
  ImGui::PopStyleColor();
  ImGui::SetCursorPosY(std::max(ImGui::GetCursorPosY(), status_top + line * 2));

  // Velopack stops every process in the install when it applies an update.
  const bool can_launch = app.files.ok() && app.avatar.HasAvatar() && !app.update.Downloading();
  ImGui::BeginDisabled(!can_launch);
  if (ShowButton("LAUNCH", ImVec2(ImGui::GetContentRegionAvail().x, launch_height), can_launch)) {
    if (auto saved = app.Save(); !saved) {
      app.Report(saved.error());
    } else if (auto launched = LaunchGame(app.paths); !launched) {
      app.Report(launched.error());
    } else {
      close = true;
    }
  }
  ImGui::EndDisabled();

  if (!app.files.ok()) {
    ImGui::TextColored(kError, "Missing files. See the Files tab before launching.");
  } else if (!app.avatar.HasAvatar()) {
    ImGui::TextColored(kError, "Choose an avatar on the Avatar tab before launching.");
  } else {
    SectionLabel("Launch saves your gamertag, avatar and settings.");
  }

  LinkText("1vs100.fans", kWebsiteUrl);
  ImGui::SameLine(0, style.ItemSpacing.x * 3);
  LinkText("Documentation", kDocsUrl);
  ImGui::SameLine(0, style.ItemSpacing.x * 3);
  LinkText("GitHub", kGithubUrl);
  ImGui::SameLine(0, style.ItemSpacing.x * 3);
  LinkText("Discord", kDiscordUrl);
  ImGui::AlignTextToFramePadding();
  SectionLabel("1 vs 100 Reborn " ONEVS100_RELEASE_VERSION ", build " ONEVS100_BUILD_VERSION);
  {
    const float closeWidth = ImGui::CalcTextSize("Close").x + style.FramePadding.x * 2;
    std::string update_label;
    if (app.update.Downloading()) {
      update_label = "Downloading " + std::to_string(app.update.DownloadPercent()) + "%";
    } else if (auto available = app.update.Available()) {
      update_label = "Update to " + available->text;
    }
    if (!update_label.empty()) {
      // A fixed ID, since the label changes with the progress.
      update_label += "###update";
      // Sized for the widest progress text so the button does not move.
      const float updateWidth =
          std::max(ImGui::CalcTextSize(update_label.c_str(), nullptr, true).x, ImGui::CalcTextSize("Downloading 100%").x) +
          style.FramePadding.x * 2;
      ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - closeWidth - style.ItemSpacing.x - updateWidth);
      ImGui::BeginDisabled(app.update.Busy());
      if (ImGui::Button(update_label.c_str())) app.update.StartDownload();
      ImGui::EndDisabled();
    }
    ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - closeWidth);
    if (ImGui::Button("Close")) close = true;
  }
  ImGui::End();
  return close;
}

}  // namespace

int main(int, char**) {
  // Velopack opens its own log file, so only its installs run its hooks.
  if (IsVelopackInstall(ExecutableFolder())) updater::RunStartupHooks();
  SDL_SetHint(SDL_HINT_APP_NAME, "1 vs 100 Launcher");
  // The click that focuses the window also presses what is under the pointer.
  SDL_SetHint(SDL_HINT_MOUSE_FOCUS_CLICKTHROUGH, "1");
  if (!SDL_Init(SDL_INIT_VIDEO)) {
    SDL_Log("SDL_Init failed: %s", SDL_GetError());
    return 1;
  }
  auto paths = MakePaths();
  if (!paths) {
    const std::string text = Describe(paths.error());
    SDL_Log("%s", text.c_str());
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "1 vs 100", text.c_str(), nullptr);
    SDL_Quit();
    return 1;
  }
  const float scale = SDL_GetDisplayContentScale(SDL_GetPrimaryDisplay()) > 0
                          ? SDL_GetDisplayContentScale(SDL_GetPrimaryDisplay())
                          : 1.0f;
  SDL_Window* window = nullptr;
  SDL_Renderer* renderer = nullptr;
  if (!SDL_CreateWindowAndRenderer("1 vs 100", static_cast<int>(800 * scale), static_cast<int>(900 * scale), 0,
                                   &window, &renderer)) {
    SDL_Log("Window creation failed: %s", SDL_GetError());
    SDL_Quit();
    return 1;
  }
  SDL_SetRenderVSync(renderer, 1);

  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGui::GetIO().IniFilename = nullptr;
  ApplyStyle(scale);
  const Fonts fonts = LoadFonts(scale);
  ImGui_ImplSDL3_InitForSDLRenderer(window, renderer);
  ImGui_ImplSDLRenderer3_Init(renderer);

  App app;
  app.paths = std::move(*paths);
  app.window = window;
  app.avatar.Init(app.paths.avatar, renderer);
  // SDL_RegisterEvents returns 0 when no event types are left.
  if (const Uint32 type = SDL_RegisterEvents(1)) app.dialog_event = type;
  if (!app.dialog_event) {
    SDL_Log("No user event for the file dialogs; they are disabled.");
    app.status = {"File dialogs are unavailable in this build.", true};
  }
  app.Load();
  if (IsVelopackInstall(app.paths.base)) {
    if (auto failed = app.update.Open(app.paths.launcher_config, app.paths.game)) app.Show(*failed);
  }

  bool running = true;
  while (running) {
    SDL_Event event;
    // Idle until input arrives; the UI has no animation. Poll faster while the avatar pack loads.
    if (SDL_WaitEventTimeout(&event, app.NeedsFastPoll() ? 50 : 500)) {
      do {
        ImGui_ImplSDL3_ProcessEvent(&event);
        if (event.type == SDL_EVENT_QUIT) running = false;
        // Coming back from a file manager is when new files usually appear.
        if (event.type == SDL_EVENT_WINDOW_FOCUS_GAINED) {
          app.RetryConfig();
          RefreshFiles(app);
        }
        if (app.dialog_event && event.type == *app.dialog_event)
          HandleFileChosen(app, event.user);
      } while (SDL_PollEvent(&event));
    }
    if (SDL_GetWindowFlags(window) & SDL_WINDOW_MINIMIZED) continue;

    // ImGui applies one press or release per frame, so a quick click needs a
    // second frame to land before the loop goes idle again.
    for (int pass = 0; pass < 2 && running; ++pass) {
      ImGui_ImplSDLRenderer3_NewFrame();
      ImGui_ImplSDL3_NewFrame();
      ImGui::NewFrame();
      if (DrawFrame(app, fonts)) running = false;
      ImGui::Render();
      SDL_SetRenderDrawColor(renderer, 0x0d, 0x0b, 0x1f, 255);
      SDL_RenderClear(renderer);
      ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), renderer);
      SDL_RenderPresent(renderer);
    }
  }

  app.avatar.ReleasePreview();
  ImGui_ImplSDLRenderer3_Shutdown();
  ImGui_ImplSDL3_Shutdown();
  ImGui::DestroyContext();
  SDL_DestroyRenderer(renderer);
  SDL_DestroyWindow(window);
  SDL_Quit();
  return 0;
}
