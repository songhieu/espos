#pragma once
// The clips' roles and chains (game.json -> clips): what she may play for a mood or an action, and what
// follows a clip (eat -> lick, bath -> shake, sleep -> wake, bone_get -> bone_chew). The script follows the
// recorded clips: she never does anything a clip does not show.
namespace pet {
namespace brain {

struct ClipInfo {
  const char* name;
  const char* role;  // idle, hungry, sleepy, bored, pet, play, feed, wash, nap, or "" (only as a reaction)
  const char* next;  // the clip that follows, or ""
};
const ClipInfo* info(const char* name);
// A clip of that role at random, never the same one twice in a row (nullptr when the role has none).
const char* byRole(const char* role);
// She is "on her own": the clip has role idle, hungry, sleepy or bored.
bool ownRole(const char* role);

}  // namespace brain
}  // namespace pet
