--[[
@title Film look (Magus)
@chdk_version 1.6
@param a Look
@default a 0
@values a Canon Warm200 Portra400 Velvia50 Kodachrome64 IlfordHP5 Ektar100 Portra800 Fuji400H Provia100F Astia100F Superia200 Polaroid669 Tri-X400 Acros100 BleachBypass TealOrange
]]
-- Loads a Zrno film look into the G10's colour table ("My Colors Off").
-- The live view and every photo use it until the camera is switched off,
-- or until you run this script again with "Canon".
-- Only RAM is changed. Looks: A/CHDK/MAGUS/*.G10 (from tools/g10_table.py).
-- Needs: shooting mode, My Colors Off, CHDK setting "Enable Lua Native Calls".

set_yield(100000, 50)

TABLE_PTR = 0x75c8
RELOAD    = -8136188      -- 0xff83da04, My Colors (re)load, argument = mode
NODES     = 729
DIR       = "A/CHDK/MAGUS/"
CANON     = DIR .. "CANON.BIN"   -- Canon's own table, saved the first time
STEMS     = {"GOLD17","PORTRA","VELVIA","KCHROME","HP5","EKTAR","P800","F400H","PROVIA","ASTIA","SUPERIA","POLA669","TRIX","ACROS","BLEACH","TEALORG"}

ROMCHECK = {{-8136188,-382910408},{-8136184,-450935616},{-8136180,-509587456},{-8136176,-443285500},{-8136172,-481296383},{-8136168,167772175},{-8136164,-442555384},{-8136160,-450947876},{-8136156,-443285496},{-8136152,-476037577},{-8136148,-335566082},{-8136144,-509603827},{-8136140,-509607931},{-8136136,-352168834},{-8136132,-352128235},{-8136128,-352128198},{-8136124,-443285496},{-8136120,-335566349},{-8136116,-485490687},{-8136112,329261021},{-8136108,307170898},{-8136104,469726683},{-8136100,-390234056},{-7525320,-382910448},{-7525316,-509591551},{-7525312,-509603840},{-7525308,-450953164},{-7525304,-450944992},{-7525300,-443547648},{-7525296,-481230835},{-7525292,-1869614847},{-7362920,-382910352},{-7362916,-450935728},{-7362912,-509587456},{-7362908,-509603840},{-7362904,-442564372},{-7362900,-509599740},{-7362896,-351786319},{-7362892,-450952048},{-7362888,-509599740},{-7362884,-443547640},{-7362880,-509603835},{-7362876,-390250384},{-7362872,-368563541},{-7362768,-382910408},{-7362764,-450931480},{-7362760,-450935532},{-7362756,-442560424},{-7362752,-443285496},{-7362748,-509599739},{-7362744,-351786357},{-7362740,-443723776}}

function fail(msg)
  print("STOP: " .. msg)
end

local function identity_at(get)
  for k = 0, 8 do
    if get(91 * k) ~= bitshl(80 * k, 20) then return false end
  end
  return bitand(get(1), 1023) ~= 0
end

local function byte4(v)
  return string.char(bitand(v, 255), bitand(bitshru(v, 8), 255),
                     bitand(bitshru(v, 16), 255), bitand(bitshru(v, 24), 255))
end

local function read_table(path)
  local f = io.open(path, "rb")
  if not f then return nil end
  local data = f:read("*a")
  f:close()
  if not data or string.len(data) ~= NODES * 4 then return nil end
  local t = {}
  for i = 0, NODES - 1 do
    local b1, b2, b3, b4 = string.byte(data, 4 * i + 1, 4 * i + 4)
    t[i] = b1 + b2 * 256 + b3 * 65536 + bitshl(b4, 24)
  end
  return t
end

local function main()
  for i = 1, #ROMCHECK do
    if peek(ROMCHECK[i][1]) ~= ROMCHECK[i][2] then
      return fail(string.format("ROM differs at %08x", bitand(ROMCHECK[i][1], -1)))
    end
  end
  if not get_mode() then return fail("switch to shooting mode first") end
  if get_prop(184) ~= 0 then return fail("set My Colors to Off") end
  local base = peek(TABLE_PTR)
  if base < 0x100000 or base > 0x2000000 or bitand(base, 3) ~= 0 then
    return fail(string.format("table pointer %08x looks wrong", base))
  end

  -- Canon's table: saved once, then always restored from the file.
  local canon = read_table(CANON)
  if canon then
    if not identity_at(function(i) return canon[i] end) then return fail("CANON.BIN is damaged") end
  else
    if not identity_at(function(i) return peek(base + 4 * i) end) then
      return fail("table already changed and no CANON.BIN: switch the camera off and on")
    end
    local f = io.open(CANON, "wb")
    if not f then return fail("cannot write " .. CANON) end
    local parts = {}
    for i = 0, NODES - 1 do parts[i + 1] = byte4(peek(base + 4 * i)) end
    f:write(table.concat(parts))
    f:close()
    canon = read_table(CANON)
    if not canon then return fail("CANON.BIN did not save") end
  end

  local t, name
  if a == 0 then
    t, name = canon, "Canon"
  else
    name = STEMS[a]
    t = read_table(DIR .. name .. ".G10")
    if not t then return fail("missing " .. DIR .. name .. ".G10") end
  end
  for i = 0, NODES - 1 do poke(base + 4 * i, t[i]) end

  local ok, err = pcall(call_func_ptr, RELOAD, 0)
  if not ok then
    print("reload failed: " .. tostring(err))
    return fail("enable Lua Native Calls in CHDK settings")
  end
  print("Look: " .. name)
end

main()
