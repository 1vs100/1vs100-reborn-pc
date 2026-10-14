-- Native fixture only: provide another local EP session after final results.
-- The original intermission, player cleanup, AdvanceProgram and timing remain.
-- UTC ISO-8601 <-> seconds since 1970 without the os library (civil-date
-- arithmetic, valid for the Gregorian calendar). Accepts ...Z with optional ms.
function NativeIsoToSeconds(text)
    if type(text)~='string' then return nil end
    local y,mo,d,h,mi,se=string.match(text,'^(%d%d%d%d)%-(%d%d)%-(%d%d)T(%d%d):(%d%d):(%d%d)')
    if not y then return nil end
    y,mo,d=tonumber(y),tonumber(mo),tonumber(d)
    if mo<=2 then y=y-1 end
    local era=math.floor(y/400)
    local yoe=y-era*400
    local mp=(mo>2) and (mo-3) or (mo+9)
    local doy=math.floor((153*mp+2)/5)+d-1
    local doe=yoe*365+math.floor(yoe/4)-math.floor(yoe/100)+doy
    local days=era*146097+doe-719468
    return days*86400+tonumber(h)*3600+tonumber(mi)*60+tonumber(se)
end
function NativeSecondsToIso(seconds)
    seconds=math.floor(seconds)
    local days=math.floor(seconds/86400)
    local rem=seconds-days*86400
    local z=days+719468
    local era=math.floor(z/146097)
    local doe=z-era*146097
    local yoe=math.floor((doe-math.floor(doe/1460)+math.floor(doe/36524)-math.floor(doe/146096))/365)
    local y=yoe+era*400
    local doy=doe-(365*yoe+math.floor(yoe/4)-math.floor(yoe/100))
    local mp=math.floor((5*doy+2)/153)
    local d=doy-math.floor((153*mp+2)/5)+1
    local m=(mp<10) and (mp+3) or (mp-9)
    if m<=2 then y=y+1 end
    return string.format('%04d-%02d-%02dT%02d:%02d:%02d.000Z',y,m,d,math.floor(rem/3600),math.floor(rem%3600/60),rem%60)
end
local originalIntermissionTask=Intermission_Task
function NativePrepareNextOfflineSession()
    if not (CONTENT_MOCK_SERVER and CurrentProgram and isExtendedPlayGame() and
        CurrentProgram.PID()=='offline-development-001' and not NextProgram) then return false end
    -- Online rooms need host-coordinated rollover, not independent wall clocks.
    if type(NativeRoomTransport)=='function' and NativeRoomTransport('mode','')~='' then return false end
    -- The title's Lua has no os library. Derive "now" from this session's own
    -- ISO StartTime plus the game clock (GetAbsoluteTime = seconds since start).
    local old=CurrentProgram
    local baseSeconds=NativeIsoToSeconds(old.self.session and old.self.session.StartTime)
    assert(baseSeconds,'Local session rollover requires the current session StartTime')
    local epoch=baseSeconds+math.floor(math.max(0,GetAbsoluteTime()))
    local nowText=NativeSecondsToIso(epoch)
    local startText=NativeSecondsToIso(epoch+60)
    local endText=NativeSecondsToIso(epoch+3660)
    local session=deepcopy(old.self.session)
    session.StartTime=startText; session.EndTime=endText; session.Duration=60
    local grid={SessionId=old.PID(),EpisodeId=old.EpisodeID(),Offers=deepcopy(old.self.offers),Session=session}
    NextProgram=GridInformationToProgram(grid,'Native offline next session')
    NextProgram.episodeLocale=old.episodeLocale
    NextProgram.episodeCountry=old.episodeCountry
    NextProgram.availableLanguages=old.availableLanguages
    -- This match has ended; do not wait for the unused schedule remainder.
    old.self.duration=math.max(0,GetAbsoluteTime())/60
    old.self.endTime=nowText
    g_countdownToTime=nil
    local prefix='v1.Session.MGS.1vs100.'..old.PID()..'.'
    for key in pairs(Lua_HVStore or {}) do
        if type(key)=='string' and string.sub(key,1,#prefix)==prefix then Lua_HVStore[key]=nil end
    end
    local bots=GameData.NexiaOfflineBots
    if bots then
        bots.questions={}
        for _,profile in ipairs(bots.profiles) do profile.finalScore=0; profile.streak=0 end
    end
    GameData.NativeOfflineSessionGeneration=(GameData.NativeOfflineSessionGeneration or 0)+1
    print('NATIVE offline session rollover generation '..GameData.NativeOfflineSessionGeneration)
    return true
end
Intermission_Task=function(...)
    NativePrepareNextOfflineSession()
    return originalIntermissionTask(...)
end
