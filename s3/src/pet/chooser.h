#pragma once
// "Choose a dog": the ten dogs of PawBoop as cards (the phone game's portrait, name, character). An installed
// dog is chosen with a tap (pet::switchDog: the pet, the runner and the assistant's voice change); one that is
// not installed downloads with a tap (the store's installer, on the memory card when there is one) and shows
// its progress. Opened by a tap on the dog's name in the game, from the book, or by the game when no dog is in.
#include <Arduino.h>

namespace pet {
namespace chooser {
void open();
#if ESPOS_ONBOARD
// For the first-boot setup's dog pages (onboard.cpp) as for the cards here: dog i's state line ("Your dog",
// "Download · 6.6 MB", "Downloading 46%"...), its colour and the download's percent (-1: none).
void state(int i, String& what, uint32_t& col, int& pct);
// A tap on dog i: 0 it is installed and readable (the caller switches to it), 1 its download started or is coming,
// -1 refused (a toast or the lock sheet says why).
int pick(int i);
String desc(int i);  // who dog i is (its character, from the store; "" when unknown)
#endif
}  // namespace chooser

// Use another dog: the game, its book and the pop-up close, the old dog's pictures and voice go, the new dog's
// packs load and the game opens again (open = true) with it. Widgets follow. Also when the web changed s3.pet_dog.
void switchDog(const char* key, bool open);
}  // namespace pet
