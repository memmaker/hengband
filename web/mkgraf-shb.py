#!/usr/bin/env python3
"""Write lib/pref/graf-shb.prf: Shockbolt tiles (Angband 4.2's 64x64 set,
taken from ~/Games/tactical-angband/lib/tiles/shockbolt) for Hengband 3.x's
monsters / base items / terrains (lib/edit/*Definitions.jsonc) and the S:
bolt slots.

Port of ~/Games/frogcomposband/web/mkgraf-shb.py (JSON reader, R:/K:/F: by
id, Hengband terrain keys).  Monsters and objects are matched
by name against Shockbolt's 4.2 pref; an entry without a tile of its own gets
its family's tile (monsters: same symbol, same colour if possible; objects:
same tval) and is marked "# stand-in".  Flavoured objects (food, potions,
scrolls: "flavor_name") get the 4.2 flavour tile of the same name
or colour.  Objects are written as K:<baseitem id>, features
as F:<terrain id>:lit:torch:dark (standard/lite/dark, Shockbolt's triplets), all by
hand.  Run from the repo root: python3 web/mkgraf-shb.py"""
import re, os, unicodedata
SHB = os.path.expanduser('~/Games/tactical-angband/lib/tiles/shockbolt')
GD = os.path.expanduser('~/Games/tactical-angband/lib/gamedata')
ED = 'lib/edit'
COLS = 'dwsorgbuDWvyRGBU'
CNAME = ['Dark', 'White', 'Slate', 'Orange', 'Red', 'Green', 'Blue', 'Umber', 'Light Dark',
         'Light Slate', 'Violet', 'Yellow', 'Light Red', 'Light Green', 'Light Blue', 'Light Umber']

def rd(p): return open(p, encoding='latin-1').read()
def norm(s):
    s = unicodedata.normalize('NFKD', s).encode('ascii', 'ignore').decode()
    return re.sub(r'[^a-z0-9]+', ' ', s.replace('&', '').replace('~', '').lower()).strip()

# --- Shockbolt's 4.2 pref ---
mon, obj, feat, trap, gf = {}, {}, {}, {}, {}
for l in open(f'{SHB}/graf-shb-dark.prf', encoding='utf-8').read().splitlines():
    p = l.split(':')
    if p[0] == 'monster': mon[norm(p[1])] = (p[2], p[3])
    elif p[0] == 'object': obj.setdefault(p[1], {})[norm(p[2])] = (p[3], p[4])
    elif p[0] == 'feat': feat[(p[1], p[2])] = (p[3], p[4])
    elif p[0] == 'trap': trap[(p[1], p[2])] = (p[3], p[4])
    elif p[0] == 'GF': gf[(p[1].split(' |')[0], p[2])] = (p[3], p[4])
flav = {int(m[1]): (m[2], m[3]) for m in re.finditer(r'^flavor:(\d+):(0x\w+):(0x\w+)', rd(f'{SHB}/flvr-shb.prf'), re.M)}

# 4.2 monster glyph/colour (glyph from monster_base unless overridden) -> family tiles
base = dict(re.findall(r'^name:(.*)\nglyph:(.)', rd(f'{GD}/monster_base.txt'), re.M))
fam = {}
for blk in open(f'{GD}/monster.txt', encoding='utf-8').read().split('\nname:')[1:]:
    name = blk.split('\n')[0]
    g = re.search(r'^glyph:(.)', blk, re.M)
    b = re.search(r'^base:(.*)', blk, re.M)
    c = re.search(r'^color:(.)', blk, re.M)
    g = g[1] if g else base.get(b[1]) if b else None
    if g and norm(name) in mon: fam.setdefault(g, []).append((c[1] if c else 'w', mon[norm(name)]))

# 4.2 flavours: kind -> colour letter -> first tile, kind -> flavour name -> tile
fl42, fln = {}, {}
kind = None
for l in rd(f'{GD}/flavor.txt').splitlines():
    p = l.split(':')
    if p[0] == 'kind': kind = p[1]
    elif p[0] in ('flavor', 'fixed') and int(p[1]) in flav:
        col = p[3] if p[0] == 'fixed' else p[2]
        t = flav[int(p[1])]
        if col in CNAME: fl42.setdefault(kind, {}).setdefault(COLS[CNAME.index(col)], t)
        if p[0] == 'flavor' and len(p) > 3: fln.setdefault(kind, {}).setdefault(norm(p[3]), t)

CN = {'Black': 'd', 'White': 'w', 'Gray': 's', 'Orange': 'o', 'Red': 'r', 'Green': 'g', 'Blue': 'b',
      'Brown': 'u', 'Dark Gray': 'D', 'Light Gray': 'W', 'Violet': 'v', 'Yellow': 'y', 'Light Red': 'R',
      'Light Green': 'G', 'Light Blue': 'B', 'Light Brown': 'U'}
def entries(f):
    """(id, name or terrain key, glyph, colour, tval, sval, flavour) of every top-level entry"""
    txt = open(f'{ED}/{f}', encoding='utf-8').read()
    parts = re.split(r'^      "id": (\d+),', txt, flags=re.M)
    out = []
    for i in range(1, len(parts), 2):
        b = parts[i + 1]
        g = lambda p: (re.search(p, b) or [None, None])[1]
        name = g(r'"key": "([^"]*)"') or g(r'"name": \{\s*"ja": "(?:\\.|[^"])*",\s*"en": "((?:\\.|[^"])*)"')
        fl = g(r'"flavor_name": \{[^}]*"en": "([^"]*)"') or ''
        out.append((int(parts[i]), name.replace('\\"', '"'), g(r'"character": "((?:\\.|[^"])*)"') or '?',
                    CN.get(g(r'"color": "([^"]*)"'), 'w'), int(g(r'"type_value": (\d+)') or 0),
                    int(g(r'"subtype_value": (\d+)') or 0), fl))
    return out

out, n = [], {'exact': 0, 'stand-in': 0, 'hand': 0}
def put(line, name, how):
    n[how] += 1
    out.append(f'# {name}' + (' (stand-in)' if how == 'stand-in' else ''))
    out.append(line)

# --- Monsters ---
# symbols missing from 4.2 -> a 4.2 family
GLYPH = {'Q': 'G', 'N': 'p', 'x': 'X', 'l': 'l', 'I': 'I', 'z': 'z', 'A': 'A', 'U': 'U', 'Y': 'Y',
         'y': 'y', 'm': 'm', '$': '$', '!': '$', '?': '$', '=': '$', '|': '$', '(': '$', '/': '$',
         '~': '$', '*': '*', '.': 'l', '#': 'l', '+': 'l', ',': ',', 'n': 'n', 'j': 'j'}
# Hengband names of monsters 4.2 has under another name
ALIAS = {'Novice warrior': 'Soldier', 'Novice rogue': 'Cutpurse', 'Novice priest': 'Acolyte',
         'Novice mage': 'Apprentice', 'Novice paladin': 'Warrior', 'Novice ranger': 'Archer',
         'Novice archer': 'Archer', 'Smeagol': 'Sméagol',
         "Grip, Farmer Maggot's Dog": "Grip, Farmer Maggot's dog",
         "Fang, Farmer Maggot's Dog": "Fang, Farmer Maggot's dog"}
out.append('##### Monsters #####')
for idx, name, g, c, *_ in entries('MonraceDefinitions.jsonc'):
    if idx == 0: continue
    t = mon.get(norm(ALIAS.get(name, name)))
    if t: put(f'R:{idx}:{t[0]}/{t[1]}', name, 'exact'); continue
    f = fam.get(g) or fam.get(GLYPH.get(g, g)) or fam['p']
    t = next((tt for cc, tt in f if cc == c), f[0][1])
    put(f'R:{idx}:{t[0]}/{t[1]}', name, 'stand-in')

# The player: warrior tile; per class/race from xtra-shb ($GENDER lines skipped)
out.append('R:0:0x83/0x87')
skip = False
for l in rd(f'{SHB}/xtra-shb.prf').splitlines():
    if l.startswith('?:'):
        skip = 'GENDER' in l
        if not skip: out.append(l)
    elif l.startswith('monster:<player>') and not skip:
        p = l.split(':'); out.append(f'R:0:{p[2]}/{p[3].split()[0]}')
out.append('?:1')

# --- Objects (K:<baseitem id>) ---
TV = {1: 'skeleton', 2: 'bottle', 3: 'junk', 4: 'whistle', 5: 'spike', 7: 'chest', 8: 'figurine',
      9: 'statue', 10: 'corpse', 11: 'capture', 16: 'shot', 17: 'arrow', 18: 'bolt', 19: 'bow',
      20: 'digger', 21: 'hafted', 22: 'polearm', 23: 'sword', 30: 'boots', 31: 'gloves', 32: 'helm',
      33: 'crown', 34: 'shield', 35: 'cloak', 36: 'soft armour', 37: 'hard armour',
      38: 'dragon armour', 39: 'light', 40: 'amulet', 45: 'ring', 46: 'quiver', 50: 'card',
      55: 'staff', 65: 'wand', 66: 'rod', 69: 'parchment', 70: 'scroll', 75: 'potion', 77: 'flask',
      80: 'food', 81: 'rune', 127: 'gold'}
# Hengband's realms -> the 4.2 book look nearest in spirit
BOOK = {90: 'prayer book', 91: 'magic book', 92: 'nature book', 93: 'shadow book',
        94: 'shadow book', 95: 'magic book', 96: 'magic book', 97: 'magic book', 98: 'shadow book',
        99: 'prayer book', 100: 'shadow book', 101: 'shadow book', 104: 'prayer book',
        105: 'nature book', 106: 'prayer book', 107: 'shadow book', 108: 'nature book',
        109: 'magic book'}
# tvals without 4.2 objects: hand-picked tiles of the set
OBJ_HAND = {'skeleton': ('0x97', '0x8E'), 'corpse': ('0x97', '0x95'), 'bottle': ('0x88', '0x80'),
            'junk': ('0x87', '0xB6'), 'whistle': ('0x87', '0xB6'), 'spike': ('0x86', '0xB5'),
            'figurine': ('0x92', '0x82'), 'statue': ('0x92', '0x82'), 'capture': ('0x88', '0x80'),
            'quiver': ('0x87', '0xB6'), 'staff': ('0x8A', '0x80'), 'wand': ('0x88', '0x80'),
            'rod': ('0x88', '0x80'), 'potion': ('0x88', '0x80')}
FLKIND = {80: 'mushroom', 75: 'potion', 70: 'scroll'}
out.append('##### Objects #####')
for idx, name, g, c, tv, sv, fl in entries('BaseitemDefinitions.jsonc'):
    if idx == 0: continue
    k = f'K:{idx}'
    tn = TV.get(tv)
    if tv in BOOK:                            # books: the n-th book of the 4.2 realm
        bl = list(obj[BOOK[tv]].values()); put(f'{k}:{bl[min(sv, len(bl) - 1)][0]}/{bl[min(sv, len(bl) - 1)][1]}', name, 'hand'); continue
    t = obj.get(tn, {}).get(norm(name))
    if t: put(f'{k}:{t[0]}/{t[1]}', name, 'exact'); continue
    if fl and tv in FLKIND:                   # flavoured: 4.2 flavour of that name, else colour
        fk = FLKIND[tv]
        t = fln.get(fk, {}).get(norm(fl))
        if t: put(f'{k}:{t[0]}/{t[1]}', f'{name} ({fl})', 'exact'); continue
        t = fl42[fk].get(c) or next(iter(fl42[fk].values()))
        put(f'{k}:{t[0]}/{t[1]}', f'{name} ({fl})', 'hand'); continue
    if tv in (40, 45):                        # one ring / amulet kind: a plain 4.2 flavour
        t = next(iter(fl42['ring' if tv == 45 else 'amulet'].values()))
        put(f'{k}:{t[0]}/{t[1]}', name, 'hand'); continue
    if tn in OBJ_HAND: put(f'{k}:{OBJ_HAND[tn][0]}/{OBJ_HAND[tn][1]}', name, 'hand'); continue
    t = next(iter(obj.get(tn, {}).values()), obj['none']['unknown item'])
    if tn in ('card', 'parchment'): t = next(iter(obj['scroll'].values()))
    if tn == 'rune': t = trap[('glyph of warding', 'lit')]
    put(f'{k}:{t[0]}/{t[1]}', name, 'stand-in')

# --- Features: F:<id>:lit:torch:dark (standard/lite/dark) ---
def F(code):
    if (code, 'lit') in feat: return [feat[(code, s)] for s in ('lit', 'torch', 'dark')]
    return [feat[(code, '*')]]
def T(nm): return [trap[(nm, s)] for s in ('lit', 'torch', 'dark')]
def one(a, c): return [(a, c)]
FLOOR = F('FLOOR'); GRASS = one('0x97', '0x83'); DIRT = one('0x97', '0x82'); SAND = one('0x97', '0x81')
WATER = one('0x98', '0xE0'); LAVA = one('0x98', '0xD7'); ACID = WATER
TREE = one('0x9F', '0x83'); PINE = one('0x9F', '0x87'); DEAD = one('0x9F', '0x81')
BUSH = one('0x8E', '0xC9'); DBUSH = one('0x8E', '0xC5'); PATTERN = one('0x97', '0x8B')
RUNE = T('glyph of warding'); CASTLE = one('0x96', '0x83'); TILED = one('0x96', '0x89')
SHOP = {'general': F('STORE_GENERAL'), 'armour': F('STORE_ARMOR'), 'weapon': F('STORE_WEAPON'),
        'temple': one('0x99', '0x86'), 'alchemy': F('STORE_ALCHEMY'), 'magic': F('STORE_MAGIC'),
        'home': F('HOME'), 'black': F('STORE_BLACK'), 'book': F('STORE_BOOK')}
TAG = {  # terrain key -> tile(s)
    'NONE': F('NONE'), 'FLOOR': FLOOR, '*FLOOR*': FLOOR, 'INVIS': FLOOR, 'GLYPH': RUNE, 'OPEN_DOOR': F('OPEN'),
    'BROKEN_DOOR': F('BROKEN'), 'UP_STAIR': F('LESS'), 'DOWN_STAIR': F('MORE'),
    'QUEST_ENTER': F('MORE'), 'TOWN_EXIT': F('MORE'), 'SHAFT_UP': F('LESS'), 'SHAFT_DOWN': F('MORE'),
    'ENTRANCE': F('MORE'), 'TOWN': CASTLE, 'ARENA_GATE': CASTLE, 'MUSEUM': CASTLE,
    'TRAP_TRAPDOOR': T('trap door'), 'TRAP_PIT': T('pit'), 'TRAP_SPIKED_PIT': T('spiked pit'),
    'TRAP_POISON_PIT': T('poison pit'), 'TRAP_TY_CURSE': T('rune of necromancy'),
    'TRAP_TELEPORT': T('teleport rune'), 'TRAP_FIRE': T('fire trap'), 'TRAP_ACID': T('acid trap'),
    'TRAP_SLOW': T('slow dart'), 'TRAP_LOSE_STR': T('strength loss dart'),
    'TRAP_LOSE_DEX': T('dexterity loss dart'), 'TRAP_LOSE_CON': T('constitution loss dart'),
    'TRAP_BLIND': T('blinding gas trap'), 'TRAP_CONFUSE': T('confusion gas trap'),
    'TRAP_POISON': T('poison gas trap'), 'TRAP_SLEEP': T('sleep gas trap'),
    'TRAP_TRAPS': T('rune of summon foe'), 'TRAP_ALARM': T('siren'), 'TRAP_OPEN': T('earthquake trap'),
    'TRAP_ARMAGEDDON': T('rune of dragonsong'), 'TRAP_PIRANHA': T('rune of summoning'),
    'TRAP_BEAR': T('knife trap'), 'TRAP_ICICLE': T('rock fall trap'), 'TRAP_BANANA': T('decoy'),
    'ROGUE_TRAP_1': T('mine trap'), 'ROGUE_TRAP_2': T('blast trap'), 'ROGUE_TRAP_3': T('area blast trap'),
    'EXPLOSIVE_RUNE': T('mine trap'), 'WEB': T('web'),
    'CLOSED_DOOR': F('CLOSED'), 'SECRET_DOOR': F('GRANITE'), 'RUBBLE': F('RUBBLE'),
    'MAGMA_VEIN': F('MAGMA'), 'QUARTZ_VEIN': F('QUARTZ'), 'MAGMA_HIDDEN': F('MAGMA'),
    'QUARTZ_HIDDEN': F('QUARTZ'), 'MAGMA_TREASURE': F('MAGMA_K'), 'QUARTZ_TREASURE': F('QUARTZ_K'),
    'PERMANENT': F('PERM'), 'PERMANENT_INNER': F('PERM'), 'PERMANENT_OUTER': F('PERM'),
    'PERMANENT_SOLID': F('PERM'), 'MOUNTAIN': F('PERM'), 'MOUNTAIN_WALL': F('PERM'),
    'GENERAL_STORE': SHOP['general'], 'ARMOURY': SHOP['armour'], 'WEAPON_SMITHS': SHOP['weapon'],
    'TEMPLE': SHOP['temple'], 'ALCHEMY_SHOP': SHOP['alchemy'], 'MAGIC_SHOP': SHOP['magic'],
    'BLACK_MARKET': SHOP['black'], 'HOME': SHOP['home'], 'BOOKSTORE': SHOP['book'],
    'JEWELER': SHOP['magic'], 'SHROOMERY': SHOP['alchemy'], 'DRAGONSKIN': SHOP['armour'],
    'DEEP_WATER': WATER, 'SHALLOW_WATER': WATER, 'DEEP_LAVA': LAVA, 'SHALLOW_LAVA': F('LAVA'),
    'DARK_PIT': T('pit'), 'DIRT': DIRT, 'GRASS': GRASS, 'FLOWER': BUSH, 'BRAKE': DBUSH, 'TREE': TREE,
    'SWAMP': WATER, 'MIRROR': one('0x97', '0x8F'), 'UNDETECTED': F('NONE'), 'GLASS_FLOOR': TILED,
    'OPEN_GLASS_DOOR': F('OPEN'), 'BROKEN_GLASS_DOOR': F('BROKEN'), 'CLOSED_GLASS_DOOR': F('CLOSED'),
    'GLASS_WALL': F('QUARTZ'), 'PERMANENT_GLASS_WALL': F('PERM'), 'OPEN_CURTAIN': F('OPEN'),
    'CLOSED_CURTAIN': F('CLOSED'), 'DEEP_WASTE': ACID, 'SHALLOW_WASTE': ACID, 'SEMI_PUN': RUNE,
    'SHADOW_ZAP': RUNE, 'SLUSH': WATER, 'SNOW_FLOOR': SAND, 'ICE_FLOOR': TILED, 'SNOW_WALL': F('GRANITE'),
    'ICE_WALL': F('QUARTZ'), 'GLACIER': F('QUARTZ'), 'SNOW_TREE': PINE, 'GLACIER_STEEP': F('PERM'),
    'CREVASSE': T('pit'),
    'RUNE_PROTECTION': RUNE, 'RUNE_EXPLOSION': T('mine trap'), 'QUEST_EXIT': F('LESS'),
    'QUEST_DOWN': F('MORE'), 'QUEST_UP': F('LESS'), 'HEAVY_COLD_ZONE': TILED, 'COLD_ZONE': TILED,
    'HEAVY_ELECTRICAL_ZONE': PATTERN, 'ELECTRICAL_ZONE': PATTERN,
    'DEEP_ACID_PUDDLE': ACID, 'SHALLOW_ACID_PUDDLE': ACID,
    'DEEP_POISONOUS_PUDDLE': WATER, 'SHALLOW_POISONOUS_PUDDLE': WATER}
out.append('##### Features #####')
for idx, tag, *_ in entries('TerrainDefinitions.jsonc'):
    tag = re.sub(r'_[23]$', '', tag) if tag.startswith('QUARTZ_') else tag   # QUARTZ_TREASURE_2 ...
    t = TAG.get(tag) or TAG.get(tag.split('_ON_')[0]) or (F('GRANITE') if tag.split('_ON_')[0] == 'GRANITE' else None)
    if not t:
        t = (F('CLOSED') if 'DOOR' in tag else PATTERN if 'PATTERN' in tag else CASTLE if 'BUILDING' in tag
             else F('GRANITE') if 'GRANITE' in tag or tag.startswith('*') else None)  # *X*: dungeon-gen placeholders
    if not t: raise SystemExit(f'feature {idx} {tag}: no tile')
    put(f'F:{idx}:' + ':'.join(f'{a}/{c}' for a, c in t), tag, 'hand')

# --- S: slots.  0x30..0x7F bolts (static | - / \ by colour) ---
GFC = ['DARK_WEAK', 'LIGHT_WEAK', 'SHARD', 'FIRE', 'FIRE', 'POIS', 'COLD', 'GRAVITY', 'DARK_WEAK',
       'SHARD', 'NEXUS', 'LIGHT_WEAK', 'METEOR', 'POIS', 'ELEC', 'SOUND']
out.append('##### Bolts #####')
for base, d in ((0x30, 'static'), (0x40, '90'), (0x50, '0'), (0x60, '45'), (0x70, '135')):
    for k in range(16):
        t = gf.get((GFC[k], d)) or gf[('*', d)]
        out.append(f'S:0x{base + k:02X}:{t[0]}/{t[1]}')

hdr = f"""# File: graf-shb.prf
#
# Shockbolt 64x64 tiles (Raymond Gaustadnes; Angband 4.2 lib/tiles/shockbolt)
# for FrogComposband.  Generated by web/mkgraf-shb.py -- do not edit.
# {n['exact']} entries by name, {n['hand']} mapped by hand, {n['stand-in']} family stand-ins.
"""
open('lib/pref/graf-shb.prf', 'w').write(hdr + '\n'.join(out) + '\n')
print(n)

if __name__ == '__main__':
    assert out and all(l.startswith(('#', 'R:', 'K:', 'F:', 'S:', '?:')) for l in out)
