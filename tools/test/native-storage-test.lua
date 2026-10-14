-- Round-trip test for NativeSaveUserStorage / NativeLoadUserStorage (Lua 5.3+):
--   lua native-storage-test.lua <NativeRoom.lua>
local files = {}
local env = setmetatable({}, { __index = _G })
env.NativeStorage = function(op, name, data)
  if op == 'write' then files[name] = data; return 'ok' end
  if op == 'read' then return files[name] or '' end
  return 'error'
end
local f = assert(io.open(arg[1])); local src = f:read('a'); f:close()
assert(load(src, '@NativeRoom.lua', 't', env))()
local stats = { SeasonCount = { TheOne = 0, Mob = 3, Prizes = 0 }, LifetimeScore = 5672, LifetimeQuestions = 6,
                SuperStreak = 4, PrivacySetting = { optionNum = 1, bValid = true }, Name = 'HostPlayer', Flag = false,
                Engine = io.stdout }
local ok = env.NativeSaveUserStorage('B13EBABEBABEBABE', stats)
local back = env.NativeLoadUserStorage('B13EBABEBABEBABE')
local pass = ok and back and back.LifetimeScore == 5672 and back.SeasonCount.Mob == 3 and back.SuperStreak == 4 and
             back.PrivacySetting.optionNum == 1 and back.Flag == false and back.Engine == nil
print((pass and 'PASS' or 'FAIL') .. ' stats round trip (userdata dropped)')
print((env.NativeLoadUserStorage('0009000012345678') == nil and 'PASS' or 'FAIL') .. ' unknown player starts fresh')
print((env.NativeSaveUserStorage('../evil', stats) == false and 'PASS' or 'FAIL') .. ' unsafe file names refused')
files['player-BAD'] = 'garbage'
print((env.NativeLoadUserStorage('BAD') == nil and 'PASS' or 'FAIL') .. ' corrupt file starts fresh')
