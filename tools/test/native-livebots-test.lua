-- Live (Primetime) Mob bots against the original local game service.
--   texlua native-livebots-test.lua <NativeLiveBots.lua> <luamockserver.lua>
local env = setmetatable({}, { __index = _G }); env._G = env
env.Lua_HVStore = {}; env.printf = function() end; env.print = function() end
env.DependsOnLuaFile = function() end
env.length = function(t) local n = 0 for _ in pairs(t) do n = n + 1 end return n end
env.deepcopy = function(v) if type(v) ~= 'table' then return v end local o = {} for k, x in pairs(v) do o[k] = env.deepcopy(x) end return o end
env.logs = {}; env.NativeDebugLog = function(s) table.insert(env.logs, s) end
env.CurrentProgram = {}; local mode = 'PT'
env.isPrimetimeGame = function() return mode == 'PT' end
env.GameData = { AIMob = { DifficultyLookup = { [1] = 0, [2] = 1, [3] = 3, [4] = 6, [5] = 8 }, baseProbCorrect = 0.5, skillDiffModifier = 0.15 } }
env.GetCorrectAnswer = function(q) return 2 end
env.GetQuestionDifficulty = function(q) return q <= 3 and 1 or 5 end
env.g_TheMob = { '2533274790395904' }                  -- one real Mob member
for _, file in ipairs({ arg[2], arg[1] }) do local f = assert(io.open(file)); local src = f:read('a'); f:close(); assert(load(src, '@' .. file, 't', env))() end
math.randomseed(7)
local fails = 0
local function check(name, ok, detail) print((ok and 'PASS ' or 'FAIL ') .. name .. ' (' .. tostring(detail) .. ')'); if not ok then fails = fails + 1 end end
local function key(r, q) return 'v1.Session.MGS.1vs100.pid.EP.Round.' .. r .. '.Question.' .. q .. '.MobChoices' end
local function counts(v) local right, total = 0, 0 for _, e in ipairs(v.Values) do total = total + 1 if e.Metadata.correct then right = right + 1 end end return right, total end
-- the real Mob member answers through the original Top100 post
env.LuaServer.MockTop(key(1, 1), 100, 2, { xuid = '2533274790395904', correct = true })
local v1 = env.LuaServer.MockGetMessage(key(1, 1))
local right1, total1 = counts(v1)
check('question 1: the real answer plus 99 bot answers (full Mob of 100)', total1 == 100, total1)
check('easy question: most bots right', right1 >= 70 and right1 <= 100, right1)
local again = env.LuaServer.MockGetMessage(key(1, 1))
check('a repeated read gives the same answers', select(2, counts(again)) == 100 and select(1, counts(again)) == right1, select(1, counts(again)))
local v4 = env.LuaServer.MockGetMessage(key(1, 4))
local right4, total4 = counts(v4)
check('later question: only bots still standing answer', total4 == right1 - 1, total4 .. ' answers, ' .. (right1 - 1) .. ' bots standing')
check('very hard question eliminates many', right4 < total4 * 0.7, right4 .. '/' .. total4)
local v5 = env.LuaServer.MockGetMessage(key(2, 1))
check('new round: a fresh Mob of bots', select(2, counts(v5)) == 99, select(2, counts(v5)))
env.NATIVE_DISABLE_BOTS = true
local off = env.LuaServer.MockGetMessage(key(3, 1))
check('launcher bots option off: no bots', off == nil, tostring(off))
env.NATIVE_DISABLE_BOTS = nil; mode = 'EP'
local ep = env.LuaServer.MockGetMessage(key(4, 1))
check('Extended Play: untouched', ep == nil, tostring(ep))
print(env.logs[1]); print(env.logs[2])
print(fails == 0 and 'all passed' or (fails .. ' failed')); os.exit(fails == 0 and 0 or 1)
