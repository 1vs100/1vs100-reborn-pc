-- NativeRoom.lua --
-- Opt-in shared Extended Play room for the native PC build.
--
-- Dormant unless the native NativeRoomTransport API reports a room mode
-- (ONEVS100_ROOM_MODE). One copy of the game is the host; every other copy is
-- a client. The host plays the part of the old 1 vs 100 game service for the
-- shared show: it owns the session schedule (start times and rollover), the
-- session stats store (SUM/TOP keys), the offline bots, and the clock that
-- clients align to. Every client still runs the original show schedule
-- locally, exactly as the Xbox did against the real service; only the service
-- calls go to the host.
--
--   * Service GETs of shared keys run at their original serverExecuteTime
--     (plus a small margin), on the host's store, and are answered with the
--     host's full view (including its bots).
--   * Client SUM/TOP posts go to the host, which validates them (allowed keys
--     only, one SUM per player per key, one TOP per player per key, the
--     posting peer owns the player ID, bounded score growth) before applying.
--   * Clients mirror their own posts locally, so a lost host degrades to a
--     solo show instead of a fatal service error.
--
-- Wire format: NativeRoom.Encode/Decode (bounded, never evaluates received
-- data). Everything here is plain data; no Lua is ever loaded from the network.

NativeRoom = NativeRoom or {}
local R = NativeRoom

R.VERSION = 2               -- 2: party roster and game messages
R.GET_MARGIN = 0.35        -- seconds after serverExecuteTime before reading
R.GET_TIMEOUT = 4          -- seconds a client waits for the host to answer
R.WELCOME_TIMEOUT = 45     -- seconds a client waits for the host at startup
R.STEAM_WELCOME_TIMEOUT = 300 -- Steam joins: finding the party and connecting can take a while
R.NEXT_SESSION_TIMEOUT = 20

local now = function() return GetAbsoluteTime and GetAbsoluteTime() or 0 end
local function log(text)
    if type(NativeDebugLog) == 'function' then pcall(NativeDebugLog, 'ROOM: ' .. tostring(text)) end
end
R.Log = log

----------------------------------------------------------------------
-- Codec (bounded; unsupported values such as userdata are dropped)
----------------------------------------------------------------------
function R.Encode(value)
    local active = {}
    local function encode(v, depth)
        assert(depth < 24, 'Room nesting limit')
        local kind = type(v)
        if kind == 'nil' then return 'z' end
        if kind == 'boolean' then return v and 'y' or 'x' end
        if kind == 'number' then
            if v ~= v or v == math.huge or v == -math.huge then return 'z' end
            local s = string.format('%.17g', v); return 'n' .. #s .. ':' .. s
        end
        if kind == 'string' then return 's' .. #v .. ':' .. v end
        if kind ~= 'table' or active[v] then return 'z' end
        active[v] = true
        local parts, count = {}, 0
        for key, item in pairs(v) do
            local keyKind, itemKind = type(key), type(item)
            if (keyKind == 'string' or keyKind == 'number') and
               (itemKind == 'string' or itemKind == 'number' or itemKind == 'boolean' or itemKind == 'table') then
                local encodedItem = encode(item, depth + 1)
                if encodedItem ~= 'z' then
                    count = count + 1; assert(count <= 4096, 'Room table limit')
                    parts[#parts + 1] = encode(key, depth + 1) .. encodedItem
                end
            end
        end
        active[v] = nil
        return 't' .. count .. ':' .. table.concat(parts)
    end
    local encoded = encode(value, 0)
    assert(#encoded <= 65536, 'Room packet limit')
    return encoded
end

function R.Decode(data)
    assert(type(data) == 'string' and #data <= 65536, 'Room packet limit')
    local cursor = 1
    local function count()
        local finish = string.find(data, ':', cursor, true)
        assert(finish and finish - cursor <= 6, 'Invalid room length')
        local digits = string.sub(data, cursor, finish - 1)
        assert(string.match(digits, '^%d+$'), 'Invalid room length')
        cursor = finish + 1; return tonumber(digits)
    end
    local function decode(depth)
        assert(depth < 24 and cursor <= #data, 'Invalid room nesting')
        local tag = string.sub(data, cursor, cursor); cursor = cursor + 1
        if tag == 'z' then return nil end
        if tag == 'y' then return true end
        if tag == 'x' then return false end
        if tag == 's' or tag == 'n' then
            local size = count(); assert(cursor + size - 1 <= #data, 'Truncated room data')
            local s = string.sub(data, cursor, cursor + size - 1); cursor = cursor + size
            if tag == 's' then return s end
            local n = tonumber(s); assert(n and n == n and n ~= math.huge and n ~= -math.huge, 'Invalid room number'); return n
        end
        assert(tag == 't', 'Invalid room tag')
        local size = count(); assert(size <= 4096, 'Room table limit')
        local t = {}
        for i = 1, size do
            local key = decode(depth + 1)
            assert(type(key) == 'string' or type(key) == 'number', 'Invalid room key')
            assert(t[key] == nil, 'Duplicate room key')
            local item = decode(depth + 1); assert(item ~= nil, 'Nil room table value'); t[key] = item
        end
        return t
    end
    local value = decode(0); assert(cursor == #data + 1, 'Trailing room data'); return value
end

local function copy(value)
    if type(value) ~= 'table' then return value end
    local out = {}
    for k, v in pairs(value) do out[k] = copy(v) end
    return out
end

----------------------------------------------------------------------
-- Transport helpers
----------------------------------------------------------------------
local function transport(op, value) return NativeRoomTransport(op, value or '') end
function R.Send(peer, message)
    local ok, encoded = pcall(R.Encode, message)
    if not ok then log('encode failed: ' .. tostring(encoded)); return false end
    local result = transport('sendto', tostring(peer) .. '|' .. encoded)
    if result ~= 'queued' then R.lastSendError = result; return false end
    return true
end
local function nativeMs() return tonumber(transport('utcms')) or 0 end

----------------------------------------------------------------------
-- Shared-key policy
----------------------------------------------------------------------
local SHARED_SUFFIXES = {
    '%.TotalCorrect$', '%.TotalPlayerCount$', '%.RoundScores$', '%.EPShowEndTopScores$',
    '%.StatBreaks%.NumCorrectAnswers%.Bucket$', '%.StatBreaks%.Streak%.Bucket$', '%.LobbyPlayerCount$',
    -- Live (Primetime): choosing the One and the Mob, the Mob's answers, helps.
    '%.TheOne$', '%.MobSelection$', '%.TheMob$', '%.RSTotalPlayerCount$',
    '%.MobChoices$', '%.Brain$', '%.Crowd%.Choice%d$',
}
-- The original service answered an empty SUM with a zero count and an empty
-- TOP with no rows; callers index the result, so never hand them nil.
local function emptyDefault(key, value)
    if value ~= nil or type(key) ~= 'string' then return value end
    if string.find(key, '%.RoundScores$') or string.find(key, '%.EPShowEndTopScores$') then return { Values = {} } end
    if string.find(key, '%.TotalCorrect$') or string.find(key, '%.TotalPlayerCount$') or
       string.find(key, '%.LobbyPlayerCount$') or string.find(key, '%.RSTotalPlayerCount$') or
       string.find(key, '%.Crowd%.Choice%d$') then return { Value = 0 } end
    return value
end
R.EmptyDefault = emptyDefault

local SESSION_PREFIX = 'v1.Session.MGS.1vs100.'
function R.IsSharedKey(key)
    if type(key) ~= 'string' or string.sub(key, 1, #SESSION_PREFIX) ~= SESSION_PREFIX then return false end
    if CurrentProgram and CurrentProgram.PID then
        local ok, pid = pcall(CurrentProgram.PID)
        if ok and type(pid) == 'string' and
           string.sub(key, #SESSION_PREFIX + 1, #SESSION_PREFIX + #pid + 1) ~= pid .. '.' then return false end
    end
    for _, pattern in ipairs(SHARED_SUFFIXES) do
        if string.find(key, pattern) then return true end
    end
    return false
end

----------------------------------------------------------------------
-- Clock (clients follow the host's clock; the host uses this PC's UTC)
----------------------------------------------------------------------
R.clockOffset = 0     -- seconds to add to this PC's UTC to get host UTC
R.bestRtt = nil
local function adoptOffset(sentMs, hostMs)
    local recvMs = nativeMs()
    local rtt = recvMs - sentMs
    if rtt < 0 or rtt > 5000 then return end
    if R.bestRtt and rtt > R.bestRtt * 1.5 + 20 then return end
    R.bestRtt = R.bestRtt and math.min(R.bestRtt, rtt) or rtt
    local offset = (hostMs + rtt / 2 - recvMs) / 1000
    if math.abs(offset - R.clockOffset) > 0.05 then
        log(string.format('clock offset %.3f s (rtt %d ms)', offset, rtt))
    end
    R.clockOffset = offset
end

----------------------------------------------------------------------
-- Host state
----------------------------------------------------------------------
R.peers = {}            -- host: peer -> { xuids = {xuid=true}, name, lastScore = {} }
R.sumSeen = {}          -- host: key -> { [xuid] = true }
R.topSeen = {}          -- host: key -> { [owner] = true }
R.currentSession = nil  -- host: session table clients should play
R.sessionGeneration = 0

-- XUIDs of this PC's own signed-in players (never claimable by a client).
local function refreshLocalPlayers()
    R.localXuids = R.localXuids or {}
    if type(ForAllLocalPlayers) ~= 'function' then return end
    pcall(ForAllLocalPlayers, function(player)
        if type(player) == 'table' and type(player.playerID) == 'string' then
            R.localXuids[player.playerID] = true
            R.localName = R.localName or player.playerName
        end
    end)
end
R.RefreshLocalPlayers = refreshLocalPlayers

local function ownsXuid(peerState, xuid)
    if type(xuid) ~= 'string' or #xuid == 0 or #xuid > 32 then return false end
    refreshLocalPlayers()
    for other, state in pairs(R.peers) do
        if state ~= peerState and state.xuids[xuid] then return false end
    end
    if R.localXuids and R.localXuids[xuid] then return false end
    peerState.xuids[xuid] = true
    return true
end

-- Validates and applies one client post on the host. Returns ok, reason.
function R.HostApplyPost(peer, struct)
    local peerState = R.peers[peer]
    if not peerState then return false, 'unknown peer' end
    local action = type(struct) == 'table' and type(struct.Actions) == 'table' and struct.Actions[1]
    if type(action) ~= 'table' then return false, 'malformed' end
    local name, kind, value = action.Name, action.Action, action.Value
    if not R.IsSharedKey(name) then return false, 'key not shared: ' .. tostring(name) end
    local live, liveOk, liveReason = R.HostApplyLivePost(peerState, action, struct)
    if live then return liveOk, liveReason end
    if kind == 'Sum' then
        if value ~= 1 then return false, 'sum value must be 1' end
        local xuid = action.Xuid
        if not ownsXuid(peerState, xuid) then return false, 'xuid not owned' end
        R.sumSeen[name] = R.sumSeen[name] or {}
        if R.sumSeen[name][xuid] then return false, 'duplicate sum' end
        R.sumSeen[name][xuid] = true
    elseif type(kind) == 'string' and string.sub(kind, 1, 3) == 'Top' then
        local size = tonumber(string.sub(kind, 4))
        if not size or size < 1 or size > 101 then return false, 'bad top size' end
        if type(value) ~= 'number' or value < 0 or value > 1000000 then return false, 'bad score' end
        local meta = action.Metadata
        local owner
        if type(meta) == 'table' then
            owner = meta.playerID
            if not ownsXuid(peerState, owner) then return false, 'player not owned' end
            if type(meta.gamerTag) ~= 'string' or #meta.gamerTag > 32 then return false, 'bad gamertag' end
        elseif type(meta) == 'string' and #meta <= 32 then
            owner = 'peer' .. peer .. ':' .. meta
        else
            return false, 'bad metadata'
        end
        -- A score can only grow, and by at most one question's worth at a time.
        local last = peerState.lastScore[owner] or 0
        if value + 0.001 < last then return false, 'score decreased' end
        if value - last > 6000 then return false, 'score jump too large' end
        R.topSeen[name] = R.topSeen[name] or {}
        if R.topSeen[name][owner] then return false, 'duplicate top' end
        R.topSeen[name][owner] = true
        peerState.lastScore[owner] = value
    else
        return false, 'action not allowed: ' .. tostring(kind)
    end
    R.localPost(struct)
    return true
end

-- Serve a client GET with the host's full view (outermost GET wrapper, which
-- includes the host-owned bots), without the execute-time wait.
function R.HostServeGet(key)
    if not R.IsSharedKey(key) then return nil end
    R.serving = true
    local ok, value = pcall(LuaServer.MockGetMessage, key)
    R.serving = false
    if not ok then log('host GET failed for ' .. tostring(key) .. ': ' .. tostring(value)); return emptyDefault(key, nil) end
    return emptyDefault(key, value)
end

-- Live (Primetime) posts. Each player may post once per key, only for a
-- player ID this peer owns; the Mob list (TheMob) only from the One's peer.
-- Returns handled, ok, reason (handled false: use the Extended Play rules).
local function liveKind(name)
    if string.find(name, '%.TheOne$') then return 'one' end
    if string.find(name, '%.MobSelection$') then return 'mobselect' end
    if string.find(name, '%.TheMob$') then return 'mob' end
    if string.find(name, '%.MobChoices$') then return 'choices' end
    if string.find(name, '%.Brain$') then return 'brain' end
    return nil
end
function R.HostApplyLivePost(peerState, action, struct)
    local name, kind, value, meta = action.Name, action.Action, action.Value, action.Metadata
    local live = liveKind(name)
    if not live then return false end
    local function once(owner)
        R.topSeen[name] = R.topSeen[name] or {}
        if R.topSeen[name][owner] then return false end
        R.topSeen[name][owner] = true
        return true
    end
    local isTop = type(kind) == 'string' and string.sub(kind, 1, 3) == 'Top' and tonumber(string.sub(kind, 4))
    if live == 'mob' then
        if kind ~= 'Set' or type(value) ~= 'table' or type(value.TheOne) ~= 'table' then return true, false, 'bad Mob list' end
        if not ownsXuid(peerState, value.TheOne.Xuid) then return true, false, 'Mob list not from the One' end
        if not once('mob') then return true, false, 'duplicate Mob list' end
    else
        if not isTop or isTop < 1 or isTop > 101 then return true, false, 'bad top size' end
        if type(value) ~= 'number' or value ~= value or value < 0 or value > 1000000 then return true, false, 'bad value' end
        local owner = action.Xuid
        if not ownsXuid(peerState, owner) then return true, false, 'player not owned' end
        if live == 'one' and meta ~= owner then return true, false, 'bad One entry' end
        if live == 'mobselect' and (type(meta) ~= 'string' or #meta > 32) then return true, false, 'bad Mob entry' end
        if live == 'choices' then
            if type(meta) ~= 'table' or meta.xuid ~= owner or type(meta.correct) ~= 'boolean' then return true, false, 'bad answer entry' end
            if value ~= 1 and value ~= 2 and value ~= 3 then return true, false, 'bad answer' end
        end
        if live == 'brain' and (type(meta) ~= 'table' or meta.xuid ~= owner) then return true, false, 'bad help entry' end
        if not once(owner) then return true, false, 'duplicate entry' end
    end
    R.localPost(struct)
    return true, true
end

----------------------------------------------------------------------
-- Live (Primetime) show state
----------------------------------------------------------------------
-- The show is driven by the original ServerStateManager reading and writing
-- one GameState key (luaengine\gameservice.lua keeps it in a local store).
-- In a room the host's store is the shared one: clients read it at the
-- original execute time, and the One (on any PC) writes the next state to it.
-- UTC times travel as seconds from the show start (episodeStartTime), which
-- every copy shares, and become UTC times again on arrival.
function R.IsGameStateKey(key)
    return type(key) == 'string' and string.match(key, '^v1%.Session%.MGS%.1vs100%.[^%.]+%.GameState$') ~= nil
end
local function toRelative(utc)
    if utc == nil or type(ConvertToRelativeTime) ~= 'function' then return nil end
    local ok, value = pcall(ConvertToRelativeTime, utc)
    return ok and type(value) == 'number' and value or nil
end
local function toUtc(seconds)
    if type(seconds) ~= 'number' or type(ConvertToUtcTime) ~= 'function' then return nil end
    local ok, value = pcall(ConvertToUtcTime, seconds)
    return ok and value or nil
end
function R.StateToWire(state)
    if type(state) ~= 'table' then return nil end
    return { name = state.name, start = toRelative(state.startTimeUtc), finish = toRelative(state.endTimeUtc),
             sendGet = toRelative(state.sendGetUtc), data = copy(state.data) }
end
function R.StateFromWire(wire)
    if type(wire) ~= 'table' or type(wire.name) ~= 'string' or type(wire.start) ~= 'number' or
       type(wire.finish) ~= 'number' then return nil end
    local startUtc, endUtc = toUtc(wire.start), toUtc(wire.finish)
    if not (startUtc and endUtc) then return nil end
    local sendGet = wire.sendGet and toUtc(wire.sendGet) or nil
    if type(ServerState) == 'table' and type(ServerState.new) == 'function' then
        return ServerState:new(wire.name, startUtc, endUtc, sendGet, wire.data)
    end
    return { name = wire.name, startTimeUtc = startUtc, endTimeUtc = endUtc, sendGetUtc = sendGet, data = wire.data }
end
local function roomPlaying() return R.active and not R.solo end

-- Called by the GameState service wrapper (luaengine\gameservice.lua).
function R.GameStateGet(key, executeTimeUtc, store)
    if not roomPlaying() or R.host then return store:Get(key, executeTimeUtc) end
    if type(SleepUntil) == 'function' and executeTimeUtc then SleepUntil(executeTimeUtc + R.GET_MARGIN) end
    if not (R.connected and R.welcomed) or not R.InTask() then
        log('GameState GET without the host; using this PC\'s copy')
        return store:Get(key, executeTimeUtc)
    end
    R.nextID = R.nextID + 1
    local id = R.nextID
    R.Send(0, { t = 'gsget', id = id, key = key })
    R.WaitFor(function() return R.replies[id] end, R.GET_TIMEOUT)
    local reply = R.replies[id]; R.replies[id] = nil
    if not reply then
        log('host did not answer the GameState GET; using this PC\'s copy')
        return store:Get(key, executeTimeUtc)
    end
    local state = R.StateFromWire(reply.value)
    if not state then return { ErrorCode = 404, ErrorName = 'SBG_E_NOT_FOUND' } end
    store.valuesSet[key] = state  -- keeps a local copy for a lost host
    return { ErrorCode = 0, Value = state }
end
function R.GameStateSet(key, value, executeTimeUtc, store)
    if not roomPlaying() or R.host then return store:Set(key, value, executeTimeUtc) end
    store.valuesSet[key] = value
    local wire = R.StateToWire(value)
    if R.connected and R.welcomed and wire then
        R.Send(0, { t = 'gsset', key = key, value = wire })
        log('sent the next show state to the host: ' .. tostring(wire.name))
    end
    return { ErrorCode = 0 }
end
local function hostGameStateStore()
    return type(NexiaGameStateStore) == 'function' and NexiaGameStateStore() or nil
end
function R.HostServeGameState(key)
    local store = hostGameStateStore()
    if not (store and R.IsGameStateKey(key)) then return nil end
    return R.StateToWire(store.valuesSet[key])
end
function R.HostApplyGameState(peer, key, wire)
    local peerState, store = R.peers[peer], hostGameStateStore()
    if not (peerState and store and R.IsGameStateKey(key)) then return false, 'not a show state' end
    local state = R.StateFromWire(wire)
    if not state then return false, 'malformed state' end
    local one = type(state.data) == 'table' and state.data.OnesXuid
    if not ownsXuid(peerState, one) then return false, 'only the One sets the show state' end
    store.valuesSet[key] = state
    log(string.format('peer %d (the One) set the show state to %s', peer, state.name))
    return true
end

function R.HostResetSessionStore()
    R.sumSeen = {}; R.topSeen = {}
    for _, state in pairs(R.peers) do state.lastScore = {} end
end

local function hostHandle(peer, message)
    local t = message.t
    if t == 'hello' then
        local state = R.peers[peer] or { xuids = {}, lastScore = {} }
        state.name = type(message.name) == 'string' and string.sub(message.name, 1, 32) or '?'
        R.peers[peer] = state
        log(string.format('peer %d hello name=%s xuid=%s version=%s', peer, state.name, tostring(message.xuid), tostring(message.version)))
        if message.version ~= R.VERSION then
            R.Send(peer, { t = 'reject', reason = 'version' }); return
        end
        R.Send(peer, { t = 'welcome', peer = peer, ms = message.ms, hostMs = nativeMs(),
                       session = R.currentSession, generation = R.sessionGeneration })
    elseif t == 'ping' then
        R.Send(peer, { t = 'pong', ms = message.ms, hostMs = nativeMs() })
    elseif t == 'get' then
        if type(message.id) ~= 'number' then return end
        local value = R.HostServeGet(message.key)
        R.Send(peer, { t = 'res', id = message.id, value = value })
    elseif t == 'post' then
        local ok, reason = R.HostApplyPost(peer, message.value)
        if not ok then log(string.format('peer %d post rejected: %s', peer, tostring(reason))) end
    elseif t == 'gsget' then
        if type(message.id) ~= 'number' then return end
        R.Send(peer, { t = 'res', id = message.id, value = R.HostServeGameState(message.key) })
    elseif t == 'gsset' then
        local ok, reason = R.HostApplyGameState(peer, message.key, message.value)
        if not ok then log(string.format('peer %d show state rejected: %s', peer, tostring(reason))) end
    elseif t == 'players' then
        R.HostPartyPlayers(peer, message.list)
    elseif t == 'qnet' then
        R.HostPartyQNet(peer, message)
    elseif t == 'session?' then
        -- A client whose show has ended asks for the next one. The host does not
        -- rely on its own game reaching intermission (nobody may be playing on it).
        if message.ended and type(message.after) == 'string' then R.HostAdvanceSession(message.after) end
        if R.currentSession then
            R.Send(peer, { t = 'session', session = R.currentSession, generation = R.sessionGeneration })
        end
    end
end

----------------------------------------------------------------------
-- Client state
----------------------------------------------------------------------
R.replies = {}
R.nextID = 0
R.connected = false
R.welcomed = false

local function clientHandle(message)
    local t = message.t
    if t == 'welcome' then
        R.welcomed = true
        R.peerID = message.peer
        if type(message.ms) == 'number' and type(message.hostMs) == 'number' then adoptOffset(message.ms, message.hostMs) end
        if type(message.session) == 'table' then
            R.hostSession = message.session; R.hostGeneration = message.generation
        end
        log(string.format('welcomed as peer %s; host session %s', tostring(message.peer),
            tostring(type(message.session) == 'table' and message.session.StartTime)))
    elseif t == 'reject' then
        R.rejected = message.reason; log('host rejected this client: ' .. tostring(message.reason))
    elseif t == 'pong' then
        if type(message.ms) == 'number' and type(message.hostMs) == 'number' then adoptOffset(message.ms, message.hostMs) end
    elseif t == 'res' then
        if type(message.id) == 'number' then R.replies[message.id] = { value = message.value } end
    elseif t == 'roster' or t == 'qnet' then
        R.PartyReceive(message)
    elseif t == 'session' then
        if type(message.session) == 'table' then
            R.hostSession = message.session; R.hostGeneration = message.generation
            log('host published session ' .. tostring(message.session.StartTime))
        end
    end
end

local function sendHello()
    refreshLocalPlayers()
    local xuid = R.localXuids and next(R.localXuids)
    R.Send(0, { t = 'hello', version = R.VERSION, ms = nativeMs(), xuid = xuid, name = R.localName })
end

----------------------------------------------------------------------
-- Pump: drain the native queue (called by the pump task and by waits)
----------------------------------------------------------------------
function R.Pump()
    if not R.active or R.pumping then return end
    R.pumping = true
    local ok, err = pcall(R.PumpOnce)
    R.pumping = false
    R.lastPump = nativeMs()
    R.pumpCount = (R.pumpCount or 0) + 1
    if not ok then log('pump error: ' .. tostring(err)) end
end
function R.PumpOnce()
    R.EnsureRoomClock()
    for i = 1, 64 do
        local packet = transport('recv')
        if packet == '' then break end
        local peerText, kind, payload = string.match(packet, '^(%d+)|(%a)|(.*)$')
        local peer = tonumber(peerText)
        if kind == 'j' then
            if R.host then log('peer ' .. peer .. ' connected ' .. payload)
            else R.connected = true; log('connected to host ' .. payload); sendHello() end
        elseif kind == 'l' then
            if R.host then
                local state = R.peers[peer]
                log('peer ' .. tostring(peer) .. ' left: ' .. payload)
                -- Keep its posted results; release its identities for a rejoin.
                if state then state.xuids = {} end
                R.peers[peer] = nil
                if R.HostBroadcastRoster then pcall(R.HostBroadcastRoster) end
            else
                R.connected = false; R.welcomed = false
                log('lost host: ' .. payload)
            end
        elseif kind == 'm' and peer then
            local ok, message = pcall(R.Decode, payload)
            if ok and type(message) == 'table' then
                if R.host then
                    local handled, err = pcall(hostHandle, peer, message)
                    if not handled then log('host handler error: ' .. tostring(err)) end
                else
                    clientHandle(message)
                end
            else
                log('dropped malformed packet from ' .. tostring(peer))
            end
        end
    end
    local beat = nativeMs()
    if not R.lastBeat or beat - R.lastBeat > 30000 then
        R.lastBeat = beat
        log(string.format('heartbeat pumps=%d status=%s', R.pumpCount or 0, transport('status')))
    end
    if not R.host and R.connected and R.welcomed then
        local t = nativeMs()
        if not R.lastPing or t - R.lastPing > 5000 then R.lastPing = t; R.Send(0, { t = 'ping', ms = t }) end
    end
end

local function inTask()
    return coroutine and coroutine.running and coroutine.running() ~= nil
end
local function waitStep()
    R.Pump()
    if Wait then Wait(0.02) end
end

-- Wait inside a task until fn() is truthy or timeout seconds pass.
local function waitFor(fn, timeout)
    if not inTask() then R.Pump(); return fn() end
    local deadline = nativeMs() + timeout * 1000
    while not fn() and nativeMs() < deadline do waitStep() end
    return fn()
end
R.InTask, R.WaitFor = inTask, waitFor

----------------------------------------------------------------------
-- Service wrappers
----------------------------------------------------------------------
-- BeginGetServerMessage records the original execute time for the GET.
function R.InstallMessageHooks()
    if not R.active or R.messageHooksInstalled or type(BeginGetServerMessage) ~= 'function' then return end
    R.messageHooksInstalled = true
    local originalBegin = BeginGetServerMessage
    BeginGetServerMessage = function(messageTable, programID, round, question, xuid, difficulty, serverExecuteTime, ...)
        R.pendingExecuteTime = serverExecuteTime
        local a, b = originalBegin(messageTable, programID, round, question, xuid, difficulty, serverExecuteTime, ...)
        R.pendingExecuteTime = nil
        return a, b
    end
end

local function waitUntilExecuteTime()
    local at = R.pendingExecuteTime
    if type(at) ~= 'number' or not inTask() then return end
    local target = math.min(at + R.GET_MARGIN, now() + 5)
    while now() < target do waitStep() end
end

-- Avatar manifests are engine objects and do not cross the network. Give each
-- final top-score record a manifest: the local player's own Avatar for this
-- PC's players, a party member's own Avatar when it arrived through the room
-- (their player on this PC was built from it), otherwise a studio stand-in
-- (as the offline bots do).
local function fixCalloutManifests(key, value)
    if type(key) ~= 'string' or not string.find(key, '%.EPShowEndTopScores$') then return value end
    if type(value) ~= 'table' or type(value.Values) ~= 'table' then return value end
    local localBodies = {}
    if type(ForAllLocalPlayers) == 'function' then
        pcall(ForAllLocalPlayers, function(player)
            if type(player) == 'table' and player.playerID and player.Body then localBodies[player.playerID] = player.Body end
        end)
    end
    local standins = EPStandIns and EPStandIns.Mob or {}
    for index, record in ipairs(value.Values) do
        local meta = record.Metadata
        if type(meta) == 'table' and meta.manifest == nil then
            local body = localBodies[meta.playerID]
            local id = meta.playerID
            if not body and type(id) == 'string' and R.partyAvatar and R.partyAvatar[id] and
               R.partyJoined and R.partyJoined[id] and Players and Players.GetPlayerByXUID then
                local ok, player = pcall(Players.GetPlayerByXUID, id)
                if ok and type(player) == 'table' and player.Body then body = player.Body end
            end
            if not body and #standins > 0 then
                local standin = standins[(index - 1) % #standins + 1]
                body = standin and standin.Body
            end
            if body then
                local ok, manifest = pcall(body.GetAvatarManifest, body)
                if ok then meta.manifest = manifest end
            end
        end
    end
    return value
end
R.FixCalloutManifests = fixCalloutManifests

function R.InstallServiceWrappers()
    R.localGet = LuaServer.MockGetMessage
    R.localPost = LuaServer.MockPostMessage
    LuaServer.MockGetMessage = function(key)
        if not R.IsSharedKey(key) or R.solo then return R.localGet(key) end
        if R.host then
            if R.serving then return R.localGet(key) end
            waitUntilExecuteTime()
            R.Pump()
            return fixCalloutManifests(key, emptyDefault(key, R.localGet(key)))
        end
        waitUntilExecuteTime()
        if not (R.connected and R.welcomed) or not inTask() then return emptyDefault(key, R.localGet(key)) end
        R.nextID = R.nextID + 1
        local id = R.nextID
        if not R.Send(0, { t = 'get', id = id, key = key }) then return emptyDefault(key, R.localGet(key)) end
        waitFor(function() return R.replies[id] end, R.GET_TIMEOUT)
        local reply = R.replies[id]; R.replies[id] = nil
        if not reply then
            log('host did not answer GET ' .. key .. '; using local data')
            return emptyDefault(key, R.localGet(key))
        end
        return fixCalloutManifests(key, emptyDefault(key, reply.value))
    end
    LuaServer.MockPostMessage = function(struct)
        local action = type(struct) == 'table' and type(struct.Actions) == 'table' and struct.Actions[1]
        local name = type(action) == 'table' and action.Name
        if R.IsSharedKey(name) and not R.host and not R.solo then
            -- Mirror locally (solo fallback), then send to the host for the shared show.
            local result = R.localPost(struct)
            if R.connected and R.welcomed then R.Send(0, { t = 'post', value = struct }) end
            return result
        end
        if R.IsSharedKey(name) and R.host and action.Xuid then
            R.localXuids = R.localXuids or {}
            R.localXuids[action.Xuid] = true
            R.sumSeen[name] = R.sumSeen[name] or {}
            R.sumSeen[name][action.Xuid] = true
        end
        return R.localPost(struct)
    end
end

----------------------------------------------------------------------
-- Session schedule
----------------------------------------------------------------------
-- Called from the offline grid fixture (a task). Host: records its session.
-- Client: waits for the host's welcome and plays the host's session.
-- Show length: the host's launcher setting applies to the whole room. The
-- launch script trims EPTimingsTable / EPBonusQuestions for its own setting
-- (keeping the full bonus table in NATIVE_EP_BONUS_FULL); a client re-trims to
-- the host's count. In the 19:53 two-PC run the friend's copy (3 questions)
-- ended while the host's copy was still on question 4.
function R.ShowLength()
    return EPTimingsTable and tonumber(EPTimingsTable.maxNumberOfQuestions) or nil
end
function R.ApplyShowLength(count)
    count = tonumber(count)
    if not count or count < 1 or count > 37 or not EPTimingsTable then return end
    count = math.floor(count)
    if R.ShowLength() == count then return end
    local before = R.ShowLength()
    EPTimingsTable.maxNumberOfQuestions = count
    local bonus = GameData and GameData.EPBonusQuestions
    if type(bonus) == 'table' then
        if type(NATIVE_EP_BONUS_FULL) ~= 'table' then
            NATIVE_EP_BONUS_FULL = {}
            for q, v in pairs(bonus) do NATIVE_EP_BONUS_FULL[q] = v end
        end
        local full = NATIVE_EP_BONUS_FULL
        for q in pairs(bonus) do bonus[q] = nil end
        for q, v in pairs(full) do if q < count then bonus[q] = v end end
        bonus[count] = full[37]  -- the final question keeps the show's last bonus
    end
    log(string.format('show length set to the host\'s %d questions (was %s)', count, tostring(before)))
end
local function withShowLength(session)
    if type(session) == 'table' and R.ShowLength() then session.NativeQuestions = R.ShowLength() end
    return session
end
-- Show mode: the host's session GameType (1 = Live/Primetime, 2 = Extended
-- Play) is what every copy plays; the client's own launcher choice is ignored.
local function adoptShowLength(session)
    if type(session) ~= 'table' then return end
    R.ApplyShowLength(session.NativeQuestions)
    local gameType = tonumber(session.GameType)
    if gameType == 1 or gameType == 2 then
        if gameType ~= R.lastGameType then
            log('show mode from host: ' .. (gameType == 1 and 'Live (Primetime)' or 'Extended Play'))
        end
        R.lastGameType = gameType
        OFFLINE_SESSION_GAME_TYPE = gameType
    end
end

function R.ResolveStartupSession(session)
    if not R.active then return session end
    if R.host then
        R.currentSession = withShowLength(copy(session)); R.sessionGeneration = R.sessionGeneration + 1
        -- Clients that connected before the schedule existed are waiting for it.
        R.Send(0, { t = 'session', session = R.currentSession, generation = R.sessionGeneration })
        log('hosting session ' .. tostring(session.StartTime) .. ' (' .. (tonumber(session.GameType) == 1 and 'Live (Primetime)' or 'Extended Play') .. ')')
        return session
    end
    log('waiting for the host session (mode ' .. tostring(R.mode) .. ')')
    local lastAsk = 0
    waitFor(function()
        if R.hostSession ~= nil or R.rejected then return true end
        local t = nativeMs()
        if R.connected and R.welcomed and t - lastAsk > 2000 then lastAsk = t; R.Send(0, { t = 'session?' }) end
        return false
    end, (R.mode == 'steam-join') and R.STEAM_WELCOME_TIMEOUT or R.WELCOME_TIMEOUT)
    if R.hostSession then
        log('playing host session ' .. tostring(R.hostSession.StartTime))
        adoptShowLength(R.hostSession)
        return copy(R.hostSession)
    end
    -- Without the host's schedule, this copy plays alone and keeps its data local.
    R.solo = true
    log('no host session; playing a solo session (bots and stats stay on this PC)')
    return session
end

-- Host: move the shared show on to the next session once the show that started
-- at afterStart is over. Idempotent: whoever reports the end first (a client,
-- or the host's own intermission) advances it; later calls get the same session.
function R.HostAdvanceSession(afterStart)
    if not (R.active and R.host and R.currentSession) then return nil end
    if R.currentSession.StartTime ~= afterStart then return copy(R.currentSession) end
    if type(NativeSecondsToIso) ~= 'function' then return nil end
    local nowSeconds = math.floor(nativeMs() / 1000)
    local session = copy(R.currentSession)
    session.StartTime = NativeSecondsToIso(nowSeconds + 60)
    session.EndTime = NativeSecondsToIso(nowSeconds + 3660)
    session.Duration = 60
    -- Fresh shared store and bots for the new show.
    local pid = CurrentProgram and CurrentProgram.PID and CurrentProgram.PID()
    local prefix = SESSION_PREFIX .. tostring(pid) .. '.'
    for key in pairs(Lua_HVStore or {}) do
        if type(key) == 'string' and string.sub(key, 1, #prefix) == prefix then Lua_HVStore[key] = nil end
    end
    local bots = GameData and GameData.NexiaOfflineBots
    if bots then
        bots.questions = {}
        for _, profile in ipairs(bots.profiles or {}) do profile.finalScore = 0; profile.streak = 0 end
    end
    R.PublishNextSession(session)
    return copy(session)
end

-- Host: after preparing the next session, publish it to every client.
function R.PublishNextSession(session)
    if not (R.active and R.host) then return end
    R.currentSession = withShowLength(copy(session)); R.sessionGeneration = R.sessionGeneration + 1
    R.HostResetSessionStore()
    R.Send(0, { t = 'session', session = R.currentSession, generation = R.sessionGeneration })
    log('published next session ' .. tostring(session.StartTime))
end

-- Client: wait for the host's next session (one starting after afterStart).
function R.AwaitNextSession(afterStart)
    if not R.active or R.host then return nil end
    -- A copy that started solo (host not found in time) joins the shared show
    -- from the next show on, once it is connected. Its own bots retire.
    if R.solo then
        if not (R.connected and R.welcomed) then return nil end
        R.solo = false
        local bots = GameData and GameData.NexiaOfflineBots
        if bots then bots.count = 0; bots.profiles = {}; bots.questions = {} end
        log('host found; leaving solo play from the next show')
    end
    local function ready()
        local s = R.hostSession
        return s and s.StartTime and s.StartTime ~= afterStart and s
    end
    local lastAsk = 0
    local session = waitFor(function()
        local t = nativeMs()
        if R.connected and R.welcomed and t - lastAsk > 2000 then
            lastAsk = t; R.Send(0, { t = 'session?', after = afterStart, ended = true })
        end
        return ready()
    end, R.NEXT_SESSION_TIMEOUT)
    if session then adoptShowLength(session); return copy(session) end
    -- No answer: play on alone, keeping data local, until the host is back.
    R.solo = true
    log('no next session from the host; playing solo until it answers again')
    return nil
end

----------------------------------------------------------------------
-- Player stats on disk (season score, super streaks, lifetime stats). The
-- original kept these in the retired Xbox LIVE user storage service. Saved per
-- player XUID in userdata\storage\ beside the executable (NativeStorage).
----------------------------------------------------------------------
local function storageName(xuid)
    if type(xuid) ~= 'string' or not string.match(xuid, '^[%w]+$') or #xuid > 40 then return nil end
    return 'player-' .. xuid
end
function NativeSaveUserStorage(xuid, storage)
    local name = storageName(xuid)
    if not name or type(storage) ~= 'table' or type(NativeStorage) ~= 'function' then return false end
    local ok, data = pcall(R.Encode, { version = 1, storage = storage })
    if not ok then log('stats not saved for ' .. xuid .. ': ' .. tostring(data)); return false end
    local result = NativeStorage('write', name, data)
    if result ~= 'ok' then log('stats save failed for ' .. xuid); return false end
    return true
end
function NativeLoadUserStorage(xuid)
    local name = storageName(xuid)
    if not name or type(NativeStorage) ~= 'function' then return nil end
    local data = NativeStorage('read', name, '')
    if type(data) ~= 'string' or data == '' then return nil end
    local ok, saved = pcall(R.Decode, data)
    if not ok or type(saved) ~= 'table' or saved.version ~= 1 or type(saved.storage) ~= 'table' then
        log('stats file for ' .. xuid .. ' is unreadable; starting fresh')
        return nil
    end
    log('loaded saved stats for ' .. xuid)
    return saved.storage
end

----------------------------------------------------------------------
-- Main menu "Invite Friends" (called from onevslive.lua in the PC build)
----------------------------------------------------------------------
function NativeInviteFromMainMenu(user, button)
    local title, text = 'Invite Friends', ''
    local steam = R.active and (R.mode == 'steam-host' or R.mode == 'steam-join')
    local party = steam and transport('party') or ''
    local members = {}
    for name in string.gmatch(party, '[^|]+') do members[#members + 1] = name end
    local partyText = #members > 0 and ('\nParty (' .. #members .. '): ' .. table.concat(members, ', ')) or ''
    if not R.active then
        text = 'Xbox LIVE invites are not available in the PC build. To play with friends, host a party ' ..
               'from the launcher; friends choose "Join a friend\'s party".'
    elseif R.host then
        local id = string.match(transport('self'), '^steam:(%d+)$')
        if steam then
            transport('invite')  -- Steam overlay invite dialog, when the overlay is available
            text = 'You are hosting a party of up to 4. Invite Steam friends from the Steam overlay, or they choose ' ..
                   '"Join a friend\'s party" in the launcher. Your Steam ID: ' .. tostring(id) .. partyText
        else
            text = 'You are hosting a test room. Other copies on this PC can join it.'
        end
    else
        local status = transport('status')
        if status == 'searching' then text = 'Looking for a Steam friend hosting a 1 vs 100 party...'
        elseif status == 'party-full' then text = 'That party is full (4 players). Still looking...'
        else
            text = (R.connected and 'You are in the host\'s party' or 'Waiting for the host') ..
                   (R.solo and ' (playing solo until the next show).' or '.') .. partyText
        end
    end
    if DialogBox and DialogBox.ShowHalf then
        DialogBox.ShowHalf(title, text, { button1Txt = LOCALIZE and LOCALIZE("ACCEPT") or 'OK' })
    end
end

----------------------------------------------------------------------
-- Party: remote players in the party bar (PC build)
--
-- The original filled the four party slots from the Xbox QNet network
-- session; every copy then ran the same player code for remote players
-- (Player:New with no controller) and kept them current with QNet player
-- messages (answers, scores, roles, creation data). Here the room carries
-- those same messages, and each copy's player list is shared through the
-- host, so the original remote-player code does the rest.
--
--   * Each copy reports its local players to the host ('players'); the host
--     validates ownership and broadcasts the full party ('roster').
--   * A copy creates the other copies' players through the original
--     LuaOnNetworkPlayerJoin, and removes them through LuaOnNetworkPlayerLeave.
--   * QNetSendToAll / QNetSendTo also go over the room (player and amping
--     events only; invite and grid events never do), and arrive through the
--     original LuaOnNetworkDataReceived.
--   * Each copy sends its player's own Avatar (the console manifest, as hex)
--     with its player list; the roster carries it to everyone. A remote
--     player's Avatar is built from it (items a PC does not have show as the
--     standard item). A copy that sent none gets a stand-in Avatar.
----------------------------------------------------------------------
R.PARTY_EVENTS = {
    PlayerUpdate = true, PlayerType = true, PlayerAnswerData = true, PlayerLockHelp = true,
    LockLifelineView = true, RequestPlayerCreationData = true, PlayerCreationData = true,
    PlayerInGame = true, AvatarAmpingNetworkEvent = true,
}
R.partyRoster = {}     -- every copy: xuid -> gamertag, for the whole party
R.partyJoined = {}     -- every copy: remote xuid -> gamertag, players created here
R.partyInbox = {}      -- every copy: queued room messages for the game (delivered in a task)
R.partyLocal = ''      -- every copy: last local player list sent
R.partyAvatar = {}     -- every copy: remote xuid -> manifest hex handed to the Avatar provider
R.avatarCache = {}     -- every copy: local xuid -> { hex, ms }

-- This copy's own Avatar manifest (hex) for a local player; '' for players
-- without a saved profile Avatar (couch guests). Refreshed every 10 s, so an
-- Avatar Editor save reaches the party in a later roster.
local function localAvatar(xuid)
    local cached = R.avatarCache[xuid]
    local t = nativeMs()
    if cached and t - cached.ms < 10000 then return cached.hex end
    local ok, hex = pcall(transport, 'avatar', xuid)
    hex = (ok and type(hex) == 'string' and #hex == 2000) and hex or ''
    R.avatarCache[xuid] = { hex = hex, ms = t }
    return hex
end
local function validAvatar(hex)
    return type(hex) == 'string' and #hex == 2000 and not string.find(hex, '[^0-9a-fA-F]')
end
-- Hand a party member's Avatar to the native provider (once per change).
local function useRemoteAvatar(xuid, hex)
    if not validAvatar(hex) or R.partyAvatar[xuid] == hex then return end
    local ok, result = pcall(transport, 'avatarof', xuid .. '|' .. hex)
    if ok and result == 'ok' then
        R.partyAvatar[xuid] = hex
        log('party: Avatar received for ' .. xuid)
    else
        log('party: Avatar for ' .. xuid .. ' not used: ' .. tostring(result))
    end
end
R.UseRemoteAvatar = useRemoteAvatar

local function cleanName(name)
    if type(name) ~= 'string' then return nil end
    name = string.gsub(string.sub(name, 1, 32), '[%c]', '')
    if name == '' then return nil end
    return name
end
local function cleanXuid(xuid)
    if type(xuid) ~= 'string' or #xuid == 0 or #xuid > 32 or not string.match(xuid, '^[%w]+$') then return nil end
    return xuid
end

local function localPlayers()
    local list, key = {}, {}
    if not (Players and Players.GetLocalPlayerList) then return list, '' end
    local ok, xuids = pcall(Players.GetLocalPlayerList)
    if not ok or type(xuids) ~= 'table' then return list, '' end
    for _, xuid in ipairs(xuids) do
        local player = Players.GetPlayerByXUID(xuid)
        local name = player and cleanName(player.playerName)
        if cleanXuid(xuid) and name then
            list[#list + 1] = { x = xuid, n = name }
            key[#key + 1] = xuid .. '=' .. name
        end
    end
    table.sort(key)
    return list, table.concat(key, ';')
end

local function isLocalXuid(xuid)
    local list = localPlayers()
    for _, entry in ipairs(list) do if entry.x == xuid then return true end end
    return false
end

-- QNet object: answer gamertag / membership questions for remote players.
local function wrapQNet()
    if not SessionData then return end
    local real = SessionData.qNet
    if type(real) == 'table' and rawget(real, '__nativeRoomProxy') then return end
    if real == nil then
        if type(QNet) ~= 'function' then return end
        local ok, created = pcall(QNet)
        if not ok or created == nil then return end
        real = created
    end
    local overrides = {
        GetGamerTag = function(self, xuid)
            if R.partyRoster[xuid] and not isLocalXuid(xuid) then return R.partyRoster[xuid] end
            return real:GetGamerTag(xuid)
        end,
        GetAllXUIDs = function(self)
            local result, seen, n = {}, {}, 0
            local ok, list = pcall(function() return real:GetAllXUIDs() end)
            if ok and type(list) == 'table' then
                for key, xuid in pairs(list) do result[key] = xuid; seen[xuid] = true end
            end
            for xuid in pairs(R.partyJoined) do
                if not seen[xuid] then n = n + 1; result[100 + n] = xuid end
            end
            return result
        end,
        GetLocalXUIDs = function(self)
            local ok, list = pcall(function() return real:GetLocalXUIDs() end)
            return (ok and type(list) == 'table') and list or {}
        end,
    }
    local proxy = { __nativeRoomProxy = true }
    setmetatable(proxy, { __index = function(t, k)
        local override = overrides[k]
        if override then return override end
        local value = real[k]
        if type(value) ~= 'function' then return value end
        return function(self, ...) return value(real, ...) end
    end })
    SessionData.qNet = proxy
    log('party: QNet bridged')
end

-- Outgoing QNet messages also go to the other copies.
local function relayQNet(data, toList)
    if not R.active or type(data) ~= 'table' or not R.PARTY_EVENTS[data.Type] then return end
    local sender = cleanXuid(data.playerID)
    if not sender then return end
    local to
    if type(toList) == 'table' then
        to = {}
        for _, xuid in pairs(toList) do if cleanXuid(xuid) then to[#to + 1] = xuid end end
    end
    if data.Type ~= 'AvatarAmpingNetworkEvent' then log('party event send: ' .. tostring(data.Type) .. ' from=' .. sender) end
    local message = { t = 'qnet', from = sender, to = to, d = data }
    if R.host then
        R.Send(0, message)
    elseif R.connected and R.welcomed then
        R.Send(0, message)
    end
end
function R.InstallPartyHooks()
    R.partyHandlerHooks = R.partyHandlerHooks or {}
    for _, event in ipairs({'PlayerInGame', 'PlayerAnswerData', 'PlayerUpdate'}) do
        if PlayerEventHandler and type(PlayerEventHandler[event]) == 'function' and not R.partyHandlerHooks[event] then
            local original, name = PlayerEventHandler[event], event
            R.partyHandlerHooks[name] = true
            PlayerEventHandler[name] = function(playerID, data, toPlayerID)
                local result = original(playerID, data, toPlayerID)
                local player = type(GetGlobalPlayer) == 'function' and GetGlobalPlayer(playerID)
                log('party event applied: ' .. name .. ' from=' .. tostring(playerID) ..
                    ' result=' .. tostring(result) .. ' widgets=' .. tostring(player and player.shouldShowWidgets) ..
                    ' locked=' .. tostring(player and player.bAnswerLocked))
                return result
            end
        end
    end
    if R.partyHooks then return end
    if type(QNetSendToAll) ~= 'function' or type(QNetSendTo) ~= 'function' then return end
    R.partyHooks = true
    local engineSendToAll, engineSendTo = QNetSendToAll, QNetSendTo
    QNetSendToAll = function(data, ...)
        local ok, err = pcall(relayQNet, data, nil)
        if not ok then log('party relay error: ' .. tostring(err)) end
        return engineSendToAll(data, ...)
    end
    QNetSendTo = function(playersList, data, ...)
        local ok, err = pcall(relayQNet, data, playersList)
        if not ok then log('party relay error: ' .. tostring(err)) end
        return engineSendTo(playersList, data, ...)
    end
    -- Lobby dancing ("Press Y to amp"): the original SendAmpingUpdate only sends
    -- while QNet reports a network session, which the PC build never has (it
    -- stays offline on purpose). With party players present, send the same
    -- update anyway; the receiving copies already apply it
    -- (LuaOnNetworkDataReceived -> NetworkAmpingData -> UpdateNetworkAmping).
    if type(SendAmpingUpdate) == 'function' and not R.ampingHook then
        R.ampingHook = true
        local engineSendAmpingUpdate = SendAmpingUpdate
        SendAmpingUpdate = function(playerTable, numButtonPresses, messageID, ...)
            local inSession = SessionData and SessionData.qNet and SessionData.qNet:IsInSession()
            if inSession or not next(R.partyJoined) or type(playerTable) ~= 'table' then
                return engineSendAmpingUpdate(playerTable, numButtonPresses, messageID, ...)
            end
            local animNumber = playerTable.isSustainingFinisher and playerTable.levelUpAnimNumber or playerTable.ampAnimNumber
            local animName = playerTable.isSustainingFinisher and playerTable:FinisherAnimName() or
                             (AmpingAnimationList and AmpingAnimationList[animNumber])
            QNetSendToAll(CreateQNetData(playerTable.playerID, EventType.Avatar.Amping, {
                isInFinisher = playerTable.isSustainingFinisher,
                currAnimName = animName,
                ampAnimSpeed = playerTable.ampSpeed,
                numButtonPresses = numButtonPresses,
                messageID = messageID,
            }))
        end
    end
    if type(CreateAvatar) == 'function' then
        local engineCreateAvatar = CreateAvatar
        CreateAvatar = function(xuid, ...)
            if type(xuid) == 'string' and R.partyJoined[xuid] then
                -- Their own Avatar when it arrived (the provider serves it for
                -- this XUID); a stand-in otherwise.
                if R.partyAvatar[xuid] then return engineCreateAvatar(xuid, ...) end
                return engineCreateAvatar('Random', ...)
            end
            return engineCreateAvatar(xuid, ...)
        end
    end
end

-- Host: rebuild the party from its own players and each peer's report.
function R.HostBroadcastRoster()
    local roster, list, avatars = {}, {}, {}
    for _, entry in ipairs((localPlayers())) do
        roster[entry.x] = entry.n
        local hex = localAvatar(entry.x)
        if hex ~= '' then avatars[entry.x] = hex end
    end
    for _, state in pairs(R.peers) do
        for xuid, name in pairs(state.party or {}) do
            if not roster[xuid] then
                roster[xuid] = name
                local hex = state.avatars and state.avatars[xuid]
                if hex then avatars[xuid] = hex; useRemoteAvatar(xuid, hex) end
            end
        end
    end
    for xuid, name in pairs(roster) do list[#list + 1] = { x = xuid, n = name, m = avatars[xuid] } end
    R.partyRoster = roster
    R.lastRosterSent = nativeMs()
    R.Send(0, { t = 'roster', list = list })
end

-- Host: a peer reports its local players.
function R.HostPartyPlayers(peer, list)
    local state = R.peers[peer]
    if not state or type(list) ~= 'table' then return end
    local party, count, avatars = {}, 0, {}
    for _, entry in ipairs(list) do
        local xuid, name = cleanXuid(type(entry) == 'table' and entry.x), cleanName(type(entry) == 'table' and entry.n)
        if xuid and name and count < 4 then
            if state.xuids[xuid] or ownsXuid(state, xuid) then
                party[xuid] = name; count = count + 1
                if validAvatar(entry.m) then avatars[xuid] = entry.m end
            else log(string.format('party: peer %d cannot claim %s', peer, xuid)) end
        end
    end
    local changed = false
    for xuid in pairs(party) do if not (state.party and state.party[xuid]) then changed = true end end
    for xuid in pairs(state.party or {}) do if not party[xuid] then changed = true end end
    state.party = party
    state.avatars = avatars
    if changed then log(string.format('party: peer %d has %d player(s)', peer, count)) end
    R.HostBroadcastRoster()
end

-- Host: a peer's game message. Only for that peer's own players.
function R.HostPartyQNet(peer, message)
    local state = R.peers[peer]
    local data = type(message.d) == 'table' and message.d
    if not state or not data or not R.PARTY_EVENTS[data.Type] then return end
    if not (state.party and state.party[message.from]) or data.playerID ~= message.from then
        log(string.format('party: dropped %s from peer %d (not its player)', tostring(data.Type), peer)); return
    end
    R.Send(0, message)                 -- every client (the sender ignores its own)
    R.partyInbox[#R.partyInbox + 1] = message
end

-- Every copy: a game message or roster from the room.
function R.PartyReceive(message)
    if message.t == 'roster' then
        local roster = {}
        if type(message.list) == 'table' then
            for _, entry in ipairs(message.list) do
                local xuid, name = cleanXuid(type(entry) == 'table' and entry.x), cleanName(type(entry) == 'table' and entry.n)
                if xuid and name then
                    roster[xuid] = name
                    if entry.m ~= nil and not isLocalXuid(xuid) then useRemoteAvatar(xuid, entry.m) end
                end
            end
        end
        R.partyRoster = roster
    elseif message.t == 'qnet' then
        R.partyInbox[#R.partyInbox + 1] = message
    end
end

local function gameReady()
    if not (STATE_Lobby and STATE_Lobby.bAllPackagesInitialized) then return false end
    local list = localPlayers()
    if #list == 0 then return false end
    if PendingPlayers and PendingPlayers.IsCreatingPlayer then
        for _, entry in ipairs(list) do
            local ok, creating = pcall(PendingPlayers.IsCreatingPlayer, PendingPlayers, entry.x)
            if ok and creating then return false end
        end
    end
    return true
end

-- Runs in the pump task: report local players, create/remove remote players,
-- and hand queued messages to the game.
function R.PartyTick()
    if not R.active then return end
    R.InstallPartyHooks()
    wrapQNet()
    local t = nativeMs()
    local list, key = localPlayers()
    if gameReady() then
        if R.host then
            if key ~= R.partyLocal then R.partyLocal = key; R.HostBroadcastRoster() end
        elseif R.connected and R.welcomed and (key ~= R.partyLocal or t - (R.lastPlayersSent or 0) > 5000) then
            -- Also repeated every 5 s, so a reconnect or a lost report heals itself.
            R.partyLocal = key; R.lastPlayersSent = t
            local report = {}
            for i, entry in ipairs(list) do
                local hex = localAvatar(entry.x)
                report[i] = { x = entry.x, n = entry.n, m = hex ~= '' and hex or nil }
            end
            R.Send(0, { t = 'players', list = report })
        end
    end
    if R.host and t - (R.lastRosterSent or 0) > 5000 then R.HostBroadcastRoster() end
    if not R.host and not (R.connected and R.welcomed) then R.partyRoster = {}; R.partyLocal = '' end

    -- Remove players who left the party.
    for xuid in pairs(R.partyJoined) do
        if not R.partyRoster[xuid] then
            R.partyJoined[xuid] = nil
            log('party: ' .. xuid .. ' left')
            if type(LuaOnNetworkPlayerLeave) == 'function' then pcall(LuaOnNetworkPlayerLeave, { xuid = xuid }) end
        end
    end
    -- Add players who joined (one per tick, four slots in all).
    if gameReady() and SessionData and SessionData.qNet then
        local slots = 4 - #list
        for _ in pairs(R.partyJoined) do slots = slots - 1 end
        for xuid, name in pairs(R.partyRoster) do
            if slots <= 0 then break end
            if not R.partyJoined[xuid] and not isLocalXuid(xuid) then
                R.partyJoined[xuid] = name
                log('party: ' .. name .. ' (' .. xuid .. ') joined')
                if type(LuaOnNetworkPlayerJoin) == 'function' then
                    local ok, err = pcall(LuaOnNetworkPlayerJoin, { xuid = xuid })
                    if not ok then
                        R.partyJoined[xuid] = nil -- callback failed; permit a later pump to retry
                        log('party: join failed: ' .. tostring(err))
                    end
                end
                break
            end
        end
    end
    -- Deliver game messages (after creation, the game also replays any it queued).
    if #R.partyInbox > 0 then
        local inbox = R.partyInbox
        R.partyInbox = {}
        for _, message in ipairs(inbox) do
            local data = message.d
            local deliver = type(data) == 'table' and R.PARTY_EVENTS[data.Type] and not isLocalXuid(message.from)
                            and R.partyRoster[message.from] ~= nil
            if deliver and type(message.to) == 'table' then
                deliver = false
                for _, xuid in ipairs(message.to) do if isLocalXuid(xuid) then deliver = true end end
            end
            if deliver and type(LuaOnNetworkDataReceived) == 'function' then
                data.playerID = message.from
                -- Original PlayerInGame needs a LOCAL destination to enable
                -- shouldShowWidgets. Broadcasts must fan out to local recipients;
                -- a targeted list's first entry may belong to another console.
                local targets = {}
                for _, entry in ipairs(list) do
                    local selected = type(message.to) ~= 'table'
                    if not selected then
                        for _, xuid in ipairs(message.to) do if xuid == entry.x then selected = true; break end end
                    end
                    if selected then targets[#targets + 1] = entry.x end
                end
                -- PlayerInGame has per-recipient behavior. Other events update
                -- the sender's shared player once (avoid duplicate score/effects).
                for index, target in ipairs(targets) do
                    if index == 1 or data.Type == 'PlayerInGame' then
                        log('party event receive: ' .. tostring(data.Type) .. ' from=' .. tostring(message.from) .. ' to=' .. target)
                        local ok, err = pcall(LuaOnNetworkDataReceived,
                            { fromxuid = message.from, toxuid = target, data = data })
                        if not ok then log('party: message failed: ' .. tostring(err)) end
                    end
                end
            end
        end
    end
end

----------------------------------------------------------------------
-- Show clock
----------------------------------------------------------------------
function R.NewRoomClock()
    local base, baseMs = UTCTime(transport('utc')), nativeMs()
    return {
        __nativeRoomClock = true,
        Now = function(self) return base + (nativeMs() - baseMs) / 1000 + R.clockOffset end,
        GetLastErrorData = function(self) return { offline = true, source = 'room-clock' } end,
    }
end
-- The game's clock (global utcclock) is created by startup and can be
-- recreated later (luamessagehandlers.lua), sometimes after the room has
-- installed: the 19:53 and 20:03 runs never logged the replacement, and both
-- copies kept drifting (host question 1 at +75, +69, +67 s after the same
-- schedule offset). Checked on every pump, so whichever clock the game holds
-- is replaced as soon as it exists.
function R.EnsureRoomClock()
    if not (R.active and CONTENT_MOCK_SERVER and CONTENT_NO_XLSP_ERROR) then return end
    if utcclock == nil or (type(utcclock) == 'table' and rawget(utcclock, '__nativeRoomClock')) then return end
    local ok, before = pcall(function() return utcclock:Now() end)
    utcclock = R.NewRoomClock()
    local drift = ok and type(before) == 'number' and (utcclock:Now() - before) or nil
    R.clockReplaced = (R.clockReplaced or 0) + 1
    log('room clock: wall-clock UTC now in use' ..
        (drift and string.format(' (the game clock was %.1f s %s)', math.abs(drift), drift >= 0 and 'behind' or 'ahead') or ''))
end

----------------------------------------------------------------------
-- Install
----------------------------------------------------------------------
function R.Install()
    if R.installed then return R.active end
    R.installed = true
    if type(NativeRoomTransport) ~= 'function' then return false end
    local mode = transport('mode')
    if mode == '' then return false end
    R.mode = mode
    R.host = mode == 'local-host' or mode == 'steam-host'
    R.active = true
    R.InstallServiceWrappers()
    log(string.format('room %s (%s); status %s; self %s', R.host and 'host' or 'client', mode,
        transport('status'), transport('self')))
    -- Show clock for every copy in a room: this PC's wall-clock UTC, read on
    -- every call, plus the measured offset to the host. The startup clock
    -- (startup.lua) is read once and then advanced with the engine's Now(); in
    -- the 19:38 two-PC run the friend's copy ran its whole show 37 s behind the
    -- host's (intro 37.4 s later, every question after it too) although both
    -- played the same session start and the PCs' clocks agreed to 0.6 s, which
    -- fits that clock losing time while the game was loading. Reading the wall
    -- clock each time cannot fall behind.
    local engineClock = UTCClock
    UTCClock = function(...)
        if not (CONTENT_MOCK_SERVER and CONTENT_NO_XLSP_ERROR) then return engineClock(...) end
        return R.NewRoomClock()
    end
    R.EnsureRoomClock()
    return true
end

-- Keep the room serviced every frame. A background task alone was not enough:
-- in the first two-copy run the host only answered clients while one of its own
-- GETs was waiting, so most client GETs timed out. GetAbsoluteTime is called
-- constantly by the original scripts (tasks, timers, UI), so it also drives the
-- pump, at most every 30 ms; Pump never yields and is re-entrancy guarded.
function R.StartPump()
    if not R.active or R.pumpStarted then return end
    R.pumpStarted = true
    if type(GetAbsoluteTime) == 'function' then
        local engineGetAbsoluteTime = GetAbsoluteTime
        local calls = 0
        GetAbsoluteTime = function(...)
            calls = calls + 1
            if calls >= 16 and not R.pumping then
                calls = 0
                if nativeMs() - (R.lastPump or 0) >= 30 then R.Pump() end
            end
            return engineGetAbsoluteTime(...)
        end
        now = function() return engineGetAbsoluteTime() end
    end
    if type(CreateGlobalTask) == 'function' then
        CreateGlobalTask(function()
            log('pump task started')
            while true do
                R.Pump()
                local ok, err = pcall(R.PartyTick)
                if not ok and not R.partyErrorLogged then R.partyErrorLogged = true; log('party error: ' .. tostring(err)) end
                Wait(0.05)
            end
        end)
    end
end

R.Install()
