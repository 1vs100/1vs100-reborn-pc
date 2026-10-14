-- Two-client simulation of NativeRoom.lua (Lua 5.3+, e.g. texlua or lua5.3):
--   lua native-room-test.lua <NativeRoom.lua> <luamockserver.lua (LF or CRLF)>
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

-- 1. startup session handshake
connect()
local hostSession = { StartTime = '2026-10-10T18:01:00.000Z', EndTime = 'x', GameType = 1 }
H.NativeRoom.ResolveStartupSession(hostSession)
local clientSession
C.CreateGlobalTask(function() clientSession = C.NativeRoom.ResolveStartupSession({ StartTime = 'client-own' }) end)
run(1)
check('client adopts host session', clientSession and clientSession.StartTime == hostSession.StartTime)
check('client plays the host\'s show mode (Live)', C.OFFLINE_SESSION_GAME_TYPE == 1 and clientSession.GameType == 1)
check('client plays the host\'s show length (got ' .. tostring(C.EPTimingsTable.maxNumberOfQuestions) .. ')', C.EPTimingsTable.maxNumberOfQuestions == 5)
check('client final-question bonus moves to the host\'s last question', C.GameData.EPBonusQuestions[5] == 3 and C.GameData.EPBonusQuestions[3] == nil)
-- 2. shared SUM and TOP
post(H, 'Sum', P .. 'TotalCorrect', 1, 'B13EBABEBABEBABE')
post(C, 'Sum', P .. 'TotalCorrect', 1, 'B13EBABEBABEBAB2')
post(C, 'Sum', P .. 'TotalCorrect', 1, 'B13EBABEBABEBAB2')  -- duplicate
post(C, 'Sum', P .. 'TotalCorrect', 1, 'B13EBABEBABEBABE')  -- host's identity
post(H, 'Top100', P .. 'RoundScores', 812, nil, 'HostPlayer')
post(C, 'Top100', P .. 'RoundScores', 640, nil, 'Player2')
post(C, 'Top100', P .. 'RoundScores', 99999, nil, 'Cheater')
run(0.5)
local hostCount = H.Lua_HVStore[P .. 'TotalCorrect'] and H.Lua_HVStore[P .. 'TotalCorrect'].Value
check('host SUM counts both players once (got ' .. tostring(hostCount) .. ')', hostCount == 2)
local got, topGot
C.CreateGlobalTask(function()
  C.NativeRoom.pendingExecuteTime = T + 1
  local asked = T
  got = C.LuaServer.MockGetMessage(P .. 'TotalCorrect')
  waited = T - asked
  topGot = C.LuaServer.MockGetMessage(P .. 'RoundScores')
end)
local before = T
run(3)
check('client GET reads host SUM (got ' .. tostring(got and got.Value) .. ')', got and got.Value == 2)
check('client GET waited for execute time + margin (waited ' .. string.format('%.2f', waited or -1) .. ' s)', waited and waited >= 1.3 and waited < 2.5)
check('client TOP has both players, no cheater', topGot and #topGot.Values == 2 and topGot.Values[1].Metadata == 'HostPlayer' and topGot.Values[2].Metadata == 'Player2')
-- 3. client cannot write non-shared keys on host
post(C, 'Set', 'v1.Setup.MGS.1vs100.RoomTestKey', 1)
run(0.2)
check('client Set stays local', H.Lua_HVStore['v1.Setup.MGS.1vs100.RoomTestKey'] == nil)
-- 4. next session
local nextSession
C.CreateGlobalTask(function() nextSession = C.NativeRoom.AwaitNextSession(hostSession.StartTime) end)
-- The host has nobody playing on it: the client's end-of-show request alone
-- must advance the shared show.
H.NativeSecondsToIso = function(s) return 'iso:' .. s end
run(3)
check('idle host advances the show on the client\'s request (got ' .. tostring(nextSession and nextSession.StartTime) .. ')',
  nextSession and nextSession.StartTime and nextSession.StartTime ~= hostSession.StartTime and H.Lua_HVStore[P .. 'TotalCorrect'] == nil)
-- The host's own intermission afterwards gets the same session, not a second one.
local again = H.NativeRoom.HostAdvanceSession(hostSession.StartTime)
check('host intermission reuses the advanced session', again and again.StartTime == nextSession.StartTime)
-- 5. host lost: client falls back to its local mirror
linkUp = false; status.client = 'disconnected:test'; table.insert(queues.client, '0|l|test')
local fallback
C.CreateGlobalTask(function() fallback = C.LuaServer.MockGetMessage(P .. 'TotalCorrect') end)
run(1)
check('client falls back to local data when host lost (got ' .. tostring(fallback and fallback.Value) .. ')', fallback and fallback.Value == 3)
for _, r in ipairs(results) do print(r) end
print('host log:'); for _, l in ipairs(H.logs) do print('  ' .. l) end
print('client log:'); for _, l in ipairs(C.logs) do print('  ' .. l) end
if FAILED then os.exit(1) end
