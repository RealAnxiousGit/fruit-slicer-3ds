# Changelog

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

### Changed

- Text that would run past the edge of its sign is made smaller to fit.
- Swipe and combo popups that land together are stacked instead of overlapping.
- The README no longer says the game was only tested in an emulator: 0.4.0 ran well on a real New 2DS XL.

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
