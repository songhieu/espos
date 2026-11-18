#pragma once
// PawBoop's ten dogs (hand-off 3, dogs.json): each dog is a package of its own, an add-on of PawBoop
// (vn.pawboop.dog.<key>: pet.pbs, run.pbs, voice.pba, assist.pba, icon.jpg), installed from the store, usually on
// the memory card. The chosen one is the pet, the runner of the run game and the voice of the dog assistant.
// Setting s3.pet_dog (synced with the web like every s3.* setting): the dog's key; "" or a dog that is not
// installed = the first installed one in the game's order.
#include <Arduino.h>

namespace pet {
namespace dogs {

struct Info {
  const char* key;   // "bao", ... (dogs.json order)
  const char* name;  // "Bao", ... (shown as is: the dogs' names are names)
};
constexpr int COUNT = 10;
extern const Info ALL[COUNT];

const Info* byKey(const char* key);
String pkgId(const char* key);   // "vn.pawboop.dog.<key>"
bool installed(const char* key);  // installed and readable (its card is in)
int installedCount();
const Info* chosen();            // nullptr: no dog installed
const char* name();              // the chosen dog's name ("PawBoop" when none)
void choose(const char* key);    // stores s3.pet_dog; the packs follow at the next pk::open()
uint32_t version();              // changes when the dog to use changes (setting, install, removal, card)

}  // namespace dogs
}  // namespace pet
