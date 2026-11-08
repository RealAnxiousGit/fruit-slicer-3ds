// Records kept between runs: Daily Fruit's calendar and streak, the Fruit Journal, and the Stats page.
// Everything lives in gSave; this is only the arithmetic around it.
#pragma once
#include "common.h"

namespace progress {
// Days since 1 January 2000 on the device's clock, in local time.
u32 today();
#ifdef DEV_TOOLS
void devSkipDay();  // developer builds: pretend it is one day later, until the game closes
#endif
void dayToDate(u32 day, int* year, int* month, int* dayOfMonth);
u32 dailySeed(u32 day);

// Records a Daily Fruit score for `day` and moves the calendar and streak along.
// Returns the coin bonus: paid for the first run of each day, more for a longer streak.
u32 dailyRecord(u32 day, u32 score);
bool dailyPlayed(u32 day);
u32 dailyScore(u32 day);    // that day's best, 0 if not played or too long ago
int dailyStreak(u32 day);   // the streak still alive on `day`: played that day or the day before
int dailyNumber(u32 day);   // which day of the player's calendar that is: 1 on the first day played, then one more every real day
u32 dailyBonusPreview(u32 day);  // what the first run on `day` would pay

// Fruit Journal: one page per fruit kind. The loose grape from a burst bunch counts as grapes.
constexpr int kNamedFruit = 21;  // every fruit kind the game throws, in its own order
constexpr int kJournalLevels = 4;
int journalPages();
int journalFruit(int page);        // fruit kind on that page
int journalKind(int fruit);        // where a sliced fruit is counted
const char* fruitName(int fruit);
int journalLevel(u32 count);       // milestones reached, 0 to kJournalLevels
u32 journalMilestone(int level);   // slices needed for milestone `level` (1 to kJournalLevels)
u32 journalReward(int level);      // coins it pays
// Coins for every milestone crossed between two sets of counts.
u32 journalPay(const u32* before, const u32* after);

// Stats page: the most-played mode, or MODE_COUNT before any run.
Mode favouriteMode();
}  // namespace progress
