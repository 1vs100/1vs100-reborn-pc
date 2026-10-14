-- Room show clock follows wall time even when the engine's Now() stalls.
--   texlua native-clock-test.lua <NativeRoom.lua> <luamockserver.lua>
local T = 1000.0
local env = setmetatable({}, { __index = _G }); env._G = env
env.Lua_HVStore = {}; env.printf = function() end; env.print = function() end
env.DependsOnLuaFile = function() end
env.CONTENT_MOCK_SERVER = true; env.CONTENT_NO_XLSP_ERROR = true
env.GetAbsoluteTime = function() return 0 end
env.Now = function() return 5 end                      -- engine timer frozen (loading)
env.UTCTime = function(iso) return tonumber(iso) or 0 end
env.UTCClock = function() local o, a = 0, env.Now() return { Now = function() return o + (env.Now() - a) end } end
env.utcclock = env.UTCClock()                            -- startup clock, created before the room loads
env.NativeDebugLog = function() end
env.NativeRoomTransport = function(op) if op == 'mode' then return 'local-join' end
  if op == 'utcms' then return tostring(math.floor(T * 1000)) end if op == 'utc' then return tostring(T) end
  if op == 'status' then return 'connecting' end if op == 'self' then return 'local' end return '' end
for _, file in ipairs({ arg[2], arg[1] }) do local f = assert(io.open(file)); local src = f:read('a'); f:close(); assert(load(src, '@' .. file, 't', env))() end
local fails = 0
local function check(name, ok, detail) print((ok and 'PASS ' or 'FAIL ') .. name .. ' (' .. tostring(detail) .. ')'); if not ok then fails = fails + 1 end end
local a = env.utcclock:Now(); T = T + 37
local b = env.utcclock:Now()
check('startup clock replaced; show clock advances 37 s while the engine timer is frozen', math.abs((b - a) - 37) < 0.01, b - a)
env.NativeRoom.clockOffset = -0.6; local c = env.utcclock:Now()
check('host clock offset applied', math.abs((c - b) + 0.6) < 0.01, c - b)
local fresh = env.UTCClock(); T = T + 10
check('new clocks also follow wall time', math.abs(fresh:Now() - (env.utcclock:Now())) < 0.01, fresh:Now() - env.utcclock:Now())
-- The game creates (or recreates) its clock after the room installed.
env.utcclock = { Now = function() return 12345 end }    -- engine-time clock, stalled
env.NativeRoom.Pump()
local d = env.utcclock:Now(); T = T + 20
check('clock created after install is replaced on the next pump and follows wall time',
      rawget(env.utcclock, '__nativeRoomClock') == true and math.abs(env.utcclock:Now() - d - 20) < 0.01, env.utcclock:Now() - d)
print(fails == 0 and 'all passed' or (fails .. ' failed')); os.exit(fails == 0 and 0 or 1)
