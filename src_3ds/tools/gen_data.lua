-- gen_data.lua: generates C data tables from the original Lua prototypes.
-- Run with LuaJIT 2.0 (the version the original game ships) so that pairs()
-- iteration order matches the original:
--   luajit gen_data.lua <path to src_lua> <output dir>
local SRC = arg[1] or '../../src_lua'
local OUT = arg[2] or '../game'

-- ---------------------------------------------------------------- stubs
Object = {}
Object.__index = Object
function Object:init() end
function Object:extend()
  local cls = {}
  for k, v in pairs(self) do if k:find("__") == 1 then cls[k] = v end end
  cls.__index = cls
  cls.super = self
  setmetatable(cls, self)
  return cls
end
function Object:__call(...) local o = setmetatable({}, self); o:init(...); return o end

local hexmarker = {}
function HEX(h) local t = {__hex = h}; return t end
function localize(k) return '@loc:'..tostring(k) end
love = { filesystem = { getInfo = function() return nil end, createDirectory = function() end, append = function() end },
         system = { getOS = function() return 'Linux' end } }
function convert_save_to_meta() end
function get_compressed() return nil end
function STR_UNPACK(s) return assert(loadstring(s))() end
function get_starting_params() return {} end

dofile(SRC..'/game.lua')
G = setmetatable({}, Game)
G.SETTINGS = {profile = 1}
G.save_progress = function() end
Game.init_item_prototypes(G)

-- challenges
dofile(SRC..'/challenges.lua')

-- ---------------------------------------------------------------- output helpers
local out = {}
local function w(...) for _, v in ipairs({...}) do out[#out+1] = tostring(v) end end
local function q(s)
  if s == nil then return 'NULL' end
  s = tostring(s)
  if s:sub(1,5) == '@loc:' then return '"'..s..'"' end
  return '"'..s:gsub('\\', '\\\\'):gsub('"', '\\"'):gsub('\n', '\\n')..'"'
end
local function num(v) if v == nil then return '0' end return string.format('%.17g', v) end
local function b(v) return v and 'true' or 'false' end
local function pos(p) if not p then return '{0, 0, 0}' end return string.format('{%s, %s, %s}', num(p.x), num(p.y), num(p.v or 0)) end

local function extra_init(e, prefix)
  prefix = prefix or '.extra'
  if e == nil then return '' end
  if type(e) == 'number' then return prefix..' = {.kind = EX_NUM, .num = '..num(e)..'}, ' end
  if type(e) == 'string' then return prefix..' = {.kind = EX_STR, .str = '..q(e)..'}, ' end
  local parts = {'.kind = EX_TABLE'}
  local keys = {}
  for k in pairs(e) do keys[#keys+1] = k end
  table.sort(keys)
  for _, k in ipairs(keys) do
    local v = e[k]
    if type(v) == 'number' then parts[#parts+1] = '.'..k..' = '..num(v)
    elseif type(v) == 'string' then parts[#parts+1] = '.'..k..' = '..q(v)
    elseif type(v) == 'boolean' then parts[#parts+1] = '.'..k..' = '..num(v and 1 or 0)
    else error('bad extra field '..k) end
  end
  return prefix..' = {'..table.concat(parts, ', ')..'}, '
end

local CONFIG_NUM = {mult=1,h_mult=1,h_x_mult=1,h_dollars=1,p_dollars=1,t_mult=1,t_chips=1,h_size=1,d_size=1,bonus=1,
  max_highlighted=1,min_highlighted=1,planets=1,tarots=1,choose=1,discards=1,hands=1,dollars=1,extra_hand_bonus=1,
  extra_discard_bonus=1,joker_slot=1,consumable_slot=1,spectral_rate=1,hand_size=1,ante_scaling=1,extra_disp=1,
  reroll_discount=1, edition_count=1}
local CONFIG_STR = {type=1, mod_conv=1, suit_conv=1, hand_type=1, voucher=1, edition=1}
local CONFIG_BOOL = {softlock=1, remove_card=1, no_interest=1, remove_faces=1, randomize_rank_suit=1}

local function config_init(c)
  if not c then return '{0}' end
  local parts = {}
  local keys = {}
  for k in pairs(c) do keys[#keys+1] = k end
  table.sort(keys)
  for _, k in ipairs(keys) do
    local v = c[k]
    if k == 'extra' then parts[#parts+1] = extra_init(v):gsub(', $', '')
    elseif k == 'Xmult' then parts[#parts+1] = '.Xmult = '..num(v)..', .has_Xmult = true'
    elseif k == 'bonus' then parts[#parts+1] = '.bonus = '..num(v)..', .has_bonus = true'
    elseif CONFIG_NUM[k] then parts[#parts+1] = '.'..k..' = '..num(v)
    elseif CONFIG_STR[k] then parts[#parts+1] = '.'..k..' = '..q(v)
    elseif CONFIG_BOOL[k] then parts[#parts+1] = '.'..k..' = '..b(v)
    elseif k == 'consumables' or k == 'vouchers' then
      local l = {}
      for _, x in ipairs(v) do l[#l+1] = q(x) end
      parts[#parts+1] = '.'..k..' = {'..table.concat(l, ', ')..'}, .n_'..k..' = '..#v
    else error('unknown config key '..k) end
  end
  return '{'..table.concat(parts, ', ')..'}'
end

local function unlock_init(u)
  if not u then return '{0}' end
  local parts = {'.present = true'}
  for k, v in pairs(u) do
    if k == 'type' then parts[#parts+1] = '.type = '..q(v)
    elseif k == 'extra' then
      if type(v) == 'number' then parts[#parts+1] = '.extra_kind = EX_NUM, .extra_num = '..num(v)
      elseif type(v) == 'string' then parts[#parts+1] = '.extra_kind = EX_STR, .extra_str = '..q(v)
      elseif type(v) == 'table' then
        parts[#parts+1] = '.extra_kind = EX_TABLE'
        for kk, vv in pairs(v) do
          if kk == 'count' then parts[#parts+1] = '.count = '..num(vv)
          elseif kk == 'polychrome' then parts[#parts+1] = '.polychrome = '..b(vv)
          else parts[#parts+1] = '.'..kk..' = '..q(vv) end
        end
      end
    elseif k == 'hidden' then parts[#parts+1] = '.hidden = '..b(v)
    elseif k == 'deck' then parts[#parts+1] = '.deck = '..q(v)
    else parts[#parts+1] = '.'..k..' = '..num(v) end
  end
  return '{'..table.concat(parts, ', ')..'}'
end

local function center_init(key, v)
  local p = {}
  p[#p+1] = '.key = '..q(key)
  if v.order then p[#p+1] = '.order = '..num(v.order)..', .has_order = true' end
  if v.unlocked ~= nil then p[#p+1] = '.unlocked = '..b(v.unlocked)..', .unlocked_set = true' end
  if v.discovered then p[#p+1] = '.discovered = true' end
  if v.alerted then p[#p+1] = '.alerted = true' end
  if v.start_alerted then p[#p+1] = '.start_alerted = true' end
  if v.blueprint_compat then p[#p+1] = '.blueprint_compat = true' end
  if v.perishable_compat then p[#p+1] = '.perishable_compat = true' end
  if v.eternal_compat then p[#p+1] = '.eternal_compat = true' end
  if v.rarity then p[#p+1] = '.rarity = '..num(v.rarity) end
  if v.cost then p[#p+1] = '.cost = '..num(v.cost)..', .has_cost = true' end
  if v.name then p[#p+1] = '.name = '..q(v.name) end
  p[#p+1] = '.pos = '..pos(v.pos)
  if v.soul_pos then p[#p+1] = '.soul_pos = '..pos(v.soul_pos)..', .has_soul_pos = true' end
  if v.set then p[#p+1] = '.set = '..q(v.set) end
  if v.effect then p[#p+1] = '.effect = '..q(v.effect) end
  if v.cost_mult then p[#p+1] = '.cost_mult = '..num(v.cost_mult) end
  p[#p+1] = '.config = '..config_init(v.config)
  if v.unlock_condition then p[#p+1] = '.unlock_condition = '..unlock_init(v.unlock_condition) end
  if v.enhancement_gate then p[#p+1] = '.enhancement_gate = '..q(v.enhancement_gate) end
  if v.no_pool_flag then p[#p+1] = '.no_pool_flag = '..q(v.no_pool_flag) end
  if v.yes_pool_flag then p[#p+1] = '.yes_pool_flag = '..q(v.yes_pool_flag) end
  if v.consumeable then p[#p+1] = '.consumeable = true' end
  if v.freq then p[#p+1] = '.freq = '..num(v.freq) end
  if v.hidden then p[#p+1] = '.hidden = true' end
  if v.available then p[#p+1] = '.available = true' end
  if v.requires then
    local l = {}
    if type(v.requires) == 'table' then for _, x in ipairs(v.requires) do l[#l+1] = q(x) end else l[1] = q(v.requires) end
    p[#p+1] = '.requires = {'..table.concat(l, ', ')..'}, .n_requires = '..#l
  end
  if v.stake then p[#p+1] = '.stake = '..num(v.stake) end
  if v.atlas then p[#p+1] = '.atlas = '..q(v.atlas) end
  if v.kind then p[#p+1] = '.kind = '..q(v.kind) end
  if v.weight then p[#p+1] = '.weight = '..num(v.weight) end
  if v.max then p[#p+1] = '.max = '..num(v.max) end
  if v.line then p[#p+1] = '.line = '..q(v.line) end
  if v.label then p[#p+1] = '.label = '..q(v.label) end
  if v.omit then p[#p+1] = '.omit = true' end
  if v.demo then p[#p+1] = '.demo = true' end
  if v.wip then p[#p+1] = '.wip = true' end
  if v.skip_pool then p[#p+1] = '.skip_pool = true' end
  return '{'..table.concat(p, ', ')..'}'
end

w('/* GENERATED by tools/gen_data.lua from src_lua/game.lua and challenges.lua - do not edit */\n')
w('#include "game.h"\n\n')

-- centers in LuaJIT 2.0 pairs() order
local center_keys = {}
for k in pairs(G.P_CENTERS) do center_keys[#center_keys+1] = k end
w('const int DATA_N_CENTERS = ', #center_keys, ';\n')
w('const Center DATA_CENTERS[] = {\n')
for _, k in ipairs(center_keys) do w('    ', center_init(k, G.P_CENTERS[k]), ',\n') end
w('};\n\n')

for _, name in ipairs({'j_locked','v_locked','c_locked','j_undiscovered','t_undiscovered','p_undiscovered','s_undiscovered','v_undiscovered','booster_undiscovered'}) do
  w('const Center DATA_', name, ' = ', center_init(name, G[name]), ';\n')
end
w('\n')

-- playing cards
local card_keys = {}
for k in pairs(G.P_CARDS) do card_keys[#card_keys+1] = k end
w('const int DATA_N_CARDS = ', #card_keys, ';\n')
w('const PCard DATA_CARDS[] = {\n')
for _, k in ipairs(card_keys) do
  local v = G.P_CARDS[k]
  w('    {.key = ', q(k), ', .name = ', q(v.name), ', .value = ', q(v.value), ', .suit = ', q(v.suit), ', .pos = ', pos(v.pos), '},\n')
end
w('};\n\n')

-- blinds
local blind_keys = {}
for k in pairs(G.P_BLINDS) do blind_keys[#blind_keys+1] = k end
w('const int DATA_N_BLINDS = ', #blind_keys, ';\n')
w('const BlindProto DATA_BLINDS[] = {\n')
local function blind_init(k, v)
  local p = {'.key = '..q(k), '.name = '..q(v.name), '.order = '..num(v.order), '.dollars = '..num(v.dollars), '.mult = '..num(v.mult)}
  if v.vars and #v.vars > 0 then
    local l = {}
    for _, x in ipairs(v.vars) do l[#l+1] = q(x) end
    p[#p+1] = '.vars = {'..table.concat(l, ', ')..'}, .nvars = '..#v.vars
  end
  if v.debuff_text then p[#p+1] = '.debuff_text = '..q(v.debuff_text) end
  local d = {}
  for kk, vv in pairs(v.debuff or {}) do
    if type(vv) == 'number' then d[#d+1] = '.'..kk..' = '..num(vv) else d[#d+1] = '.'..kk..' = '..q(vv) end
  end
  p[#p+1] = '.debuff = {'..table.concat(d, ', ')..'}'
  p[#p+1] = '.pos = '..pos(v.pos)
  if v.boss then
    p[#p+1] = string.format('.has_boss = true, .boss = {%s, %s, %s}', num(v.boss.min), num(v.boss.max), b(v.boss.showdown))
  end
  return '{'..table.concat(p, ', ')..'}'
end
for _, k in ipairs(blind_keys) do w('    ', blind_init(k, G.P_BLINDS[k]), ',\n') end
w('};\n')
w('const char *DATA_BLIND_COLOURS[] = {\n')
for _, k in ipairs(blind_keys) do
  local v = G.P_BLINDS[k]
  w('    ', v.boss_colour and q(v.boss_colour.__hex) or 'NULL', ',\n')
end
w('};\n')
w('const BlindProto DATA_b_undiscovered = {.name = ', q(G.b_undiscovered.name), ', .debuff_text = ', q(G.b_undiscovered.debuff_text), ', .pos = ', pos(G.b_undiscovered.pos), '};\n\n')

-- tags
local tag_keys = {}
for k in pairs(G.P_TAGS) do tag_keys[#tag_keys+1] = k end
w('const int DATA_N_TAGS = ', #tag_keys, ';\n')
w('const TagProto DATA_TAGS[] = {\n')
local function tag_init(k, v)
  local p = {'.key = '..q(k), '.name = '..q(v.name), '.set = '..q(v.set), '.order = '..num(v.order)}
  if v.min_ante then p[#p+1] = '.min_ante = '..num(v.min_ante)..', .has_min_ante = true' end
  local c = {}
  for kk, vv in pairs(v.config or {}) do
    if type(vv) == 'number' then c[#c+1] = '.'..kk..' = '..num(vv) else c[#c+1] = '.'..kk..' = '..q(vv) end
  end
  p[#p+1] = '.config = {'..table.concat(c, ', ')..'}'
  if v.requires then p[#p+1] = '.requires = '..q(v.requires) end
  p[#p+1] = '.pos = '..pos(v.pos)
  return '{'..table.concat(p, ', ')..'}'
end
for _, k in ipairs(tag_keys) do w('    ', tag_init(k, G.P_TAGS[k]), ',\n') end
w('};\n')
w('const TagProto DATA_tag_undiscovered = {.name = ', q(G.tag_undiscovered.name), ', .order = 1, .config = {.type = ""}, .pos = ', pos(G.tag_undiscovered.pos), '};\n\n')

-- stakes
local stake_keys = {}
for k in pairs(G.P_STAKES) do stake_keys[#stake_keys+1] = k end
w('const int DATA_N_STAKES = ', #stake_keys, ';\n')
w('const StakeProto DATA_STAKES[] = {\n')
for _, k in ipairs(stake_keys) do
  local v = G.P_STAKES[k]
  w('    {.key = ', q(k), ', .name = ', q(v.name), ', .unlocked = ', b(v.unlocked), ', .order = ', num(v.order), ', .pos = ', pos(v.pos), ', .stake_level = ', num(v.stake_level), ', .set = ', q(v.set), '},\n')
end
w('};\n\n')

-- seals
local seal_keys = {}
for k in pairs(G.P_SEALS) do seal_keys[#seal_keys+1] = k end
w('const int DATA_N_SEALS = ', #seal_keys, ';\n')
w('const SealProto DATA_SEALS[] = {\n')
for _, k in ipairs(seal_keys) do
  local v = G.P_SEALS[k]
  w('    {.key = ', q(k), ', .order = ', num(v.order), ', .set = ', q(v.set), '},\n')
end
w('};\n\n')

-- iteration order of G.GAME.hands as created by init_game_object (used where the Lua iterates pairs(G.GAME.hands))
local game = Game.init_game_object(setmetatable({}, Game))
local hand_order = {}
for k in pairs(game.hands) do hand_order[#hand_order+1] = q(k) end
w('const char *DATA_HANDS_PAIRS_ORDER[] = {', table.concat(hand_order, ', '), '};\n')
local cp_order = {}
for k in pairs(game.cards_played) do cp_order[#cp_order+1] = q(k) end
w('const char *DATA_CARDS_PLAYED_PAIRS_ORDER[] = {', table.concat(cp_order, ', '), '};\n\n')

local f = assert(io.open(OUT..'/data_protos.c', 'w'))
f:write(table.concat(out))
f:close()

-- ---------------------------------------------------------------- challenges
out = {}
w('/* GENERATED by tools/gen_data.lua from src_lua/challenges.lua - do not edit */\n')
w('#include "game.h"\n\n')
local function rules(list, field)
  if not list or #list == 0 then return '' end
  local l = {}
  for _, r in ipairs(list) do
    local s = '{.id = '..q(r.id)
    if r.value ~= nil then
      if type(r.value) == 'string' then s = s..', .has_value = true, .value_is_str = true, .value_str = '..q(r.value)
      else s = s..', .has_value = true, .value_num = '..num(r.value) end
    end
    l[#l+1] = s..'}'
  end
  return '.'..field..' = {'..table.concat(l, ', ')..'}, .n'..field..' = '..#list..', '
end
local function cards(list, field)
  if not list or #list == 0 then return '' end
  local l = {}
  for _, c in ipairs(list) do
    local s = '{.id = '..q(c.id)
    if c.edition then s = s..', .edition = '..q(c.edition) end
    if c.eternal then s = s..', .eternal = true' end
    if c.pinned then s = s..', .pinned = true' end
    l[#l+1] = s..'}'
  end
  return '.'..field..' = {'..table.concat(l, ', ')..'}, .n'..field..' = '..#list..', '
end
local function bans(list, field)
  if not list or #list == 0 then return '' end
  local l = {}
  for _, c in ipairs(list) do
    local s = '{.id = '..q(c.id)
    if c.type then s = s..', .type = '..q(c.type) end
    if c.ids then
      local ids = {}
      for _, x in ipairs(c.ids) do ids[#ids+1] = q(x) end
      s = s..', .ids = {'..table.concat(ids, ', ')..'}, .nids = '..#c.ids
    end
    l[#l+1] = s..'}'
  end
  return '.'..field..' = {'..table.concat(l, ', ')..'}, .n_'..field..' = '..#list..', '
end
local function keyset(t, field)
  if not t then return '' end
  local l = {}
  for k, v in pairs(t) do if v then l[#l+1] = q(k) end end
  table.sort(l)
  return '.'..field..' = {'..table.concat(l, ', ')..'}, .n_'..field..' = '..#l..', '
end
local deckcards = {}
w('int G_N_CHALLENGES = ', #G.CHALLENGES, ';\n')
for i, ch in ipairs(G.CHALLENGES) do
  if ch.deck and ch.deck.cards then
    local l = {}
    for _, c in ipairs(ch.deck.cards) do
      l[#l+1] = string.format('{%s, %s, %s, %s, %s}', q(c.s), q(c.r), q(c.e), q(c.d), q(c.g))
    end
    w('static CardProto ch_cards_', i, '[] = {', table.concat(l, ', '), '};\n')
  end
end
w('Challenge G_CHALLENGES[] = {\n')
for i, ch in ipairs(G.CHALLENGES) do
  local s = '    {.name = '..q(ch.name)..', .id = '..q(ch.id)..', '
  if ch.rules then
    s = s..rules(ch.rules.custom, 'custom')..rules(ch.rules.modifiers, 'modifiers')
  end
  s = s..cards(ch.jokers, 'jokers')..cards(ch.consumeables, 'consumeables')..cards(ch.vouchers, 'vouchers')
  if ch.deck then
    local d = '.present = true, .type = '..q(ch.deck.type)..', '
    if ch.deck.cards then d = d..'.cards = ch_cards_'..i..', .ncards = '..#ch.deck.cards..', ' end
    d = d..keyset(ch.deck.yes_ranks, 'yes_ranks')..keyset(ch.deck.no_ranks, 'no_ranks')
    d = d..keyset(ch.deck.yes_suits, 'yes_suits')..keyset(ch.deck.no_suits, 'no_suits')
    if ch.deck.enhancement then d = d..'.enhancement = '..q(ch.deck.enhancement)..', ' end
    if ch.deck.edition then d = d..'.edition = '..q(ch.deck.edition)..', ' end
    if ch.deck.gold_seal then d = d..'.gold_seal = '..q(ch.deck.gold_seal)..', ' end
    s = s..'.deck = {'..d..'}, '
  end
  if ch.restrictions then
    local r = bans(ch.restrictions.banned_cards, 'banned_cards')..bans(ch.restrictions.banned_tags, 'banned_tags')..bans(ch.restrictions.banned_other, 'banned_other')
    s = s..'.restrictions = {'..r..'}, '
  end
  w(s, '},\n')
end
w('};\n')
f = assert(io.open(OUT..'/data_challenges.c', 'w'))
f:write(table.concat(out))
f:close()
print('generated '..#center_keys..' centers, '..#card_keys..' cards, '..#blind_keys..' blinds, '..#tag_keys..' tags, '..#G.CHALLENGES..' challenges')
