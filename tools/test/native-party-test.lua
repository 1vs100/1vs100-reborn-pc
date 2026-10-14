-- Two-copy simulation of the NativeRoom party bridge (Lua 5.3+, e.g. texlua):
--   texlua native-party-test.lua <NativeRoom.lua> <luamockserver.lua>
-- Stubs the game's player/QNet entry points and checks what the bridge feeds
-- them. Unit evidence only; it does not run the game.
ROOM_FILE = ROOM_FILE or arg[1]
MOCK_FILE = MOCK_FILE or arg[2]
local T = 0
local queues = { host = {}, client = {} }
local status = { host = 'host:0', client = 'connecting' }
local linkUp = true
local tasks = {}
local function mkenv(name)
  local env = setmetatable({}, { __index = _G })
  env._G = env
  env.Lua_HVStore = {}
  env.length = function(t) local n = 0 for _ in pairs(t) do n = n + 1 end return n end
  env.printf = function() end
  env.DependsOnLuaFile = function() end
  env.print = function() end
  env.CONTENT_MOCK_SERVER = true; env.CONTENT_NO_XLSP_ERROR = true
  env.GetAbsoluteTime = function() return T end
  env.Wait = function(s) coroutine.yield(s) end
  env.CreateGlobalTask = function(fn) table.insert(tasks, { co = coroutine.create(fn), wake = T, env = name }) end
  env.UTCClock = function() return { Now = function() return T end } end
  env.UTCTime = function() return 0 end
  env.Now = function() return T end
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
  -- Game stubs
  env.destinations = {}
  env.players = {}           -- xuid -> player table (local and remote)
  env.joins, env.leaves, env.received, env.engineSends, env.avatars = {}, {}, {}, {}, {}
  env.Players = {
    GetLocalPlayerList = function() local l = {} for x, p in pairs(env.players) do if p.bLocal then l[#l + 1] = x end end table.sort(l) return l end,
    GetPlayerByXUID = function(x) return env.players[x] or false end,
  }
  env.ForAllLocalPlayers = function(fn) for _, x in ipairs(env.Players.GetLocalPlayerList()) do fn(env.players[x]) end end
  env.STATE_Lobby = { bAllPackagesInitialized = true }
  local realQNet = { GetGamerTag = function(self, x) return env.players[x] and env.players[x].bLocal and env.players[x].playerName or nil end,
                     GetAllXUIDs = function(self) return {} end, GetLocalXUIDs = function(self) return nil end,
                     IsInSession = function(self) return false end }
  env.SessionData = { qNet = realQNet }
  env.QNetSendToAll = function(data) table.insert(env.engineSends, data.Type) end
  env.QNetSendTo = function(list, data) table.insert(env.engineSends, data.Type) end
  env.CreateAvatar = function(x) table.insert(env.avatars, x); return {} end
  env.LuaOnNetworkPlayerJoin = function(message)
    local tag = env.SessionData.qNet:GetGamerTag(message.xuid)
    table.insert(env.joins, message.xuid .. '=' .. tostring(tag))
    env.CreateAvatar(message.xuid)
    env.players[message.xuid] = { playerID = message.xuid, playerName = tag, bLocal = false }
  end
  env.LuaOnNetworkPlayerLeave = function(message) table.insert(env.leaves, message.xuid); env.players[message.xuid] = nil end
  -- Original amping send: only inside a QNet network session (never on PC).
  env.EventType = { Avatar = { Amping = 'AvatarAmpingNetworkEvent' } }
  env.CreateQNetData = function(sender, eventType, value) return { playerID = tostring(sender), Type = eventType, Value = value } end
  env.AmpingAnimationList = { 'Dance1' }
  env.SendAmpingUpdate = function(p, n, id) if env.SessionData.qNet:IsInSession() then env.QNetSendToAll(env.CreateQNetData(p.playerID, env.EventType.Avatar.Amping, { messageID = id })) end end
  env.LuaOnNetworkDataReceived = function(message) table.insert(env.received, message.fromxuid .. ':' .. message.data.Type); table.insert(env.destinations, message.toxuid or 'nil') end
  return env
end
local function load_into(env, file)
  local f = assert(io.open(file)); local src = f:read('a'); f:close()
  assert(load(src, '@' .. file, 't', env))()
end
local H, C = mkenv('host'), mkenv('client')
H.players['2533274790395904'] = { playerID = '2533274790395904', playerName = 'HostPlayer', bLocal = true }
C.players['2533274790395905'] = { playerID = '2533274790395905', playerName = 'GuestPlayer', bLocal = true }
for _, env in ipairs({ H, C }) do load_into(env, MOCK_FILE); load_into(env, ROOM_FILE); env.NativeRoom.StartPump() end
local function run(seconds)
  local stop = T + seconds
  while T < stop do
    T = T + 0.02
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
local pass, fail = 0, 0
local function check(name, ok, detail)
  if ok then pass = pass + 1; print('PASS ' .. name .. (detail and (' (' .. detail .. ')') or ''))
  else fail = fail + 1; print('FAIL ' .. name .. (detail and (' (' .. detail .. ')') or '')) end
end
local hostJoin = H.LuaOnNetworkPlayerJoin
local transientFailures = 0
H.LuaOnNetworkPlayerJoin = function(message)
  if transientFailures == 0 then transientFailures = 1; error('injected transient join failure') end
  return hostJoin(message)
end
-- connect: client gets 'j' then hello/welcome
status.client = 'connected'; status.host = 'host:1'
table.insert(queues.client, '0|j|steam:host'); table.insert(queues.host, '1|j|steam:client')
run(3)
check('failed join callback is retried', transientFailures == 1 and #H.joins == 1)
check('host creates the client player with its gamertag', H.joins[1] == '2533274790395905=GuestPlayer', H.joins[1])
check('client creates the host player with its gamertag', C.joins[1] == '2533274790395904=HostPlayer', C.joins[1])
check('remote players get a stand-in avatar', H.avatars[1] == 'Random' and C.avatars[1] == 'Random', tostring(H.avatars[1]))
local inSession = false
for _, x in pairs(C.SessionData.qNet:GetAllXUIDs()) do if x == '2533274790395904' then inSession = true end end
check('remote player counts as in the session', inSession)
check('no duplicate joins', #H.joins == 1 and #C.joins == 1, #H.joins .. '/' .. #C.joins)
-- client answers
C.QNetSendToAll({ playerID = '2533274790395905', Type = 'PlayerAnswerData', Value = { Correct = true, TimeBonusRatio = 0.8 } })
run(0.5)
check('client answer reaches the host game', H.received[1] == '2533274790395905:PlayerAnswerData', H.received[1])
check('client does not receive its own answer back', #C.received == 0, #C.received)
check('engine send still runs', C.engineSends[1] == 'PlayerAnswerData')
-- host score update
H.QNetSendToAll({ playerID = '2533274790395904', Type = 'PlayerUpdate', Value = { scoreValue = 816 } })
run(0.5)
check('host score update reaches the client game', C.received[1] == '2533274790395904:PlayerUpdate', C.received[1])
-- spoof and blocked types
C.QNetSendToAll({ playerID = '2533274790395904', Type = 'PlayerUpdate', Value = { scoreValue = 99999 } })
C.QNetSendTo({ '2533274790395904' }, { playerID = '2533274790395905', Type = 'GridInfo', Value = {} })
run(0.5)
check('host drops messages for players the client does not own, and grid/invite events', #H.received == 1, #H.received)
-- targeted creation data
H.QNetSendTo({ '2533274790395905' }, { playerID = '2533274790395904', Type = 'PlayerCreationData', Value = { lifetimeScore = 5 } })
run(0.5)
check('targeted creation data reaches its player', C.received[2] == '2533274790395904:PlayerCreationData', C.received[2])
-- Broadcast PlayerInGame must carry a local destination (original handler
-- returns early without it); targeted lists may name a nonlocal player first.
H.QNetSendToAll({ playerID = '2533274790395904', Type = 'PlayerInGame', Value = {} })
run(0.5)
check('broadcast in-game event has local destination', C.destinations[#C.destinations] == '2533274790395905')
H.QNetSendTo({ '2533274790395904', '2533274790395905' }, { playerID = '2533274790395904', Type = 'PlayerCreationData', Value = {} })
run(0.5)
check('targeted delivery selects recipient on this client', C.destinations[#C.destinations] == '2533274790395905')
-- lobby dancing reaches the other copy although QNet reports no session
local before = #H.received
C.SendAmpingUpdate({ playerID = '2533274790395905', ampAnimNumber = 1, ampSpeed = 1.5 }, 7, 42)
run(0.5)
check('client dance (amping) update reaches the host game', H.received[before + 1] == '2533274790395905:AvatarAmpingNetworkEvent', H.received[before + 1])
-- client leaves
table.insert(queues.host, '1|l|test'); linkUp = false
table.insert(queues.client, '0|l|test')
run(1)
check('host removes the client player when it leaves', H.leaves[1] == '2533274790395905', H.leaves[1])
check('client removes the host player when the host is lost', C.leaves[1] == '2533274790395904', C.leaves[1])
print(string.format('%d passed, %d failed', pass, fail))
for _, env in ipairs({ H, C }) do for _, l in ipairs(env.logs) do if l:find('error') or l:find('failed') then print('  log: ' .. l) end end end
os.exit(fail == 0 and 0 or 1)
