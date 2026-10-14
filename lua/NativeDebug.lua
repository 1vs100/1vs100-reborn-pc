-- NativeDebug.lua --
-- Launcher Debug tab options for the native PC build. Everything here stays
-- off unless the launch script sets the matching NATIVE_* global (appended to
-- the staged globals.lua, which loads before this file).
--
--   NATIVE_SPONSORSHIP = false     sponsor-free studio (nosponsor wall), the
--                                  jumbotron info panel kept
--   NATIVE_SHOW_SKIN = 'Holiday'   themed studio screens (episode package)
--   NATIVE_HOST_TOOL = 'host'      original Host Tool: spectate with the
--                                  producer stats overlay (does not play)
--                    = 'producer'  play, with the producer stats overlay
--   NATIVE_DEBUG_INPUT = true      original debug menu (Back) and free camera
--                                  (left stick click, triggers move it)
--   NATIVE_SPONSOR_DANCE = true    award the sponsor dance to every player
-- Simple original switches (DEFAULT_FAKE_LIVE_HOST, DEFAULT_SHOW_SUBTITLES,
-- CONTENT_USE_FAKEREALS) are set directly in globals.lua by the launch script.

NativeDebug = NativeDebug or {}
local D = NativeDebug

local function log(text)
    if type(NativeDebugLog) == 'function' then pcall(NativeDebugLog, 'DEBUG: ' .. tostring(text)) end
end

----------------------------------------------------------------------
-- Show skin: the episode sponsor selects themed scenes in episode.xzp
-- (PTSetScreens_<name>, SetPipeColor_<name>, StageFeedback_<name>, ...).
-- Scenes a theme does not have fall back to the standard ones.
----------------------------------------------------------------------
D.SKINS = { Holiday = 'Holiday', TheBigGame = 'TheBigGame', XboxLIVE = 'Xbox LIVE',
            ColinCorp = 'ColinCorp', Sprint = 'Sprint' }
local LOCALES = { 'en-us', 'en-gb', 'en-ca', 'en-ie', 'en-au', 'fr-fr', 'fr-ca', 'de-de', 'es-es', 'es-mx', 'it-it' }
-- Called by the offline grid fixture (program.lua) for the session's EpisodeSponsor.
function NativeEpisodeSponsor()
    local skin = D.SKINS[tostring(NATIVE_SHOW_SKIN or '')]
    if not skin then return nil end
    local byLocale = {}
    for _, locale in ipairs(LOCALES) do byLocale[locale] = skin end
    log('show skin: ' .. skin)
    return byLocale
end

-- Some original themed scenes are incomplete: SetPipeColor_Xbox LIVE has no
-- SponsorGroup (crash, 2026-10-11 10:20 log), two ColinCorp lighting scenes lack
-- effect groups. When the game asks a themed scene for a part it does not have,
-- that one scene is swapped for the standard scene and its parts are fetched
-- again from it; the rest of the skin stays.
local function sceneKeys(group)
    local keys = {}
    for key in pairs(group) do keys[key] = true end
    return keys
end
function D.InstallSkinFallback()
    local skin = D.SKINS[tostring(NATIVE_SHOW_SKIN or '')]
    if D.skinFallback or not skin or type(SceneUI) ~= 'table' or type(SceneUI.CreateSceneUI) ~= 'function' then return end
    D.skinFallback = true
    local suffix = '_' .. skin
    local themed = {}
    local engineCreate = SceneUI.CreateSceneUI
    SceneUI.CreateSceneUI = function(name, resource, clipMode, ...)
        local group = engineCreate(name, resource, clipMode, ...)
        if group and type(name) == 'string' and #name > #suffix and string.sub(name, -#suffix) == suffix then
            themed[group] = { name = name, base = string.sub(name, 1, #name - #suffix), resource = resource,
                              clipMode = clipMode, calls = {} }
        end
        return group
    end
    local function swapToStandard(group, info, failure)
        themed[group] = nil
        if type(LoadXuiScene) ~= 'function' then return false end
        local ok, scene = pcall(LoadXuiScene, info.base, info.resource or 'Launch', true, info.clipMode)
        if not ok or not scene then return false end
        for _, call in ipairs(info.calls) do
            for key in pairs(call.keys) do group[key] = nil end
        end
        local old = group.scene
        group.scene = scene
        if old then pcall(function() old:Dispose() end) end
        for _, call in ipairs(info.calls) do call.fn(group, unpack(call.args, 1, call.n)) end
        log('skin scene ' .. info.name .. ' is missing a part (' .. tostring(failure) .. '); using the standard ' .. info.base)
        return true
    end
    local function guard(name)
        local engineAdd = SceneUI[name]
        if type(engineAdd) ~= 'function' then return end
        SceneUI[name] = function(group, ...)
            local info = themed[group]
            if not info then return engineAdd(group, ...) end
            local before = sceneKeys(group)
            local ok, result = pcall(engineAdd, group, ...)
            local added = {}
            for key in pairs(group) do if not before[key] then added[key] = true end end
            if ok then
                info.calls[#info.calls + 1] = { fn = engineAdd, args = { ... }, n = select('#', ...), keys = added }
                return result
            end
            for key in pairs(added) do group[key] = nil end
            if not swapToStandard(group, info, result) then error(result, 0) end
            return engineAdd(group, ...)
        end
    end
    guard('AddElement'); guard('AddProgressBar'); guard('AddAvatar')
    log('show skin fallback ready for ' .. skin)
end
pcall(D.InstallSkinFallback)

----------------------------------------------------------------------
-- Server config switches (the local service's config table)
----------------------------------------------------------------------
function D.ApplyConfig()
    local config = Lua_HVStore and Lua_HVStore['v1.Setup.MGS.1vs100.Config']
    if type(config) ~= 'table' then return end
    if NATIVE_SPONSORSHIP == false then
        for key in pairs(config) do
            if type(key) == 'string' and string.find(key, '^DisableSponsorship_') then config[key] = true end
        end
        -- The jumbotron's countdown/players/amps panel is the sponsor's "Now"
        -- widget; keep it without the sponsor.
        CONTENT_USE_NOW_WIDGET = true
        log('sponsor off: sponsor-free studio, jumbotron panel kept')
    end
    if NATIVE_SPONSOR_DANCE then
        config.AWARD_SPONSOR_DANCE = true
        DEFAULT_AWARD_SPONSOR_DANCE = true
        log('sponsor dance awarded to every player')
    end
end
D.ApplyConfig()

----------------------------------------------------------------------
-- Original Host Tool (hosttools.lua). In the original it was unlocked by a
-- special content package; here the first local player gets it.
----------------------------------------------------------------------
function D.StartHostTool()
    if D.hostToolStarted or (NATIVE_HOST_TOOL ~= 'host' and NATIVE_HOST_TOOL ~= 'producer') then return end
    if type(CreateGlobalTask) ~= 'function' then return end
    D.hostToolStarted = true
    CreateGlobalTask(function()
        for _ = 1, 6000 do
            local list = type(Players) == 'table' and type(Players.GetLocalPlayerList) == 'function' and Players.GetLocalPlayerList()
            local player = type(list) == 'table' and list[1]
            if player and player.controllerID and type(Host) == 'table' and type(Host.JoinGame) == 'function' then
                if NATIVE_HOST_TOOL == 'host' then
                    Host.UnlockHostTool(player.controllerID)
                else
                    Host.JoinGame(player.playerID, player.controllerID, false)
                end
                log('Host Tool on as ' .. NATIVE_HOST_TOOL .. ' (LT/RT change pages, right stick click shows/hides)')
                return
            end
            Wait(0.1)
        end
        log('Host Tool: no local player appeared; not started')
    end)
end

----------------------------------------------------------------------
-- Original debug input. The game only accepts it in non-retail builds; lift
-- that check while it handles a controller message, and open the original
-- debug menu with Back.
----------------------------------------------------------------------
function D.InstallDebugInput()
    if D.debugInputInstalled or not NATIVE_DEBUG_INPUT or type(LuaDebugInput) ~= 'function' then return end
    D.debugInputInstalled = true
    local engineDebugInput = LuaDebugInput
    LuaDebugInput = function(message)
        if type(GameInformation) == 'table' then GameInformation.allowDebugInput = true end
        local retailGate = IsNotRetailBuild
        IsNotRetailBuild = function() return true end
        local ok, result = pcall(engineDebugInput, message)
        if ok and result == nil and type(message) == 'table' and message.BUTTON_PRESSED and
           message.button == BUTTON_BACK and type(DebugMenu) == 'table' and not DebugMenu.bVisible and
           type(ShowDebugMenu) == 'function' then
            pcall(ShowDebugMenu, message.user)
            result = 'DEBUG_MENU'
        end
        IsNotRetailBuild = retailGate
        if not ok then log('debug input error: ' .. tostring(result)); return nil end
        return result
    end
    log('debug input on: Back opens the debug menu; left stick click toggles the free camera')
end

-- Start what needs the running game once the service is first used.
if LuaServer and type(LuaServer.MockGetMessage) == 'function' and not D.hooked then
    D.hooked = true
    local engineGet = LuaServer.MockGetMessage
    LuaServer.MockGetMessage = function(...)
        if not D.started then
            D.started = true
            pcall(D.InstallSkinFallback)
            pcall(D.InstallDebugInput)
            pcall(D.StartHostTool)
        end
        return engineGet(...)
    end
end
