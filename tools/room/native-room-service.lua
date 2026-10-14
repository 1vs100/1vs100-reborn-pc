-- Development adapter. Install explicitly after mock services are initialized.
-- Bounded data codec: never evaluates Lua received from the network.
NativeRoom = {replies={}, nextID=0, requests=0}
function NativeRoom.Encode(value)
    local active={}
    local function encode(v,depth)
        assert(depth<24,'Room nesting limit')
        local kind=type(v)
        if kind=='nil' then return 'z' end
        if kind=='boolean' then return v and 'y' or 'x' end
        if kind=='number' then
            assert(v==v and v~=math.huge and v~=-math.huge,'Invalid room number')
            local s=string.format('%.17g',v); return 'n'..#s..':'..s
        end
        if kind=='string' then return 's'..#v..':'..v end
        assert(kind=='table' and not active[v],'Unsupported/cyclic room data')
        active[v]=true
        local parts,count={},0
        for key,item in pairs(v) do
            assert(type(key)=='string' or type(key)=='number','Invalid room key')
            count=count+1; assert(count<=2048,'Room table limit')
            parts[#parts+1]=encode(key,depth+1)..encode(item,depth+1)
        end
        active[v]=nil
        return 't'..count..':'..table.concat(parts)
    end
    local encoded=encode(value,0)
    assert(#encoded<=65536,'Room packet limit')
    return encoded
end
function NativeRoom.Decode(data)
    assert(type(data)=='string' and #data<=65536,'Room packet limit')
    local cursor=1
    local function count()
        local finish=string.find(data,':',cursor,true)
        assert(finish and finish-cursor<=6,'Invalid room length')
        local digits=string.sub(data,cursor,finish-1)
        assert(string.match(digits,'^%d+$'),'Invalid room length')
        cursor=finish+1; return tonumber(digits)
    end
    local function decode(depth)
        assert(depth<24 and cursor<=#data,'Invalid room nesting')
        local tag=string.sub(data,cursor,cursor); cursor=cursor+1
        if tag=='z' then return nil end
        if tag=='y' then return true end
        if tag=='x' then return false end
        if tag=='s' or tag=='n' then
            local size=count(); assert(cursor+size-1<=#data,'Truncated room data')
            local s=string.sub(data,cursor,cursor+size-1); cursor=cursor+size
            if tag=='s' then return s end
            local n=tonumber(s); assert(n and n==n and n~=math.huge and n~=-math.huge,'Invalid room number'); return n
        end
        assert(tag=='t','Invalid room tag')
        local size=count(); assert(size<=2048,'Room table limit')
        local t={}
        for i=1,size do
            local key=decode(depth+1)
            assert(type(key)=='string' or type(key)=='number','Invalid room key')
            assert(t[key]==nil,'Duplicate room key')
            local item=decode(depth+1); assert(item~=nil,'Nil room table value'); t[key]=item
        end
        return t
    end
    local value=decode(0); assert(cursor==#data+1,'Trailing room data'); return value
end
function NativeRoom.Pump()
    if not NativeRoom.installed then return end
    for i=1,32 do
        local packet=NativeRoomTransport('poll',''); if packet=='' then break end
        local ok,message=pcall(NativeRoom.Decode,packet)
        if ok and type(message)=='table' then
            if NativeRoom.host then
                if type(message.id)=='number' and message.id%1==0 and message.id>0 and message.id<=100000 then
                    local response=NativeRoom.completed[message.id]
                    if not response then
                        local success,value=pcall(function()
                            if message.op=='get' then return NativeRoom.localGet(message.key) end
                            if message.op=='state-get' then return NativeRoom.states[message.key] end
                            if message.op=='post' then
                                local action=message.value and message.value.Actions and message.value.Actions[1]
                                assert(action and (action.Action=='Sum' or string.sub(action.Action or '',1,3)=='Top'),'Client operation not allowed')
                                return NativeRoom.localPost(message.value)
                            end
                            error('Client operation not allowed')
                        end)
                        response={id=message.id,ok=success,value=success and value or nil,error=not success and tostring(value) or nil}
                        NativeRoom.completed[message.id]=response
                        NativeRoom.requests=NativeRoom.requests+1
                    end
                    NativeRoomTransport('send',NativeRoom.Encode(response))
                end
            elseif type(message.id)=='number' and NativeRoom.waiting[message.id] then
                NativeRoom.replies[message.id]=message
            end
        end
    end
end
function NativeRoom.Request(op,key,value)
    NativeRoom.nextID=NativeRoom.nextID+1
    local id=NativeRoom.nextID
    NativeRoom.waiting[id]=true
    local sent=NativeRoomTransport('send',NativeRoom.Encode({id=id,op=op,key=key,value=value}))
    if sent~='queued' then NativeRoom.waiting[id]=nil; error(sent) end
    local deadline=GetAbsoluteTime()+5
    while not NativeRoom.replies[id] and GetAbsoluteTime()<deadline do NativeRoom.Pump(); Sleep(0.01) end
    local response=NativeRoom.replies[id]
    NativeRoom.replies[id]=nil; NativeRoom.waiting[id]=nil
    assert(response,'Room request timed out')
    assert(response.ok,response.error or 'Room request rejected')
    return response.value
end
function NativeRoom.Install()
    if NativeRoom.installed or type(NativeRoomTransport)~='function' then return false end
    local mode=NativeRoomTransport('mode','')
    if mode=='' then return false end
    NativeRoom.host=mode=='local-host' or mode=='steam-host'
    NativeRoom.localGet=LuaServer.MockGetMessage
    NativeRoom.localPost=LuaServer.MockPostMessage
    NativeRoom.states={}; NativeRoom.completed={}; NativeRoom.waiting={}
    LuaServer.MockGetMessage=function(key)
        NativeRoom.Pump()
        if NativeRoom.host then return NativeRoom.localGet(key) end
        return NativeRoom.Request('get',key)
    end
    LuaServer.MockPostMessage=function(value)
        NativeRoom.Pump()
        if NativeRoom.host then return NativeRoom.localPost(value) end
        return NativeRoom.Request('post',nil,value)
    end
    NativeRoom.installed=true
    return true
end
-- Explicit state API for wiring XLP_GameService after the room is ready.
function NativeRoom.GetState(key)
    NativeRoom.Pump()
    if NativeRoom.host then return NativeRoom.states[key] end
    return NativeRoom.Request('state-get',key)
end
function NativeRoom.SetState(key,value)
    assert(NativeRoom.host,'Only room host publishes GameState')
    NativeRoom.Pump(); NativeRoom.states[key]=value
end
