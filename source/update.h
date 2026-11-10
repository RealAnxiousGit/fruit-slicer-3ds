// Update check (release plan 3.2): once a day, with the player's say-so, asks GitHub for a newer release,
// offers it, and has the port download and install it. Never during a run, and silent without a network.
#pragma once
#include "common.h"

namespace update {
enum State {
	IDLE,
	ASK,          // first start: may the game check?
	CHECKING,     // asking GitHub
	OFFER,        // a newer version, which this build can install
	NOTE,         // a newer version, but this build can't install itself (a 3DSX with no known path, a read-only folder)
	DOWNLOADING,  // fetching and installing it
	READY,        // installed: restart (or close to HOME) to finish
	LATEST,       // Check Now found nothing newer
	FAILED,       // Check Now got no answer, or a download didn't finish
};

void boot(const char* version);  // after the save loads
void tick();                     // every frame
State state();
bool wantsSign();                // something the player should see now
bool manual();                   // started from Check Now
const char* newVersion();        // the release's tag, without its "v"
const char* notes();             // its changelog, plain text
float progress();                // the download, 0 to 1
bool failedDownload();           // FAILED after a download rather than a check

void consent(bool yes);          // the first-start question
void checkNow();                 // Options: look right away and report what was found
void later();                    // not this version: ask again only for a newer one
void install();                  // Update Now
void dismiss();                  // OK on LATEST, FAILED or NOTE
void restart();                  // READY: the port starts the new version once the game has closed
}  // namespace update
