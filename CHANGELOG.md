# Changelog

## 1.1.0

Saves from 1.0 carry over: scores, coins, unlocks and settings are kept. Zen and Combo Rush play by new rules, so their old best scores are cleared once, the first time 1.1 opens your save, and start fresh.

### New

- **The 3DSX version updates itself:** it downloads the new version from GitHub, goes back to the Homebrew Launcher, and the next start is the new version. This works from 1.1 on, so copy 1.1's 3DSX over by hand once. The CIA version installs updates as before.
- **Mixer:** a sign on Pick a Game with rules that combine on any mode: Low Gravity, Giant Fruit, Tiny Fruit, Mirror, Double Bombs and Sides Only. Runs with any rule on keep their own best scores. Daily Fruit and Beat Slice always play without them.
- **Pass and Play:** 2 to 4 players take one run each on the same console, in Classic, Arcade, Frenzy, Sudden Death, Minefield or Combo Rush. Each player has a colour and a name typed on the system keyboard, and a results board shows who won. These runs don't change your bests.
- **New trick fruit:** a banana bunch that breaks into bananas you can slice again, a pomegranate whose seeds add to the combo when caught in the same swipe, and a rare piñata that takes several hits, pays candy points and then drops coins. Dragon fruit now splits into three pieces. Each has a Fruit Journal page.
- **3D coins** that flip as they fly, in the 3D art style.
- **Latency Test** in Audio settings: a metronome ticks and you slice a fruit in time with it, with the stylus or A. It sets **Beat Timing**, which moves Beat Slice's beat to match your timing. Beat Timing can also be set by hand.
- **Seasonal boards:** late October, December and spring bring pumpkins, snow or blossom in place of a free board, never one you bought. Seasonal Boards in Advanced Visuals turns them off.
- **Shop:** three boards (Pumpkin Patch, Spring Blossom, Bamboo Mat), three trails (Sunset, Candy Cane, Aurora), a Splats tab with six juice colours, a Skins tab with Solid Gold and Clear Glass fruit, and seven songs by NickBin and friends. Each song's description names its artists and genre, and every song plays in Beat Slice.

### Changed

- **Zen is endless:** no timer and no game over. Finish Run in the pause menu ends it and saves your score.
- **Combo Rush is reworked:** only combos score, and a bigger combo is worth more per fruit. Fruit comes faster as your combo grows. One bomb or an empty combo meter ends the run, and there is no clock.
- **Arcade** throws more trick fruit and power-ups, starting earlier in the run.
- **Pick a Game** is split into pages of six signs, with arrows on each side. Left and right turn the page too, and the last page wraps back to the first.
- **Options** has five signs: Audio, Visuals, Gameplay and Controls, Accessibility, and Scores and Data. Fine tuning and resets moved to an Advanced page at the bottom of each one.
- **Scrolling:** the Shop, the Fruit Journal, the update notes and the Beat Slice track list scroll by dragging with the stylus. A drag never counts as picking something.
- The Shop shows six items at a time, and only the items scroll.

### Fixed

- Fruit no longer shows on top of the pause menu's options.

## 1.0.0

The first full release. Saves from 0.4 carry over: your scores and settings are kept, and coins start at 0. 0.4 can't update itself, so install 1.0 by hand; from 1.0 on the CIA version can update itself.

### New

- **Menus you slice.** Menu choices are fruit: swipe across one with the stylus to pick it, or move the selector and press A. Each choice always has the same fruit. The top screen describes the selected one.
- **Coins and the Shop.** Coins fly in now and then (5 points and 5 coins each, plus 1 coin per 100 points). Spend them on board themes, blade trails and six recorded music tracks, tried on before you buy.
- **Daily Fruit:** one 60-second run a day, the same for everyone on every platform, with a calendar, a streak and a daily coin bonus.
- **Beat Slice:** 60 seconds to music, where only slices on the beat score. The board's edges glow when a slice will count. A track list holds every track you own.
- **Practice run** on the first Play, skippable and replayable from How to Play.
- **Fruit Journal** with milestones that pay coins, and a **Stats** page.
- **New fruit:** banana, mango, cherry and dragon fruit. **Trick fruit:** spiky (stuns the blade), grapes (burst), honey (drags), jelly (bounces) and chili (a rush of double points).
- **New Arcade power-ups:** Magnet, Wide Blade, Bomb Shield and +5 Seconds.
- **Slow motion** on a 10-fruit swipe, and a new best replays your best swipe on the result sign.
- **Accessibility:** UI Scale, Larger Text, High Contrast, Colour-blind Mode, Calm Effects, Slice Assist, Game Speed and Sound Captions.
- **Circle Pad slicing:** the Circle Pad moves the blade point; hold L, R or A to slice, or flick.
- **Update check:** with your OK, the game looks on GitHub over Wi-Fi once a day for a newer release and shows what's new. The CIA version installs it while it downloads; the 3DSX version says a new version is out. Nothing about you is sent.
- **Splash lines** over the logo, a new one each time.
- **New bomb blasts:** a fireball, a shockwave, smoke and flying shards.

### Changed

- Text that would run past the edge of its sign is made smaller to fit.
- **Auto music** shuffles: when a track ends, another one you own starts. A track you pick loops. Music loads in the background and starts when it's ready, in about a second.
- **Blade and Trail** are separate: Blade sets the colour of the cutting edge, Trail is the Shop streak behind it.
- Letter outlines no longer cut into the letter beside them.
- The 3D menu labels sway gently instead of spinning, and are easier to read.
- The Shop picks an item when you click or tap it, not when you point at it.
- Every back arrow is a berry you slice, the Shop's, the Journal's and the Options pages' too.
- The lessons leave the Frenzy meter and the COMBO count off until the Frenzy lesson, which only ends once you have started a Frenzy.
- The Shop shows what an item costs against your coins, or that it is already yours. Options pages without a preview show the logo.
- Swipe and combo popups that land together are stacked instead of overlapping.
- The README no longer says the game was only tested in an emulator: 0.4.0 ran well on a real New 2DS XL.

### Fixed

- **No more slowdowns from music.** Music now loads in the background, so a slow cartridge or SD card read never holds up a frame. Reading music used to take up to a tenth of a second at a time.
- The Fruit Journal shows its fruit pictures with 3D Models chosen, not only with 2D Stickers.
- The coin counter in a game sits beside the score instead of on top of it.
- A tap with the stylus picks a menu choice, so the game modes no longer need a slice to start.
- **Picked music now plays.** Choosing a track in Options or trying one in the Shop switches to it as soon as it has loaded; before, the old track could keep playing. The first song after starting the game also plays properly.
- **Daily Fruit's calendar** counts your own days: Day 1 is the first day you play, and today's box moves along the row each day instead of always sitting on the seventh. Day 8 starts the next row.
- The pause screen no longer shows a lesson or "TRY AGAIN" line behind the PAUSED sign.

### Known issues

- The black background a real console showed in long Frenzy runs on 0.4 is not proven fixed. It has never appeared in an emulator, so please report it if you see it.

## 0.4.0-alpha

### Fixed

- Long combos no longer get louder and distort. Every combo sound now hits at the level of the loudest slices, a combo landing on another one drops to keep the pair even, the bomb and the Sunburst burst are quieter, and new sounds make room when a lot is already playing.
- Sounds start sooner: silence at the front of the swish, combo and Fruit Frenzy sounds is trimmed, up to 100 ms.
- Menu signs click the moment you touch them, instead of when you let go. They still act on release, so sliding off cancels.
- Frenzy could run forever. A bomb in the last 5 seconds took 5 seconds off a clock that was already nearly empty, so it stopped at 0:00 and the Sunburst Fruit never came. It now comes straight away.
- Frenzy is lighter and stops drawing before the GPU command list fills: ready-made halves, at most 14 fruit in the air, 24 halves, half the juice, no soft shadows and a slower Fruit Frenzy shower.
- Quitting to the Main Menu from Pause now keeps your best score and fruit count.
- A strike can't land during the first 3.5 seconds after a Fruit Frenzy, when its shower is still falling.
- Bombs already in flight fizzle out when a Fruit Frenzy starts.
- The save keeps the old copy if the SD card is full or a write fails.
- Signs no longer stretch: corners and screws keep their shape at any size.
- Headers are centred, How to Play text stays clear of the screws, the bomb art is no longer clipped, and the blade has a pointed tip.
- Dragging up or down from a slider scrolls the list instead of moving the slider.

### Changed

- Everyone starts fresh: settings and best scores from earlier versions are not carried over.
- The Fruit Frenzy meter never stops draining, so a Frenzy is harder to reach, and an empty meter ends your COMBO. Difficulty sets how fast it drains and how much a slice adds. Its label only shows during a Frenzy.
- Rebalanced throws. Minefield tops out near 65% bombs instead of 90%, with smaller waves. Combo Rush has real bombs, Hard and Insane can throw two bombs in a wave (never in Sudden Death), and Sudden Death throws a little faster.
- The Sunburst Fruit rises to the middle of the screen.
- Credits list the libraries, build tools and toolkits used to make the game.
- The Top Screen Fruit setting is gone. It is always on.

### Known issues

- The black background a real console showed in long Frenzy runs is not proven fixed. The endless-Frenzy bug above fits part of that report, but the black background has never appeared in an emulator.

## 0.3.0-alpha

3D fruit with a 2D or 3D art style, stereo depth for both, and New 3DS second-core work. Menus split into sections with Play, Options and Credits, plus D-pad and Circle Pad navigation and a pause menu. Frenzy mode and more music.

## 0.2.0-alpha

Spacing, the finale fruit, Fever, options and new audio.

## 0.1.0-alpha

First release.
