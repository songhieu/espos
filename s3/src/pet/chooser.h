#pragma once
// "Choose a dog": the ten dogs of PawBoop as cards (the phone game's portrait, name, character). An installed
// dog is chosen with a tap (pet::switchDog: the pet, the runner and the assistant's voice change); one that is
// not installed downloads with a tap (the store's installer, on the memory card when there is one) and shows
// its progress. Opened by a tap on the dog's name in the game, from the book, or by the game when no dog is in.
namespace pet {
namespace chooser {
void open();
}  // namespace chooser

// Use another dog: the game, its book and the pop-up close, the old dog's pictures and voice go, the new dog's
// packs load and the game opens again (open = true) with it. Widgets follow. Also when the web changed s3.pet_dog.
void switchDog(const char* key, bool open);
}  // namespace pet
