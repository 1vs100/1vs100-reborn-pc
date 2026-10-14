-- Live (Primetime) in a room: two-client simulation of NativeRoom.lua.
--   texlua native-liveroom-test.lua <NativeRoom.lua> <luamockserver.lua (LF or CRLF)>
-- Two isolated Lua environments, a simulated native transport and a virtual
-- clock. Unit evidence only; it does not run the game.
ROOM_FILE = ROOM_FILE or arg[1]
MOCK_FILE = MOCK_FILE or arg[2]
-- Two isolated Lua environments + simulated native transport + virtual clock.
local T = 0               -- virtual seconds
local queues = { host = {}, client = {} }
local status = { host = 'host:0', client = 'connecting' }
local linkUp = false
local tasks = {}
local function mkenv(name)
  local env = setmetatable({}, { __index = _G })
  env._G = env
  env.Lua_HVStore = {}
  env.length = function(t) return #t end
  env.deepcopy = function(v) if type(v) ~= 'table' then return v end local o = {} for k, x in pairs(v) do o[k] = env.deepcopy(x) end return o end
  env.printf = function() end
  env.DependsOnLuaFile = function() end
  env.print = function() end
  env.CONTENT_MOCK_SERVER = true; env.CONTENT_NO_XLSP_ERROR = true
  env.GetAbsoluteTime = function() return T end
  env.Wait = function(s) coroutine.yield(s) end
  env.CreateGlobalTask = function(fn, ...) local args = table.pack(...) table.insert(tasks, { co = coroutine.create(function() fn(table.unpack(args)) end), wake = T, env = name }) end
  env.UTCClock = function() return { Now = function() return T end } end
  env.UTCTime = function(s) return 0 end
  env.Now = function() return T end
  env.CurrentProgram = { PID = function() return 'offline-development-001' end }
  env.logs = {}
  env.NativeDebugLog = function(s) table.insert(env.logs, s) end
  local other = name == 'host' and 'client' or 'host'
  env.NativeRoomTransport = function(op, value)
    if op == 'mode' then return name == 'host' and 'local-host' or 'local-join' end
    if op == 'status' then return status[name] end
    if op == 'self' then return 'local' end
    if op == 'utc' then return '2026-10-10T18:00:00.000Z' end
    if op == 'utcms' then return tostring(math.floor(T * 1000 + 0.5)) end
    if op == 'sendto' then
      if not linkUp then return 'error:not-connected' end
      local peer, payload = value:match('^(%d+)|(.*)$')
      table.insert(queues[other], (name == 'host' and '0' or '1') .. '|m|' .. payload)
      return 'queued'
    end
    if op == 'recv' then return table.remove(queues[name], 1) or '' end
    return 'error:operation'
  end
  env.ForAllLocalPlayers = function(fn) if env.localPlayer then fn(env.localPlayer) end end
  return env
end
local function load_into(env, file)
  local f = assert(io.open(file)); local src = f:read('a'); f:close()
  local chunk = assert(load(src, '@' .. file, 't', env)); chunk()
end
local H, C = mkenv('host'), mkenv('client')
H.localPlayer = { playerID = 'B13EBABEBABEBABE', playerName = 'HostPlayer' }
C.localPlayer = { playerID = 'B13EBABEBABEBAB2', playerName = 'Player2' }
-- Show length: host 5 questions, client launched with 3 (bonus table trimmed by the launch script).
local function bonusFull() local b = {} for q = 1, 37 do b[q] = (q % 5 == 0) and 2 or nil end b[37] = 3 return b end
H.EPTimingsTable = { maxNumberOfQuestions = 5 }; H.GameData = { EPBonusQuestions = bonusFull() }
C.EPTimingsTable = { maxNumberOfQuestions = 3 }; C.GameData = { EPBonusQuestions = { [3] = 3 } }; C.NATIVE_EP_BONUS_FULL = bonusFull()
for _, env in ipairs({ H, C }) do load_into(env, MOCK_FILE); load_into(env, ROOM_FILE) end
-- messages.lua hook point
for _, env in ipairs({ H, C }) do
  env.BeginGetServerMessage = function(mt, a, b, c, d, e, serverExecuteTime) return env.LuaServer.MockGetMessage(mt.key) end
  env.NativeRoom.InstallMessageHooks(); env.NativeRoom.StartPump()
end
-- The host's background task is removed: the room must still be serviced by
-- the original scripts' constant GetAbsoluteTime calls (frame simulation).
for i = #tasks, 1, -1 do if tasks[i].env == 'host' then table.remove(tasks, i) end end
local function run(seconds)
  local stop = T + seconds
  while T < stop do
    T = T + 0.02
    for _ = 1, 20 do H.GetAbsoluteTime() end
    for i = #tasks, 1, -1 do
      local task = tasks[i]
      if coroutine.status(task.co) == 'dead' then table.remove(tasks, i)
      elseif T >= task.wake then
        local ok, s = coroutine.resume(task.co)
        if not ok then error(task.env .. ' task: ' .. tostring(s)) end
        task.wake = T + (s or 0)
      end
    end
  end
end
local function connect()
  linkUp = true; status.client = 'connected'; status.host = 'host:1'
  table.insert(queues.host, '1|j|ip:127.0.0.1')
  table.insert(queues.client, '0|j|ip:127.0.0.1')
end
local P = 'v1.Session.MGS.1vs100.offline-development-001.EXTENDED_PLAY.Question.1.'
local function post(env, action, key, value, xuid, meta)
  env.LuaServer.MockPostMessage({ Actions = { { Name = key, Action = action, Value = value, Xuid = xuid, Metadata = meta } } })
end
local results = {}
local function check(name, cond) results[#results + 1] = (cond and 'PASS ' or 'FAIL ') .. name; if not cond then FAILED = true end end


connect()
local hostSession = { StartTime = '2026-10-10T18:01:00.000Z', EndTime = 'x', GameType = 1 }
H.NativeRoom.ResolveStartupSession(hostSession)
C.CreateGlobalTask(function() C.NativeRoom.ResolveStartupSession({ StartTime = 'client-own' }) end)
run(1)
local HX, CX = H.localPlayer.playerID, C.localPlayer.playerID
local K = 'v1.Session.MGS.1vs100.offline-development-001.PRIMETIME.30.'
-- 1. choosing the One
post(H, 'Top1', K .. 'TheOne', 0.41, HX, HX)
post(C, 'Top1', K .. 'TheOne', 0.77, CX, CX)
post(C, 'Top1', K .. 'TheOne', 0.99, HX, HX)          -- claims the host's player
run(0.5)
local ones
C.CreateGlobalTask(function() ones = C.LuaServer.MockGetMessage(K .. 'TheOne') end)
run(1)
check('client reads the shared One list; the client wins with the higher value', ones and ones.Values and #ones.Values == 1 and ones.Values[1].Metadata == CX)
-- 2. the Mob list: only from the One's PC
post(H, 'Top101', K .. 'MobSelection', 0.5, HX, 'HEX-H')
run(0.2)
post(C, 'Set', K .. 'TheMob', { TheOne = { Xuid = HX } })   -- not the client's player
run(0.2)
check('a Mob list naming someone else as the One is rejected', H.Lua_HVStore[K .. 'TheMob'] == nil)
post(C, 'Set', K .. 'TheMob', { 'HEX-H', TheOne = { Xuid = CX, GamerTag = 'Player2' } })
run(0.2)
local mob
H.CreateGlobalTask(function() mob = H.LuaServer.MockGetMessage(K .. 'TheMob') end)
run(1)
check('the One\'s Mob list reaches the host', mob and mob.TheOne and mob.TheOne.Xuid == CX and mob[1] == 'HEX-H')
-- 3. the Mob's answers
local Q = 'v1.Session.MGS.1vs100.offline-development-001.PRIMETIME.Round.1.Question.1.'
post(H, 'Top100', Q .. 'MobChoices', 2, HX, { xuid = HX, correct = true })
post(C, 'Top100', Q .. 'MobChoices', 7, CX, { xuid = CX, correct = true })     -- not an answer
post(C, 'Top100', Q .. 'MobChoices', 3, CX, { xuid = CX, correct = false })
post(C, 'Top100', Q .. 'MobChoices', 2, CX, { xuid = CX, correct = true })     -- second answer
run(0.5)
local answers = H.Lua_HVStore[Q .. 'MobChoices']
local n, clientAnswer = 0, nil
for _, row in ipairs(answers and answers.Values or {}) do n = n + 1 if row.Metadata.xuid == CX then clientAnswer = row.Value end end
check('host keeps one valid answer per Mob member (got ' .. n .. ', client ' .. tostring(clientAnswer) .. ')', n == 2 and clientAnswer == 3)
-- 4. the show state (GameState)
for _, env in ipairs({ H, C }) do
  env.ConvertToUtcTime = function(d) return d + 1000 end
  env.ConvertToRelativeTime = function(u) return u - 1000 end
  env.SleepUntil = function(utc) while T + 1000 < utc do coroutine.yield(0.02) end end
  env.ServerState = { new = function(self, name, s, e, g, data) return { name = name, startTimeUtc = s, endTimeUtc = e, sendGetUtc = g, data = data } end }
  env.gsStore = env.MockGameService:new()
  env.NexiaGameStateStore = function() return env.gsStore end
end
local GS = 'v1.Session.MGS.1vs100.offline-development-001.GameState'
local intro = { name = 'LiveHostIntro', startTimeUtc = 1081, endTimeUtc = 1103, data = { OnesXuid = CX, RoundNumber = 1, QuestionNumber = 0 } }
C.NativeRoom.GameStateSet(GS, { name = 'QA', startTimeUtc = 1, endTimeUtc = 2, data = { OnesXuid = HX } }, 0, C.gsStore)  -- pretends to be the host's One
run(0.3)
check('a show state from a PC that is not the One is rejected', H.gsStore.valuesSet[GS] == nil)
local result = C.NativeRoom.GameStateSet(GS, intro, 0, C.gsStore)
run(0.3)
local held = H.gsStore.valuesSet[GS]
check('the One on a client sets the host\'s show state', result and result.ErrorCode == 0 and held and held.name == 'LiveHostIntro' and held.startTimeUtc == 1081 and held.endTimeUtc == 1103)
C.gsStore.valuesSet[GS] = nil
local read, asked, readAt
C.CreateGlobalTask(function() asked = T; read = C.NativeRoom.GameStateGet(GS, T + 1000 + 1.5, C.gsStore); readAt = T end)
run(4)
check('client reads the host\'s show state at its execute time (waited ' .. string.format('%.2f', (readAt or 0) - (asked or 0)) .. ' s)',
  read and read.ErrorCode == 0 and read.Value.name == 'LiveHostIntro' and read.Value.startTimeUtc == 1081 and read.Value.data.OnesXuid == CX and readAt - asked >= 1.5)
H.gsStore.valuesSet[GS] = nil
local missing
C.CreateGlobalTask(function() missing = C.NativeRoom.GameStateGet(GS, T + 1000, C.gsStore) end)
run(2)
check('no show state on the host reads as not found (the game then starts with ElectHost)', missing and missing.ErrorCode == 404)
for _, r in ipairs(results) do print(r) end
print('host log:'); for _, l in ipairs(H.logs) do print('  ' .. l) end
print('client log:'); for _, l in ipairs(C.logs) do print('  ' .. l) end
if FAILED then os.exit(1) end
