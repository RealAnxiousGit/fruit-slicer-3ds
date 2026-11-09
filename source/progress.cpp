#include "progress.h"

#include <time.h>

namespace progress {

// ---------------------------------------------------------------- dates
// Days from 1 January 1970 for a calendar date (Howard Hinnant's days_from_civil).
static long daysFromCivil(long y, unsigned m, unsigned d) {
	y -= m <= 2;
	const long era = (y >= 0 ? y : y - 399) / 400;
	const unsigned yoe = (unsigned)(y - era * 400);
	const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
	const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
	return era * 146097 + (long)doe - 719468;
}
static const long kDay2000 = daysFromCivil(2000, 1, 1);

#ifdef DEV_TOOLS
static u32 sDevDays;
void devSkipDay() { sDevDays++; }
#else
static constexpr u32 sDevDays = 0;
#endif

u32 today() {
	time_t now = time(nullptr);
	struct tm* t = localtime(&now);
	if (!t) return 1;
	long d = daysFromCivil(t->tm_year + 1900L, (unsigned)t->tm_mon + 1, (unsigned)t->tm_mday) - kDay2000;
	return (d < 1 ? 1 : (u32)d) + sDevDays;  // 0 means "never" in the save
}

void dayToDate(u32 day, int* year, int* month, int* dayOfMonth) {
	long z = (long)day + kDay2000 + 719468;
	const long era = (z >= 0 ? z : z - 146096) / 146097;
	const unsigned doe = (unsigned)(z - era * 146097);
	const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
	const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
	const unsigned mp = (5 * doy + 2) / 153;
	const unsigned d = doy - (153 * mp + 2) / 5 + 1;
	const unsigned m = mp < 10 ? mp + 3 : mp - 9;
	*year = (int)(yoe + era * 400 + (m <= 2));
	*month = (int)m;
	*dayOfMonth = (int)d;
}

u32 dailySeed(u32 day) {
	// A few rounds of mixing, so neighbouring days play nothing alike. Never 0: xorshift would stall.
	u32 h = day * 0x9E3779B1u ^ 0x51CE0001u;
	h ^= h >> 16;
	h *= 0x85EBCA6Bu;
	h ^= h >> 13;
	h *= 0xC2B2AE35u;
	h ^= h >> 16;
	return h ? h : 0x51CE51CEu;
}

// ---------------------------------------------------------------- Daily Fruit
static u32 bonusFor(int streak) {
	int extra = streak - 1;
	if (extra < 0) extra = 0;
	if (extra > 6) extra = 6;
	return 20 + 5 * (u32)extra;  // 20 coins, plus 5 for each day of streak up to a week
}

u32 dailyRecord(u32 day, u32 score) {
	Settings& s = gSave;
	const u32 stored = score + 1 > score ? score + 1 : score;
	if (s.dailyFirstDay == 0) s.dailyFirstDay = (u16)(s.dailyDay ? s.dailyDay : day);
	if (s.dailyDay != 0 && day < s.dailyDay) {
		// The clock went back: file the score on its day if it's still on the calendar, and pay nothing.
		u32 back = s.dailyDay - day;
		if (back < (u32)kDailyDays && stored > s.dailyScore[back]) s.dailyScore[back] = stored;
		return 0;
	}
	const bool first = s.dailyDay != day;
	if (first) {
		const u32 gap = s.dailyDay ? day - s.dailyDay : (u32)kDailyDays;
		for (int i = kDailyDays - 1; i >= 0; i--) s.dailyScore[i] = (u32)i >= gap ? s.dailyScore[i - gap] : 0;
		s.dailyStreak = s.dailyDay != 0 && gap == 1 ? (u16)(s.dailyStreak + 1) : 1;
		if (s.dailyStreak > s.dailyBestStreak) s.dailyBestStreak = s.dailyStreak;
		s.dailyDay = day;
	}
	if (stored > s.dailyScore[0]) s.dailyScore[0] = stored;
	return first ? bonusFor(s.dailyStreak) : 0;
}

static u32 slotFor(u32 day) {
	const Settings& s = gSave;
	if (s.dailyDay == 0 || day > s.dailyDay || s.dailyDay - day >= (u32)kDailyDays) return 0;
	return s.dailyScore[s.dailyDay - day];
}

bool dailyPlayed(u32 day) { return slotFor(day) != 0; }
u32 dailyScore(u32 day) {
	u32 v = slotFor(day);
	return v ? v - 1 : 0;
}

int dailyStreak(u32 day) {
	const Settings& s = gSave;
	if (s.dailyDay == 0 || day > s.dailyDay + 1) return 0;
	return s.dailyStreak;
}

int dailyNumber(u32 day) {
	const Settings& s = gSave;
	const u32 first = s.dailyFirstDay ? s.dailyFirstDay : s.dailyDay;
	if (first == 0 || day <= first) return 1;
	return (int)(day - first) + 1;
}

u32 dailyBonusPreview(u32 day) {
	if (gSave.dailyDay == day) return 0;
	return bonusFor(gSave.dailyDay != 0 && day == gSave.dailyDay + 1 ? gSave.dailyStreak + 1 : 1);
}

// ---------------------------------------------------------------- Fruit Journal
static const char* const kNames[kNamedFruit] = {
	"Apple", "Orange", "Lemon", "Pear", "Plum", "Watermelon", "Kiwi", "Strawberry", "Peach", "Pineapple",
	"Coconut", "Banana", "Mango", "Cherry", "Dragon Fruit", "Spiky Fruit", "Grapes", "Grape", "Honey Melon",
	"Jelly Fruit", "Chili", "Banana Bunch", "Pomegranate", "Pinata",
};
static constexpr int kGrapes = 16, kGrape = 17;
static const u32 kMilestones[kJournalLevels] = {10, 50, 250, 1000};
static const u32 kRewards[kJournalLevels] = {5, 15, 40, 100};

int journalPages() { return kNamedFruit - 1; }
int journalFruit(int page) { return page < kGrape ? page : page + 1; }
int journalKind(int fruit) { return fruit == kGrape ? kGrapes : fruit; }
const char* fruitName(int fruit) { return fruit >= 0 && fruit < kNamedFruit ? kNames[fruit] : ""; }

int journalLevel(u32 count) {
	int level = 0;
	while (level < kJournalLevels && count >= kMilestones[level]) level++;
	return level;
}
u32 journalMilestone(int level) { return level >= 1 && level <= kJournalLevels ? kMilestones[level - 1] : 0; }
u32 journalReward(int level) { return level >= 1 && level <= kJournalLevels ? kRewards[level - 1] : 0; }

u32 journalPay(const u32* before, const u32* after) {
	u32 coins = 0;
	for (int f = 0; f < kNamedFruit; f++)
		for (int level = journalLevel(before[f]) + 1; level <= journalLevel(after[f]); level++) coins += journalReward(level);
	return coins;
}

// ---------------------------------------------------------------- Stats
Mode favouriteMode() {
	int best = MODE_COUNT;
	u16 most = 0;
	for (int m = 0; m < MODE_COUNT; m++) {
		if (gSave.modePlays[m] > most) {
			most = gSave.modePlays[m];
			best = m;
		}
	}
	return (Mode)best;
}

}  // namespace progress
