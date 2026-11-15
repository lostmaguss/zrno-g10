--[[
@title G12 C2 film diagnostic
@chdk_version 1.6
]]
-- Read-only camera checks. Writes a log only; no property changes or captures.
local f=assert(io.open("A/CHDK/LOGS/FILMDIAG.TXT","a"))
local function log(s) print(s);f:write(s,"\n");f:flush() end
local b=get_buildinfo()
log("Zrno diagnostic "..tostring(b.platform).." "..tostring(b.platsub).." "..tostring(b.build_number))
log("Rotate P, C1, C2. SET logs memory. MENU ends.")
local old
repeat
 local dial=get_prop(50)
 if dial~=old then
  log("dial50="..dial.." mode49="..get_prop(49).." expected C2=8241")
  old=dial
 end
 wait_click(200)
 if is_key("set") then
  for _,heap in ipairs({"combined","system","aram","exmem"}) do
   local m=get_meminfo(heap)
   if m then log(heap.." free="..tostring(m.free_size).." largest="..tostring(m.free_block_max_size)) end
  end
 end
until is_key("menu")
f:close()
