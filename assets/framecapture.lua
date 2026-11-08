-- SplashEdit frame capture, loaded into pcsx-redux by Play (-dofile).
--
-- Every 10 vsyncs it looks for capture.flag in the PCdrv directory (the one
-- Play passes as -pcdrvbase). When the editor writes it, this records one
-- frame with redux's GPU dump recorder into capture.gpd.part, renames it to
-- capture.gpd once complete and deletes the flag. When it cannot, it writes
-- the reason to capture.err instead.
--
-- The recorder arms on startDump() and begins at the first vsync where the
-- GPU is between commands, then writes each vsync's traffic at that vsync
-- (redux runs the recorder before Lua listeners). A game drawing at 20 or 30
-- fps leaves most vsync intervals empty, so recording stops at the end of the
-- first interval that carried drawing (more than kFrameBytes), or after
-- kMaxVsyncs intervals without any.

local function base()
    -- One of 2 linked consoles: capture files in its own directory
    -- (capturedir.lua, run first), as both share the PCdrv one.
    if type(SplashEditCaptureDir) == 'string' and SplashEditCaptureDir ~= '' then return SplashEditCaptureDir end
    local ok, dir = pcall(function() return PCSX.settings.emulator.Debug.PCdrvBase end)
    if not ok or type(dir) ~= 'string' or dir == '' then return nil end
    return dir
end

local function at(name)
    local dir = base()
    if not dir then return nil end
    local last = dir:sub(-1)
    if last ~= '/' and last ~= '\\' then dir = dir .. '/' end
    return dir .. name
end

local function exists(path)
    local f = io.open(path, 'rb')
    if f then
        f:close()
        return true
    end
    return false
end

local function fail(message)
    os.remove(at('capture.gpd.part'))
    local f = io.open(at('capture.err'), 'wb')
    if f then
        f:write(message)
        f:close()
    end
    os.remove(at('capture.flag'))
    print('framecapture: ' .. message)
end

local kFrameBytes, kMaxVsyncs = 256, 120
local vsyncs, armedAt, state, file, lastPos = 0, 0, 'idle', nil, nil

-- psxsplash capture tags (psxsplash src/capturetag.hh): with g_captureTag's
-- `enabled` set, each mesh triangle's packet carries 2 GP0(00h) words naming
-- its object, triangle and ordering-table slot, which the Frame tab shows.
-- g_captureTag (4-aligned, found by its magic, no symbol file):
--   +0 "PSXSPTAG"  +8 version (1)  +12 enabled  +16 tagged  +20 skipped
-- `tagged` is written at the end of every frame built with tagging on: a
-- sentinel there, overwritten twice, means two tagged frames were built, so
-- no frame built before the switch can still be on its way to the GPU.
-- Without the struct (another engine, an older psxsplash) or when the tagged
-- frames do not come, the capture goes ahead untagged.
local kTagSentinel, kTagWaitVsyncs = 0xffffffff, 90
local tagBase, tagScanned, tagBuilt, tagWaitFrom = nil, false, 0, 0

local ffi = nil
local function ram32()
    if not ffi then
        local ok, m = pcall(require, 'ffi')
        if not ok then return nil end
        ffi = m
    end
    local ok, p = pcall(function() return ffi.cast('uint32_t*', PCSX.getMemPtr()) end)
    if ok then return p end
    return nil
end

local function tagStruct()
    local r = ram32()
    if not r then return nil, nil end
    if tagBase and r[tagBase / 4] == 0x53585350 and r[tagBase / 4 + 1] == 0x47415450 and r[tagBase / 4 + 2] == 1 then
        return r, tagBase
    end
    if tagScanned then return nil, nil end
    tagScanned = true
    -- 2 MB of main RAM, word aligned: "PSXS" "PTAG" little-endian, version 1.
    local found = {}
    for i = 0, (2 * 1024 * 1024) / 4 - 4 do
        if r[i] == 0x53585350 and r[i + 1] == 0x47415450 and r[i + 2] == 1 then found[#found + 1] = i * 4 end
    end
    if #found ~= 1 then
        print(string.format('framecapture: capture tags off (%d g_captureTag matches)', #found))
        return nil, nil
    end
    tagBase = found[1]
    print(string.format('framecapture: g_captureTag at 0x%08x', 0x80000000 + tagBase))
    return r, tagBase
end

local function tagsOff()
    local r, b = tagStruct()
    if r then r[b / 4 + 3] = 0 end
end

local function startDump()
    local ok, err = pcall(function()
        file = Support.File.open(at('capture.gpd.part'), 'TRUNCATE')
        PCSX.GPU.startDump(file)
    end)
    if not ok then
        tagsOff()
        fail('cannot start the GPU dump: ' .. tostring(err))
        state = 'idle'
        return
    end
    state, armedAt = 'armed', vsyncs
end

local function finish()
    PCSX.GPU.stopDump()
    tagsOff()
    state, file, lastPos = 'idle', nil, nil
    os.remove(at('capture.gpd'))
    local ok, err = os.rename(at('capture.gpd.part'), at('capture.gpd'))
    if not ok then
        fail('cannot rename the capture: ' .. tostring(err))
        return
    end
    os.remove(at('capture.flag'))
    print('framecapture: wrote ' .. at('capture.gpd'))
end

SplashEditFrameCapture = PCSX.Events.createEventListener('GPU::Vsync', function()
    vsyncs = vsyncs + 1
    if state == 'idle' then
        if vsyncs % 10 ~= 0 or not base() or not exists(at('capture.flag')) then return end
        os.remove(at('capture.gpd'))
        os.remove(at('capture.err'))
        if type(PCSX.GPU.startDump) ~= 'function' then
            fail('this pcsx-redux has no GPU dump recorder; update it to capture frames')
            return
        end
        local r, b = tagStruct()
        if r then
            r[b / 4 + 4] = kTagSentinel
            r[b / 4 + 3] = 1
            state, tagBuilt, tagWaitFrom = 'tagwait', 0, vsyncs
            return
        end
        startDump()
    elseif state == 'tagwait' then
        local r, b = tagStruct()
        if r and r[b / 4 + 4] ~= kTagSentinel then
            tagBuilt = tagBuilt + 1
            r[b / 4 + 4] = kTagSentinel
        end
        if not r or tagBuilt >= 2 then
            startDump()
        elseif vsyncs - tagWaitFrom > kTagWaitVsyncs then
            print('framecapture: no tagged frame came; capturing untagged')
            tagsOff()
            startDump()
        end
    elseif PCSX.GPU.isDumping() then
        -- The write position after this vsync's flush: the header and VRAM at
        -- the start, then one interval's traffic per vsync.
        local pos = tonumber(file:wTell())
        if lastPos and PCSX.GPU.getDumpFrames() >= 1 and
            (pos - lastPos > kFrameBytes or PCSX.GPU.getDumpFrames() >= kMaxVsyncs) then
            finish()
            return
        end
        lastPos = pos
    elseif vsyncs - armedAt > 600 then
        PCSX.GPU.stopDump()
        tagsOff()
        state = 'idle'
        fail('the GPU never went idle at a vsync; no frame was captured')
    end
end)
