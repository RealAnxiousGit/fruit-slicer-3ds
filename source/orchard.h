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
	CARD_COUNT, CARD_NONE = 0xFF
};
enum CardKind : u8 { KIND_UPGRADE, KIND_PASSIVE, KIND_REUSABLE, KIND_SINGLE };
enum Rarity : u8 { RARE_COMMON, RARE_RARE, RARE_LEGEND };

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

// The bosses of the 10-room run, and the Endless pool later on.
enum BossId : u8 { BOSS_MELON, BOSS_KING, BOSS_COUNT };
struct BossInfo {
	const char* name;
	u8 slices;  // clean slices to beat it
	u8 phases;
};
extern const BossInfo kBosses[BOSS_COUNT];

// A boss's projectiles live apart from the fruit, so nothing in the fruit code has to know about them.
enum ShotKind : u8 { SHOT_SEED, SHOT_SPLASH, SHOT_CANNON, SHOT_WAVE, SHOT_CROWN };
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
	MEL_SEEDS,   // Seed Spit: a spray of seeds; slice them before they land
	MEL_FLOP,    // Belly Flop: jumps and slams down, splashing juice
	MEL_CHARGE,  // Rolling Charge: rolls at the screen. Swipe it to block
	KING_VOLLEY, // Cannon Volley: cannonballs; slice them to defuse
	KING_LOB,    // Royal Lob: real bombs, and fruit that turn into bombs
	KING_CROWN,  // Crown Toss: the crown flies out and back
	KING_FUSE,   // Short Fuse: its fuse is lit; snuff it with three swipes, or it blows
	KING_GIANT,  // The Giant Bomb: he hides off screen and leaves a giant bomb; scrub it off the stage before it blows
};
// Only the last attack of a chain tires the boss out and drops its guard.

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
};

// The run itself.
static constexpr int kOrchardRooms = 10;
static constexpr float kRoomSeconds = 15.0f;
static constexpr int kSlots = 2;
static constexpr int kMaxShots = 24;
static constexpr int kEchoPoints = 48;

struct Orchard {
	// Where the run is.
	int room;            // 1 to 10
	float roomT;         // seconds into the room
	bool bossRoom;
	bool won;
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
	u8 passiveOrder[16];     // passive charms in the order they were taken, for the Inventory
	int passiveCount;
	u8 slot[kSlots];         // slotted charms, CARD_NONE when empty
	float slotCool[kSlots];  // seconds until a reusable charm can be used again
	float slotOn[kSlots];    // seconds a reusable charm still runs
	int rerolls;
	bool luckyPit;           // the next level-up shows 5 cards
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
constexpr int kRushStages = 4;     // The Big Melon, The Bomb King, then both again as Royals
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
