#pragma once
#include "common.h"

// Orchard Blast: a run through rooms, with HP, XP, level-up cards and bosses.
// All of its state is plain data inside Game, so Game::start can still clear it with memset.

// Every card the level-up screen can offer. Upgrades stack, charms are taken once.
enum CardId : u8 {
	// Upgrades
	CARD_RING, CARD_HEALTH, CARD_ARMOR, CARD_BLADE, CARD_REGROWTH, CARD_LONGEDGE, CARD_STEADY, CARD_LUCKY, CARD_PARRY,
	// Passive charms
	CARD_JUICE, CARD_MAGNET, CARD_BOMBSQUAD, CARD_SECONDWIND, CARD_BATTERY, CARD_GOLDTOUCH, CARD_SEEDPOUCH, CARD_SHARPRIND,
	CARD_ECHO, CARD_SWEEP, CARD_LUCKYLEAF, CARD_OVERFLOW, CARD_RHYTHM, CARD_THICKPEEL, CARD_GLASS,
	// Reusable charms (slotted, with a cooldown)
	CARD_BLENDER, CARD_FREEZEPOP, CARD_DEFUSER, CARD_SMOOTHIE, CARD_CLEAVER, CARD_FRUITRAIN, CARD_PULL, CARD_SLOWMO,
	// Single-use charms (slotted, used once)
	CARD_GOLDAPPLE, CARD_FRUITBOMB, CARD_RINDWALL, CARD_RIPESEED, CARD_SKIPPEEL, CARD_BOSSBANE, CARD_LUCKYPIT, CARD_COINJAR,
	CARD_LASTSTAND, CARD_FLOOD,
	CARD_CUTBOARD, CARD_LIFESTEAL, CARD_BOSSHARVEST,
	// 2.1.0 New Passive Charms (8 Uncommon/Rare/Epic + 4 Mythics)
	CARD_CITRUS_SHIELD, CARD_SWEET_TOOTH, CARD_QUICK_DRAW, CARD_JUICY_JACKPOT,
	CARD_FROSTBITE_EDGE, CARD_CHAIN_REACTION, CARD_ROYAL_TRIBUTE, CARD_PHANTOM_PARRY,
	CARD_ORCHARD_CROWN, CARD_TWIN_HARVEST, CARD_SOLAR_BLADE, CARD_PRISMATIC_CORE,
	CARD_COUNT, CARD_NONE = 0xFF
};
static constexpr int kFirstMythicCard = CARD_ORCHARD_CROWN;
static constexpr int kMythicCount = 4;
enum CardKind : u8 { KIND_UPGRADE, KIND_PASSIVE, KIND_REUSABLE, KIND_SINGLE };
enum Rarity : u8 { RARE_COMMON, RARE_UNCOMMON, RARE_RARE, RARE_EPIC, RARE_LEGEND, RARE_MYTHIC };

struct CardInfo {
	const char* name;
	const char* line1;  // what it does, in two short lines
	const char* line2;
	CardKind kind;
	Rarity rarity;
	u8 maxLevel;        // upgrades: how many times it can be taken; charms: 1
	float lasts;        // reusable charms: seconds it stays on (0 for instant)
	float cooldown;     // reusable charms: seconds before it can be used again
};
extern const CardInfo kCards[CARD_COUNT];
const char* cardKindName(CardKind k);
int cardIcon(int card);  // its badge on SHEET_SPRITES
// What the next level of a card would do, with real numbers, for the details panel. Empty if nothing changes.
void cardNextLine(int card, int level, char* out, int size);

// The bosses of Orchard Blast (regular 4 in seeded 5-room loop, plus 3 Milestone Bosses every 25th room).
enum BossId : u8 { BOSS_MELON, BOSS_KING, BOSS_PINEAPPLE, BOSS_CHILI, BOSS_DURIAN, BOSS_LEMON, BOSS_APPLE, BOSS_COUNT };
static constexpr int kRegularBossCount = 4;
static constexpr int kFirstMilestoneBoss = BOSS_DURIAN;
static constexpr int kMilestoneBossCount = BOSS_COUNT - kFirstMilestoneBoss;
struct BossInfo {
	const char* name;
	u8 slices;  // clean slices to beat it
	u8 phases;
};
extern const BossInfo kBosses[BOSS_COUNT];

// A boss's projectiles live apart from the fruit, so nothing in the fruit code has to know about them.
enum ShotKind : u8 { SHOT_SEED, SHOT_SPLASH, SHOT_CANNON, SHOT_WAVE, SHOT_CROWN, SHOT_SPIKE, SHOT_EMBER, SHOT_THORN, SHOT_ACID };
struct Shot {
	float x, y, vx, vy, age, rot;
	float life;   // the crown and the juice wave: seconds left
	u8 kind;
	u8 hits;      // slices still needed to defuse
	u32 stroke;   // the swipe that last cut it
	bool on;
};

// The boss in the room, if any.
// What a boss is doing (see boss.cpp).
enum BossAttack : u8 {
	ATK_NONE,
	MEL_SEEDS,     // Seed Spit: a spray of seeds; slice them before they land
	MEL_FLOP,      // Belly Flop: jumps and slams down, splashing juice
	MEL_CHARGE,    // Rolling Charge: rolls at the screen. Swipe it to block
	KING_VOLLEY,   // Cannon Volley: cannonballs; slice them to defuse
	KING_LOB,      // Royal Lob: real bombs, and fruit that turn into bombs
	KING_CROWN,    // Crown Toss: the crown flies out and back
	KING_FUSE,     // Short Fuse: its fuse is lit; snuff it with three swipes, or it blows
	KING_GIANT,    // The Giant Bomb: he hides off screen and leaves a giant bomb; scrub it off the stage before it blows
	PINE_DARTS,    // Spiky Dart Fan: radial spiky darts launched outward; slice to parry
	PINE_WHIRL,    // Crown Whirlwind: sweeps across tossing shockwaves and bombs
	PINE_SLAM,     // Fortress Slam: leaps and crashes down releasing twin waves before tiring
	CHILI_EMBERS,  // Ember Volley: blazing fireballs arced up across the stage; swipe to snuff
	CHILI_MIRAGE,  // Heat Mirage: fast flaming decoys that morph into bombs mid-flight
	CHILI_RUSH,    // Scorching Dash: high-speed ember zig-zag across the screen before overheating
	PINE_BOMB,     // Pineapple Guard: lobs a heavy bomb; slice it back into him (watch out for returns!)
	DUR_THORNS,    // Dusty Durian: radial armored thorn bursts & dust ring shockwaves
	DUR_SLAM,      // Dusty Durian: heavy spiked slam with ground shockwaves
	DUR_SPIN,      // Dusty Durian: whirling armored spin dash across the stage
	LEM_ACID,      // Lord Lemon: crisscrossing sour citrus shots
	LEM_ZEST,      // Lord Lemon: bouncing sour zest balls & temporary speed hazard
	LEM_BLITZ,     // Lord Lemon: unpredictable figure-8 dash + rapid citrus volley
	LEM_LEMONADE,  // Lord Lemon: floatie squeeze fills bottom half with sour Lemonade + floating drain
	APP_WORMS,     // Aggro Apple: angry worms peek out—slice near/tap them quickly before they lunge!
	APP_BARRAGE,   // Aggro Apple: fierce bouncy apple-core seed barrage
	APP_CRUSH,     // Aggro Apple: aggressive dive slam with simultaneous worm ambush
};
// Only the last attack of a chain tires the boss out and drops its guard (Pineapple Guard uses lobbed bombs).

struct AppleWorm {
	float ox, oy;
	float emergeT;  // rises from 0 to 1
	float timer;    // seconds left before lunging at the player
	bool on;
};

// The boss art's poses, in the order the sprite sheet holds them (gen_assets.py BOSS_POSES).
enum BossPose { BOSS_POSE_GUARD, BOSS_POSE_OPEN, BOSS_POSE_DAZE, BOSS_POSE_WIND };

// The Bomb King's Giant Bomb: left in the middle while he hides off screen. Scrubbing it knocks it upward; enough
// hits launch it off the stage before its timer runs out.
struct GiantBomb {
	bool on, launched, done;
	u8 hits, need, ticks;
	float x, y, vy, t, total, tick, travel, jolt;
};

struct Boss {
	u8 id;
	u8 left;          // clean slices still needed
	u8 phase;         // 1 to 3
	u8 attack;        // the attack under way, by boss (see boss.cpp)
	u8 lastAttack;
	bool on;
	bool guard;       // its guard is up
	bool beaten;
	bool cleaved;     // Cleaver: the next slice goes through the guard
	float x, y, vx, vy;
	float baseY;
	float radius;
	float rx;         // 3D: the roll of a Rolling Charge
	float t;          // seconds since it arrived
	float attackT;    // seconds into the current attack
	float nextAttack; // seconds until the next one starts
	float openT;      // seconds its guard stays down
	float dazeT;      // confused after a slice: can't be hit, doesn't attack
	float roarT;      // a new phase: a short roar before it carries on
	float hurtT;      // a slice just landed: it jolts
	float deflectT;   // the last Deflected!, so one scribble can't drain HP
	float fuseT;      // The Bomb King's Short Fuse: seconds left
	int fuseCuts;
	u32 deflectStroke;  // a swipe is deflected only once
	int volley;       // shots left in the current attack
	float volleyT;
	float squash;     // landing squash, for the look
	float crownOut;   // The Bomb King's crown is away on a Crown Toss
	float enterT;     // drops in from above before the fight starts
	float leaveT;     // beaten: fades away
	u8 chain;         // attacks left in this chain: only the last one tires it out and drops the guard
	bool finisher;    // the attack under way is the last of its chain
	u8 giantPhases;   // bit n: the Giant Bomb has already come in phase n
	bool hidden;      // The Bomb King is off screen while his Giant Bomb is out
	GiantBomb giant;
	bool royal;       // Boss Rush's second lap: one more slice, shorter openings, quicker attacks
	u8 total;         // slices to beat it
	s8 track;         // the Shop music track playing for this fight
	u8 attackCycle;   // tracks cycles between special events (Pineapple bomb lob, Chili surge)
	float surgeT;     // The Inferno Pepper's temporary speedup duration
	float boomT;      // Milestone boss 5s death sequence: countdown to the next blast
	AppleWorm worms[4];  // Aggro Apple's peeking worms
	bool enraged;     // Milestone Boss Phase 2 (<= 50% HP): red tint, steam & faster guard drops
	bool lemonadeOn;  // Lord Lemon: bottom-half sour Lemonade hazard is active
	bool drainOn;     // Lord Lemon: sliceable Drain plug has surfaced
	u8 lemStage;      // 0 idle, 1 floatie put-on/squeeze fill, 2 active hazard, 3 draining out
	float lemLevel;   // 0..1 Lemonade fill fraction of lower screen
	float lemTimer;   // stage progress / wait until drain rises
	float drainX, drainY, drainRise;  // floating drain plug coordinates and bubble bobbing
	float lemHurtCd;  // brief i-frame cooldown between sour Lemonade hits
	float lemCooldown;  // seconds before Lemonade can flood again
	float steamT;     // Phase 2 bilateral steam puff accumulator
	float thornExt;   // Dusty Durian: 0..1 thorn extension/retraction progress
	float thornCd;    // cooldown until next thorn extension burst
	float thornT;     // stage timer within thorn burst
	u8 thornStage;    // 0 idle, 1 extending, 2 extended hazard, 3 retracting
};

inline float lemonadeSurfaceY(const Boss& b) {
	const float halfY = kFieldTop + (kFieldH - kFieldTop) * 0.50f;
	return lerpf((float)kFieldH, halfY, clampf(b.lemLevel, 0.0f, 1.0f));
}

// The run itself.
static constexpr int kOrchardRooms = 10;
static constexpr float kRoomSeconds = 15.0f;
static constexpr int kSlots = 2;
static constexpr int kMaxShots = 24;
static constexpr int kEchoPoints = 48;

struct Orchard {
	// Where the run is.
	int room;            // 1 to 10 (or 1, 2, 3... in Endless)
	float roomT;         // seconds into the room
	bool bossRoom;
	bool won;
	bool endless;        // always true: Orchard Blast is Endless by default
	bool resumePrompt;   // asking whether to Continue a saved run or Start Over
	u8 resumeFocus;      // 0 = Continue, 1 = Start Over
	u16 savedRoom;       // room shown in the Continue prompt
	u32 runSeed;         // deterministic seed determining boss order & wave variety for this run
	u8 bossOrder[kRegularBossCount];
	// Health.
	float hp, maxHp;
	float regenT;
	float hurtFlash;     // the HP bar flashes when hit
	float invulnT;       // Rind Wall
	float lastStandT;
	bool peelShield;     // Thick Peel: one hit blocked this room
	bool usedSecondWind;
	bool bombSquadUsed;  // this room's free bomb is gone
	bool droppedThisRoom;
	int roomBombs;       // bombs that hurt this room (for the run summary)
	int damageTaken;
	// XP.
	float xp;
	int level;
	int levelsWaiting;   // level-ups not yet chosen
	float xpPulse;
	// Cards.
	u8 upgrade[CARD_COUNT];  // levels of each upgrade, or 1 for each charm held
	u8 passiveOrder[24];     // passive charms in the order they were taken, for the Inventory
	int passiveCount;
	u8 slot[kSlots];         // slotted charms, CARD_NONE when empty
	float slotCool[kSlots];  // seconds until a reusable charm can be used again
	float slotOn[kSlots];    // seconds a reusable charm still runs
	int rerolls;
	int paidRerolls;         // coin-purchased rerolls this run (price increments +15 per purchase)
	int totalRerolls;        // total rerolls used this run (free + paid)
	bool bossTookHit;        // true once player takes damage during current boss fight
	float tempShieldHp;      // Citrus Shield temporary boss-room shield HP
	int sweetCounter;        // Sweet Tooth fruit counter (triggers every 10)
	int swipeCounter;        // Solar Blade / Frostbite Edge swipe counter
	int extraPicksLeft;      // Twin Harvest bonus card pick on current level-up
	u8 mythicRevealCard;     // Mythic card currently being revealed (CARD_NONE if none)
	float mythicRevealT;     // animation timer for rotating pinwheel reveal
	u32 coinsSpent;          // coins spent from this run's gathered coins on rerolls
	bool luckyPit;           // the next level-up shows 5 cards
	// Room 50 first-encounter retro dialogue cutscene & Dev Room/Level Skip prompt.
	bool cutscene;
	u8 dialogueLine;
	float dialogueChars;
	int dialogueLastChar;
	float dialoguePauseT;
	bool dialogueWaitRelease;
	bool tensionStarted;
	u8 pendingMilestoneBoss;
	bool devPrompt;
	u8 devField;             // 0 = room, 1 = level, 2 = start button
	int devRoomTarget;
	int devLevelTarget;
	// Effects of charms in use.
	float blenderT, slowmoT, floodT, stillT;  // Blender, Slow-Mo, Juice Flood, Freeze Pop
	bool coinJar;            // this room's coins count twice
	bool cleaver;            // the next slice on a boss goes through its guard
	float blenderCut;        // the Blender's next cut
	// Level-up screen. The game holds still while it is open, and for the 3, 2, 1 after.
	bool choosing;           // the cards are up
	u8 offer[5];
	int offerCount;
	int focus;               // the card being read
	bool placing;            // a slotted charm was taken: pick its slot
	u8 placingCard;
	float resumeT;           // 3, 2, 1 before play goes on
	// The boss and its shots.
	Boss boss;
	Shot shots[kMaxShots];
	int bossesBeaten;
	int newTracks;  // boss tracks this run unlocked in the Shop, for the results screen
	u32 bossCoins;  // Journal boss milestones reached this run, paid with the results
	// Echo Blade: the blade's path, replayed half a second later.
	// One point a step, so the point 30 steps back is where the blade was half a second ago.
	float echoX[kEchoPoints], echoY[kEchoPoints];
	bool echoOn[kEchoPoints];
	int echoHead;
	// Clean run stats, for the results.
	float fightT;
	int charmsUsed;
};

// Deterministic seeded boss ordering & Endless difficulty helper.
void orchardBuildBossOrder(u32 seed, u8 outOrder[kRegularBossCount]);
int orchardBossForRoom(int room, u32 seed, const u8 order[kRegularBossCount], bool* royalOut = nullptr);

// Numbers that depend on the Difficulty setting.
struct OrchardLevel {
	int startHp;
	int bombDamage;
	float bossDamage;   // times each boss attack's damage
	float bossSpeed;    // boss movement and projectile speed multiplier
	float attackSpeed;  // boss attack pattern and frequency multiplier
	float openTime;     // seconds a boss's guard stays down
	int deflectDamage;
	float xpNeed;       // times the XP each level needs
};
const OrchardLevel& orchardLevel(int diff);
int orchardXpFor(int level);  // XP to go from this level to the next, on Normal

// ---------------------------------------------------------------- the other 1.2 modes (modes12.cpp)
// Boss Rush: a ready-made build, then every boss back to back, then all of them again as Royals.
enum RushBuild : u8 { BUILD_TANK, BUILD_GLASS, BUILD_CHARMER, BUILD_COUNT, BUILD_NONE = 0xFF };
struct RushBuildInfo {
	const char* name;
	const char* line1;
	const char* line2;
};
extern const RushBuildInfo kRushBuilds[BUILD_COUNT];
constexpr int kRushStages = 5;     // 4 regular bosses + 1 random Milestone Boss finale
constexpr int kRecipeMax = 3;      // kinds of fruit in one recipe
constexpr int kRecipeBonusTime = 8;
constexpr int kSharpColours = 4;   // red circle, blue square, yellow triangle, green star
constexpr int kJuggleDrops = 3;

struct Modes12 {
	// Boss Rush
	bool rushPicking;      // the build signs are up
	u8 rushBuild, rushFocus, rushStage;
	// Recipe
	u8 recipeFruit[kRecipeMax], recipeNeed[kRecipeMax], recipeHave[kRecipeMax];
	int recipeKinds, cups, spoiled;
	float cupT, cupPulse, spoilPulse;
	// Sharpshooter
	u8 call;               // the colour that scores, 0 to 3
	float callT, callFlash;
	int calledHits, wrongHits;
	// Juggle
	int knocks, bestAir;
	float addT;
};

const char* sharpName(int colour);     // "red circle" and so on
u32 sharpColour(int colour, bool strong);
