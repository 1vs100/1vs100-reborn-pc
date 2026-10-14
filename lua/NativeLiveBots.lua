-- NativeLiveBots.lua --
-- Live (Primetime) shows in the native PC build: bots fill the Mob seats that
-- real players do not take, so a show always has a full 100-member Mob.
--
-- The original game had an AI Mob (mobai.lua, Server_SubmitAIAnswers_Task in
-- serverquestionstate.lua, GameData.AIMob tuning in data.lua) but shipped with
-- it switched off (DEFAULT_NO_AI_MOB) and no longer wired in. Elimination is
-- counted from the Mob's answers: Server_GetTrustMob reads the round's
-- MobChoices list and treats every Mob member without a correct answer as
-- eliminated (GetQuestionMobSize() - correctCount). With only real players'
-- answers in that list, the empty seats were all eliminated at question 1.
--
-- Here each round starts with one bot per empty seat (100 minus the real Mob
-- members), each with a skill level. Every question, each bot still standing
-- answers with the original AI Mob formula:
--   chance correct = baseProbCorrect - skillDiffModifier * (difficulty - skill)
-- and a wrong answer eliminates it. Their answers are added to the MobChoices
-- list the show reads, in the same form real Mob answers take.
--
-- Off when the launcher's bots option is off (NATIVE_DISABLE_BOTS), outside
-- Live shows, and on room clients (only the host runs bots).

NativeLiveBots = NativeLiveBots or {}
local B = NativeLiveBots

B.SKILL_MEAN = 4          -- on the GameData.AIMob difficulty scale (0 very easy .. 8 very hard)
B.SKILL_SPREAD = 2
B.MOB_SIZE = 100
B.round = nil
B.bots = {}
B.answers = {}            -- "round:question" -> bot answers already given

local function log(text)
    if type(NativeDebugLog) == 'function' then pcall(NativeDebugLog, 'LIVEBOTS: ' .. tostring(text)) end
end
B.Log = log

local function random() return math.random() end
local function normal()
    local u1, u2 = math.max(random(), 1e-9), random()
    return math.sqrt(-2 * math.log(u1)) * math.cos(2 * math.pi * u2)
end

local function humanMobCount()
    return type(g_TheMob) == 'table' and #g_TheMob or 0
end

function B.IsActive()
    if NATIVE_DISABLE_BOTS then return false end
    if not (type(isPrimetimeGame) == 'function' and CurrentProgram and isPrimetimeGame()) then return false end
    if NativeRoom and NativeRoom.active and not NativeRoom.host and not NativeRoom.solo then return false end
    return true
end

-- Bots need XUID-shaped ids: the One posts the Mob's answers to the server
-- state with each XUID converted to hex (EncodeIndividualMobAnswers), and a
-- non-numeric id ("bot5") made that a nil table index, which ended the show
-- after the first question (2026-10-11 02:03 log). Decimal strings in
-- 0x0009FFFF000002E0.., outside the range local players are given.
function B.BotXuid(i) return '2814745472140' .. string.format('%03d', i) end
function B.StartRound(round)
    local count = math.max(0, B.MOB_SIZE - humanMobCount())
    B.bots, B.answers, B.round = {}, {}, round
    for i = 1, count do
        local skill = math.floor(B.SKILL_MEAN + B.SKILL_SPREAD * normal() + 0.5)
        B.bots[i] = { id = B.BotXuid(i), skill = math.max(0, math.min(10, skill)), alive = true }
    end
    log(string.format('round %s: %d bots join the Mob beside %d player(s)', tostring(round), count, humanMobCount()))
end

local function tuning()
    local ai = GameData and GameData.AIMob or {}
    return ai.DifficultyLookup or {}, tonumber(ai.baseProbCorrect) or 0.5, tonumber(ai.skillDiffModifier) or 0.15
end

local function wrongChoice(correct)
    local options = {}
    for choice = 1, 3 do if choice ~= correct then options[#options + 1] = choice end end
    return options[math.random(#options)]
end

-- The bots' answers to one question (given once; repeated reads return the same).
function B.AnswersFor(round, question)
    local key = tostring(round) .. ':' .. tostring(question)
    if B.answers[key] then return B.answers[key] end
    if B.round ~= round then B.StartRound(round) end
    local correctChoice = tonumber(GetCorrectAnswer(question))
    local okDifficulty, difficultyIndex = pcall(GetQuestionDifficulty, question)
    local lookup, base, modifier = tuning()
    local difficulty = okDifficulty and lookup[difficultyIndex] or 3
    local list, standing, eliminated = {}, 0, 0
    for _, bot in ipairs(B.bots) do
        if bot.alive then
            local chance = math.max(0.03, math.min(0.98, base - modifier * (difficulty - bot.skill)))
            local correct = random() < chance
            list[#list + 1] = { Value = correct and correctChoice or wrongChoice(correctChoice),
                                Metadata = { xuid = bot.id, correct = correct } }
            if correct then standing = standing + 1 else bot.alive = false; eliminated = eliminated + 1 end
        end
    end
    B.answers[key] = list
    log(string.format('round %s question %s (difficulty %s): %d bots right, %d eliminated', tostring(round),
        tostring(question), tostring(difficulty), standing, eliminated))
    return list
end

-- Trace: the game writes its step-by-step trace through LuaLog (lualog.lua),
-- which drops most role-selection lines before print and only reaches the Xbox
-- debug output anyway, so a stalled Live show left nothing in the log. Forward,
-- before that filter: game state changes always, and in Live shows the role
-- selection, server state, program, round, timing, join-in-progress, Primetime
-- and live-host traces, plus a few key prints, as 'LIVE:' lines. Re-hooked if
-- the game redefines LuaLog after this file loads.
B.TRACE_NAMES = { STATES = 'always', ROLE_DETERMINATION = 'live', SERVERSTATES = 'live', PROGRAM = 'live',
                  ROUND = 'live', TIMING = 'live', JOIN_IN_PROGRESS = 'live', PRIMETIME = 'live',
                  LIVE_HOST = 'live', HELPS = 'live', QUESTION = 'live', PROGRAM_TIME = 'live' }
B.PRINT_PATTERNS = { 'oneSelectionResult', 'RoleDetermination', 'Not enough eligible', 'The one is local',
                     'REALAVATARS', 'NO_ONE_ELIGIBLE', 'NOT_ENOUGH_ELIGIBLE', 'Get State at', 'Set State at' }
B.traceLines = 0
local function liveShow() return type(isPrimetimeGame) == 'function' and CurrentProgram ~= nil and isPrimetimeGame() end
local function forward(kind, text)
    if B.traceLines >= 3000 or type(NativeDebugLog) ~= 'function' then return end
    B.traceLines = B.traceLines + 1
    pcall(NativeDebugLog, 'LIVE: ' .. kind .. ': ' .. string.sub(tostring(text), 1, 300))
end
function B.InstallTrace()
    if type(LuaLog) == 'function' and LuaLog ~= B.tracedLuaLog then
        local engineLuaLog = LuaLog
        B.tracedLuaLog = function(logType, text, ...)
            local name = type(logType) == 'table' and logType.name or tostring(logType)
            local rule = B.TRACE_NAMES[name]
            if rule == 'always' or (rule == 'live' and liveShow()) then forward(name, text) end
            return engineLuaLog(logType, text, ...)
        end
        LuaLog = B.tracedLuaLog
    end
    if not B.printTraced and type(print) == 'function' then
        B.printTraced = true
        local enginePrint = print
        print = function(...)
            if liveShow() then
                local parts = {}
                for i = 1, select('#', ...) do parts[#parts + 1] = tostring((select(i, ...))) end
                local text = table.concat(parts, ' ')
                for _, pattern in ipairs(B.PRINT_PATTERNS) do
                    if string.find(text, pattern, 1, true) then forward('print', text); break end
                end
            end
            return enginePrint(...)
        end
    end
end
-- The engine's ServerStateManager reports through XLP_LogInfo/XLP_LogError,
-- which never reach the log; forward its lines during a Live show.
function B.InstallEngineTrace()
    for _, name in ipairs({ 'XLP_LogInfo', 'XLP_LogError' }) do
        local engineLog = rawget(_G, name)
        if type(engineLog) == 'function' and engineLog ~= B[name] then
            B[name] = function(text, ...)
                if liveShow() or name == 'XLP_LogError' then forward(name == 'XLP_LogError' and 'ENGINE ERROR' or 'ENGINE', tostring(text)) end
                return engineLog(text, ...)
            end
            rawset(_G, name, B[name])
        end
    end
end
B.InstallTrace()
B.InstallEngineTrace()

-- The local service answers a GET the moment it is asked; the real service
-- answered at the message's execute time, after the Sets scheduled before it.
-- Without that wait the One read the Mob list (CHOSEN_MOB) about 1 s before
-- posting it, took that as "the One disconnected", never got the One's role,
-- never posted Introductions, and the show went back to choosing the One
-- (2026-10-11 01:37 log). Live only; Extended Play keeps its timing.
B.MAX_GET_WAIT = 30
function B.EnsureOfflineMobSize()
    -- The original development option for a Mob under 100 players is only
    -- honoured in non-retail builds, so the retail build still asserts a Mob
    -- of 100 humans when roles are set. Bots answer for the empty seats.
    if CONTENT_ALLOW_ZERO_MOB and DEFAULT_CUSTOM_1vsN_SIZE == false then
        DEFAULT_CUSTOM_1vsN_SIZE = true
        log('offline Live: the Mob may have fewer than 100 players; bots answer for the empty seats')
    end
end
function B.InstallServiceTiming()
    if B.timingInstalled or type(BeginGetServerMessage) ~= 'function' then return end
    B.timingInstalled = true
    local engineBegin = BeginGetServerMessage
    BeginGetServerMessage = function(messageTable, programID, round, question, xuid, difficulty, serverExecuteTime, ...)
        if CONTENT_MOCK_SERVER and liveShow() then B.EnsureOfflineMobSize() end
        -- In a room the room waits for the execute time itself (NativeRoom.lua).
        if CONTENT_MOCK_SERVER and liveShow() and not (NativeRoom and NativeRoom.active) then
            if type(serverExecuteTime) == 'number' and type(GetAbsoluteTime) == 'function' and type(Wait) == 'function' then
                local delay = serverExecuteTime - GetAbsoluteTime()
                if delay > 0 and delay <= B.MAX_GET_WAIT then
                    B.getWaits = (B.getWaits or 0) + 1
                    if B.getWaits <= 40 then
                        local key = type(messageTable) == 'table' and tostring(messageTable.MessageKey) or '?'
                        forward('ENGINE', string.format('GET waits %.2f s for execute time T%.2f: %s', delay, serverExecuteTime, key))
                    end
                    Wait(delay)
                end
            end
        end
        return engineBegin(messageTable, programID, round, question, xuid, difficulty, serverExecuteTime, ...)
    end
end
B.InstallServiceTiming()

-- Intro bumper: the original TitleIntroAudio.wmv is not in the game files, so
-- the bumper showed a black screen. The PC build plays
-- assets\media\movies\TitleIntroAudio.mp4 over the game window instead
-- (native side: NativeDebugLog "VIDEO:PLAY"/"VIDEO:STOP"). Live and Extended
-- Play; the small in-set variant is left alone.
function B.InstallVideo()
    if B.videoInstalled or type(PlayIntroBumper) ~= 'function' or type(StopAndHideBumper) ~= 'function' then return end
    B.videoInstalled = true
    local enginePlay, engineStop = PlayIntroBumper, StopAndHideBumper
    PlayIntroBumper = function(bStayInSet, ...)
        local result = enginePlay(bStayInSet, ...)
        local skipped = (type(isPrimetimeGame) == 'function' and isPrimetimeGame() and DEFAULT_NO_INTRO_VIDEO) or DEFAULT_IS_HOST_TOOL
        if not bStayInSet and not skipped and type(NativeDebugLog) == 'function' then NativeDebugLog('VIDEO:PLAY TitleIntroAudio') end
        return result
    end
    StopAndHideBumper = function(...)
        if type(NativeDebugLog) == 'function' then NativeDebugLog('VIDEO:STOP') end
        return engineStop(...)
    end
end
B.InstallVideo()

-- The show reads the Mob's answers through the local game service.
if LuaServer and type(LuaServer.MockGetMessage) == 'function' and not B.installed then
    B.installed = true
    local engineGetMessage = LuaServer.MockGetMessage
    LuaServer.MockGetMessage = function(key, ...)
        local value = engineGetMessage(key, ...)
        if LuaLog ~= B.tracedLuaLog then B.InstallTrace() end
        if rawget(_G, 'XLP_LogInfo') ~= B.XLP_LogInfo then B.InstallEngineTrace() end
        if not B.timingInstalled then B.InstallServiceTiming() end
        if not B.videoInstalled then B.InstallVideo() end
        if type(key) ~= 'string' then return value end
        local round, question = string.match(key, '%.Round%.(%d+)%.Question%.(%d+)%.MobChoices$')
        if not round or not B.IsActive() then return value end
        local ok, list = pcall(B.AnswersFor, tonumber(round), tonumber(question))
        if not ok then log('error: ' .. tostring(list)); return value end
        if #list == 0 then return value end
        value = type(value) == 'table' and value or {}
        value.Values = type(value.Values) == 'table' and value.Values or {}
        for _, entry in ipairs(list) do
            if #value.Values >= B.MOB_SIZE then break end
            value.Values[#value.Values + 1] = { Value = entry.Value,
                Metadata = { xuid = entry.Metadata.xuid, correct = entry.Metadata.correct } }
        end
        return value
    end
end
