// Everything the Shop sells, one line each, in the order it shows on its tab.
// Add a line and the item is on sale: its tile, scrolling, buying, saving and the Options lists all follow.
// Never reorder or remove a line once it has shipped, because saves remember items by their place here.
//
// SHOP_THEME(name, price, line, plank rgb, accent rgb, bar rgb)   also needs its picture in gen_backgrounds.py
// SHOP_TRAIL(name, price, line, shape, colour a, colour b)        shapes are listed in blade.cpp
// SHOP_TRACK(name, price, line, file in romfs/audio, beats per minute)   0 keeps it out of Beat Slice
// BOSS_TRACK(name, price, line, file, beats per minute, boss)   a Shop song that stays locked until that boss
//                                                                 is beaten (0 The Big Melon, 1 The Bomb King)
// SHOP_SPLAT(name, price, line, colour)                           0 uses each fruit's own juice
// SHOP_SKIN(name, price, line, recolour %, colour, brightness %, opacity %, shine %)   the fruit's own colours fade by recolour %
#ifndef SHOP_THEME
#define SHOP_THEME(...)
#endif
#ifndef SHOP_TRAIL
#define SHOP_TRAIL(...)
#endif
#ifndef SHOP_TRACK
#define SHOP_TRACK(...)
#endif
#ifndef BOSS_TRACK
#define BOSS_TRACK(name, price, line, file, bpm, boss) SHOP_TRACK(name, price, line, file, bpm)
#endif
#ifndef SHOP_SPLAT
#define SHOP_SPLAT(...)
#endif
#ifndef SHOP_SKIN
#define SHOP_SKIN(...)
#endif

SHOP_THEME("Night Picnic", 100, "Gingham under the fireflies.", 40, 48, 110, 255, 214, 120, 16, 20, 54)
SHOP_THEME("Beach Towel", 160, "Stripes, sun and a little sand.", 26, 95, 103, 255, 206, 80, 120, 40, 36)
SHOP_THEME("Kitchen Counter", 220, "Terrazzo by the window.", 79, 75, 72, 255, 170, 90, 58, 56, 54)
SHOP_THEME("Snowfall", 320, "Fresh snow and tiny footprints.", 61, 87, 130, 200, 230, 255, 36, 52, 86)
SHOP_THEME("Autumn Leaves", 420, "Planks under falling maple.", 110, 60, 30, 255, 170, 60, 56, 30, 16)
SHOP_THEME("Pumpkin Patch", 380, "Vines and pumpkins after dark.", 86, 52, 96, 255, 168, 60, 34, 22, 42)
SHOP_THEME("Spring Blossom", 360, "Soft grass and drifting petals.", 39, 97, 63, 255, 214, 226, 22, 62, 44)
SHOP_THEME("Bamboo Mat", 280, "Rolled slats tied with cord.", 82, 90, 46, 255, 222, 120, 50, 58, 28)

SHOP_TRAIL("Ink", 75, "A brush stroke that dries as it goes.", TR_INK, 0x222034, 0x222034)
SHOP_TRAIL("Flame", 140, "Hot at the tip, embers behind.", TR_FLAME, 0xDC2814, 0xFFDC46)
SHOP_TRAIL("Rainbow", 240, "Every colour, all at once.", TR_RAINBOW, 0, 0)
SHOP_TRAIL("Ribbon", 250, "A ribbon that twists as it flies.", TR_RIBBON, 0xFFF0F6, 0xFF6EAA)
SHOP_TRAIL("Sparkle", 340, "Leaves a trail of twinkles.", TR_SPARKLE, 0xFFEC96, 0xFFEC96)
SHOP_TRAIL("Comet", 420, "A bright head and a long tail.", TR_COMET, 0x5A96FF, 0xE1F5FF)
SHOP_TRAIL("Sunset", 190, "Orange sliding into violet.", TR_FLAME, 0x7A2FB0, 0xFFA040)
SHOP_TRAIL("Candy Cane", 280, "Red and white, twisting.", TR_RIBBON, 0xFFFFFF, 0xE02838)
SHOP_TRAIL("Aurora", 380, "A green glow fading to blue.", TR_COMET, 0x2C6CD8, 0x7CFFB0)

// Recorded music by Of Far Different Nature (fardifferent.carrd.co), CC BY 4.0: Loop Box #1 and #4.
SHOP_TRACK("Summer House", 120, "Chill house for easy slicing.", "music_summer", 128)
SHOP_TRACK("8-Bit Hero Quest", 160, "Bouncy chiptune adventure.", "music_hero", 130)
SHOP_TRACK("Friendly Trap", 200, "Trap drums and a sunny hook.", "music_trap", 70)
SHOP_TRACK("Unruly Beat", 240, "A laid-back hip-hop groove.", "music_unruly", 82)
SHOP_TRACK("Vengeance Electro", 360, "Big electro drops.", "music_vengeance", 128)
SHOP_TRACK("Eternal Explorer", 340, "Wide, warm and cinematic.", "music_explorer", 130)
// Songs by NickBin and friends, converted by tools/convert_shop_music.py.
SHOP_TRACK("Ahead of the Curve", 400, "NickBin, King Bakuyo Pro. Drum & Bass.", "music_ahead", 175)
SHOP_TRACK("SPARE FOR ME", 380, "NickBin ft. Mq_. Hyperpop.", "music_spare", 180)
SHOP_TRACK("My Own Fright", 280, "NickBin ft. Cabex Music. Pop.", "music_fright", 160)
SHOP_TRACK("WORTHLESS ENEMY", 420, "NickBin ft. Aneolent. Metal.", "music_worthless", 180)
SHOP_TRACK("Lies", 260, "NickBin. EDM.", "music_lies", 128)
SHOP_TRACK("Lies (VIP Remix)", 440, "NickBin. EDM.", "music_liesvip", 128)
SHOP_TRACK("JUST STAY WITH ME", 480, "NickBin. Dariacore.", "music_staywithme", 150)
SHOP_TRACK("Show the World You're Enough", 300, "NickBin ft. Cabex Music. Calling All Villains. Pop.", "music_enough", 100)
// 1.2: boss tracks, made by tools/gen_audio.py. Each boss's set unlocks when it is first beaten.
BOSS_TRACK("Seed Spitter", 320, "The Big Melon's hopping chip tune.", "music_boss_seeds", 144, 0)
BOSS_TRACK("Rind and Run", 320, "The Big Melon's swung chase.", "music_boss_rind", 132, 0)
BOSS_TRACK("Long Live the Fuse", 400, "The Bomb King's royal march.", "music_boss_fuse", 116, 1)
BOSS_TRACK("Powder Keg Court", 420, "The Bomb King's drums and brass.", "music_boss_keg", 128, 1)
// Found online, levelled by tools/convert_shop_music.py (licences in docs/music-licences, credited in Credits).
BOSS_TRACK("8bit Action Boss Battle", 340, "MintoDog. Chiptune, CC0.", "music_boss_minto", 145, 0)
BOSS_TRACK("Boss Fight 2", 360, "ansimuz. Chiptune and synthwave, CC0.", "music_boss_ansimuz", 130, 0)
BOSS_TRACK("Boss Fight Bounce", 350, "Locomule. Chiptune, CC0.", "music_boss_bounce", 120, 0)
BOSS_TRACK("Epic Boss Battle", 440, "Juhani Junkala. Orchestral, CC0.", "music_boss_epic", 175, 1)
BOSS_TRACK("A Champion's Will", 460, "ATMANAN. Orchestral, CC BY 4.0.", "music_boss_champion", 140, 1)
// 2.0 Milestone Boss tracks (500-750 coins scaling with intensity, unlocked on milestone boss wins).
BOSS_TRACK("Sour Circuit", 520, "Glitch Hop milestone boss battle.", "music_boss_glitch", 110, 4)
BOSS_TRACK("Abyssal Core", 600, "Dubstep milestone boss drop.", "music_boss_dubstep", 140, 4)
BOSS_TRACK("Core Meltdown", 680, "Drum & Bass milestone chase.", "music_boss_dnb", 174, 4)
BOSS_TRACK("Slicer's Reckoning", 750, "Neurofunk DnB Room 50 finale.", "music_boss_neuro", 176, 4)

// Colours are 0xRRGGBB.
SHOP_SPLAT("Ink Blot", 50, "Every fruit lands in black ink.", 0x201C30)
SHOP_SPLAT("Bubblegum", 80, "Sticky, sweet and very pink.", 0xFF6EB4)
SHOP_SPLAT("Lime Soda", 110, "A fizzy green splash.", 0x8CE640)
SHOP_SPLAT("Blue Raspberry", 140, "No fruit has ever been this blue.", 0x2C8CFF)
SHOP_SPLAT("Golden Honey", 200, "Slow, thick and shining.", 0xF2B420)
SHOP_SPLAT("Ghost Milk", 250, "A pale splash that barely shows.", 0xE8F0FF)

SHOP_SKIN("Solid Gold", 650, "Every fruit cast in polished gold.", 100, 0xFFC84A, 210, 100, 85)
SHOP_SKIN("Clear Glass", 550, "Blown glass with a hint of the fruit.", 60, 0xC8E8FF, 180, 60, 75)
SHOP_SKIN("Holographic", 750, "Shiny transparent rainbow iridescence.", 48, 0xE0F4FF, 225, 68, 100)

#undef SHOP_THEME
#undef SHOP_TRAIL
#undef SHOP_TRACK
#undef BOSS_TRACK
#undef SHOP_SPLAT
#undef SHOP_SKIN
