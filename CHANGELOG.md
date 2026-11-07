# Changelog

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
