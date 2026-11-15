--[[
@title G12 film clock probe
@chdk_version 1.6
#shots=1 "Shots to take (0 = idle only)" [0 3]
]]
-- Read-only probe of Canon's CPU clock control on G12 1.00G.
-- Static ROM reading (task_ClockSave, 0xff816124 and neighbours):
--   0xC0400048 selects the CPU clock through the table at 0xffbb7bc4:
--   index 0=144, 1=72, 2=48, 3=36, 4=96, 5=24 (MHz-like values).
--   0xC0400008 bit 1 enables Canon's dynamic clock scaling.
--   0xC0400044 is 5 while the idle task saves power (WFI), 1 when awake.
--   RAM 0x19a4: +0 startup hold, +4 processing hold (PrcssFil.c, USB),
--   +8 live-view hold, +0xc saved clock index, +0x10 index before a boost.
-- Nothing is written to camera registers. The log goes to A/CHDK/LOGS/FILMCLK.TXT.
-- Run in C2 with the film route enabled; the script presses the shutter itself
-- because a manual shutter press would stop the script.
local mhz={[0]=144,72,48,36,96,24}
local f=assert(io.open("A/CHDK/LOGS/FILMCLK.TXT","a"))
local function sample(tag)
 local idx=peek(0xC0400048)
 local line=string.format("%s t=%d c08=%d c44=%d c48=%d (%s) c4c=%d hold0=%d hold4=%d hold8=%d saved=%d",
  tag,get_tick_count(),peek(0xC0400008),peek(0xC0400044),idx,tostring(mhz[idx] or "?"),
  peek(0xC040004C),peek(0x19a4),peek(0x19a8),peek(0x19ac),peek(0x19b0))
 f:write(line,"\n");f:flush()
 return line
end
local b=get_buildinfo()
f:write(string.format("FILMCLK %s %s %s shots=%d\n",tostring(b.platform),tostring(b.platsub),tostring(b.build_number),shots))
for i=1,10 do print(sample("idle"));sleep(200) end
for n=1,shots do
 press("shoot_half");sleep(1500)
 press("shoot_full");sleep(200);release("shoot_full");release("shoot_half")
 -- Sample through the film processing; it takes about 35 s per shot.
 local start=get_tick_count()
 while get_tick_count()-start<60000 do
  local line=sample("shot"..n)
  if (get_tick_count()-start)%5000<300 then print(line) end
  sleep(250)
 end
end
for i=1,5 do print(sample("after"));sleep(200) end
f:close()
print("Log: A/CHDK/LOGS/FILMCLK.TXT")
