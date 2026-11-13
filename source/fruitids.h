#pragma once
#include "common.h"

// Fruit kinds, as numbered in game.cpp's kFruit table. Shared with the modes in their own files.
static constexpr int kCoconut = 10;
static constexpr int kFirstNew = 11, kNewNormal = 4;  // banana to dragon fruit
static constexpr int kBanana = 11, kDragon = 14;
enum { kSpiky = 15, kGrapes, kGrape, kHoney, kJelly, kChili, kBunch, kPomegranate, kPinata };
static constexpr int kMelonFruit = 5;
static constexpr int kPinataHits = 5, kPinataMax = 2, kBunchPieces = 3, kPinataCoins = 3;
static constexpr float kPinataFrom = 45.0f;  // seconds into a run before one can appear

// game.cpp's helpers, for the modes in their own files.
int normalFruitOf(Rng& rng);                 // any everyday fruit but the coconut
int trickFruitOf(Mode mode, Rng& rng, bool pinata);
int juiceAmount(int base);                   // particle counts follow the Effects setting
bool drawing3D();                            // the 3D art style is chosen and the models are in
int fruitMeshOf(int fruit);
float acrossScale();                         // sideways speeds set by hand shrink with a narrow field
