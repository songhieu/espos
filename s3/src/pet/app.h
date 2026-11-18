#pragma once
// The PawBoop game screen (app.cpp).
namespace pet {
namespace app {
void open();
bool isOpen();  // the game screen exists (it may be under another screen)
bool active();  // it is in front and drawing
void stop();    // no more pictures (the dog is about to change; the screen is closed next)
void devClip(const char* clip);  // tests: the dog plays this clip now (apps::open("pet", "clip:pounce"))
// Coins the run game ("Chạy lấy xu", vn.pawboop.run) paid into her purse: shown arriving (+N xu, her @coin line)
// when the game screen is in front again. Ignored when the game screen is not open.
void arrived(int coins);
}  // namespace app
}  // namespace pet
