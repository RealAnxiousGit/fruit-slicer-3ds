// Everything the Shop sells, one line each, in the order it shows on its tab.
// Add a line and the item is on sale: its tile, scrolling, buying, saving and the Options lists all follow.
// Never reorder or remove a line once it has shipped, because saves remember items by their place here.
//
// SHOP_THEME(name, price, line, plank rgb, accent rgb, bar rgb)   also needs its picture in gen_backgrounds.py
// SHOP_TRAIL(name, price, line, shape, colour a, colour b)        shapes are listed in blade.cpp
// SHOP_TRACK(name, price, line, file in romfs/audio, beats per minute)   0 keeps it out of Beat Slice
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
#ifndef SHOP_SPLAT
#define SHOP_SPLAT(...)
#endif
#ifndef SHOP_SKIN
#define SHOP_SKIN(...)
#endif

SHOP_THEME("Night Picnic", 120, "Gingham under the fireflies.", 40, 48, 110, 255, 214, 120, 16, 20, 54)
SHOP_THEME("Beach Towel", 200, "Stripes, sun and a little sand.", 30, 110, 120, 255, 206, 80, 120, 40, 36)
SHOP_THEME("Kitchen Counter", 300, "Terrazzo by the window.", 96, 92, 88, 255, 170, 90, 58, 56, 54)
SHOP_THEME("Snowfall", 400, "Fresh snow and tiny footprints.", 70, 100, 150, 200, 230, 255, 36, 52, 86)
SHOP_THEME("Autumn Leaves", 500, "Planks under falling maple.", 110, 60, 30, 255, 170, 60, 56, 30, 16)
SHOP_THEME("Pumpkin Patch", 450, "Vines and pumpkins after dark.", 86, 52, 96, 255, 168, 60, 34, 22, 42)
SHOP_THEME("Spring Blossom", 450, "Soft grass and drifting petals.", 44, 110, 72, 255, 214, 226, 22, 62, 44)
SHOP_THEME("Bamboo Mat", 350, "Rolled slats tied with cord.", 96, 106, 54, 255, 222, 120, 50, 58, 28)

SHOP_TRAIL("Ink", 80, "A brush stroke that dries as it goes.", TR_INK, 0x222034, 0x222034)
SHOP_TRAIL("Flame", 150, "Hot at the tip, embers behind.", TR_FLAME, 0xDC2814, 0xFFDC46)
SHOP_TRAIL("Rainbow", 250, "Every colour, all at once.", TR_RAINBOW, 0, 0)
SHOP_TRAIL("Ribbon", 250, "A ribbon that twists as it flies.", TR_RIBBON, 0xFFF0F6, 0xFF6EAA)
SHOP_TRAIL("Sparkle", 350, "Leaves a trail of twinkles.", TR_SPARKLE, 0xFFEC96, 0xFFEC96)
SHOP_TRAIL("Comet", 450, "A bright head and a long tail.", TR_COMET, 0x5A96FF, 0xE1F5FF)
SHOP_TRAIL("Sunset", 200, "Orange sliding into violet.", TR_FLAME, 0x7A2FB0, 0xFFA040)
SHOP_TRAIL("Candy Cane", 300, "Red and white, twisting.", TR_RIBBON, 0xFFFFFF, 0xE02838)
SHOP_TRAIL("Aurora", 400, "A green glow fading to blue.", TR_COMET, 0x2C6CD8, 0x7CFFB0)

// Recorded music by Of Far Different Nature (fardifferent.carrd.co), CC BY 4.0: Loop Box #1 and #4.
SHOP_TRACK("Summer House", 150, "Chill house for easy slicing.", "music_summer", 128)
SHOP_TRACK("8-Bit Hero Quest", 200, "Bouncy chiptune adventure.", "music_hero", 130)
SHOP_TRACK("Friendly Trap", 250, "Trap drums and a sunny hook.", "music_trap", 70)
SHOP_TRACK("Unruly Beat", 300, "A laid-back hip-hop groove.", "music_unruly", 82)
SHOP_TRACK("Vengeance Electro", 400, "Big electro drops.", "music_vengeance", 128)
SHOP_TRACK("Eternal Explorer", 450, "Wide, warm and cinematic.", "music_explorer", 130)
// Songs by NickBin and friends, converted by tools/convert_shop_music.py.
SHOP_TRACK("Ahead of the Curve", 350, "NickBin, King Bakuyo Pro. Drum & Bass.", "music_ahead", 175)
SHOP_TRACK("SPARE FOR ME", 350, "NickBin ft. Mq_. Hyperpop.", "music_spare", 180)
SHOP_TRACK("My Own Fright", 400, "NickBin ft. Cabex Music. Pop.", "music_fright", 160)
SHOP_TRACK("WORTHLESS ENEMY", 400, "NickBin ft. Aneolent. Metal.", "music_worthless", 180)
SHOP_TRACK("Lies", 300, "NickBin. EDM.", "music_lies", 128)
SHOP_TRACK("Lies (VIP Remix)", 450, "NickBin. EDM.", "music_liesvip", 128)
SHOP_TRACK("JUST STAY WITH ME", 500, "NickBin. Dariacore.", "music_staywithme", 150)

// Colours are 0xRRGGBB.
SHOP_SPLAT("Ink Blot", 60, "Every fruit lands in black ink.", 0x201C30)
SHOP_SPLAT("Bubblegum", 90, "Sticky, sweet and very pink.", 0xFF6EB4)
SHOP_SPLAT("Lime Soda", 120, "A fizzy green splash.", 0x8CE640)
SHOP_SPLAT("Blue Raspberry", 150, "No fruit has ever been this blue.", 0x2C8CFF)
SHOP_SPLAT("Golden Honey", 220, "Slow, thick and shining.", 0xF2B420)
SHOP_SPLAT("Ghost Milk", 280, "A pale splash that barely shows.", 0xE8F0FF)

SHOP_SKIN("Solid Gold", 800, "Every fruit cast in polished gold.", 100, 0xFFC84A, 210, 100, 85)
SHOP_SKIN("Clear Glass", 700, "Blown glass with a hint of the fruit.", 60, 0xC8E8FF, 180, 60, 75)

#undef SHOP_THEME
#undef SHOP_TRAIL
#undef SHOP_TRACK
#undef SHOP_SPLAT
#undef SHOP_SKIN
