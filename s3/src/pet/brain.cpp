#pragma GCC optimize("Os")  // cold code (events, saves, ticks): size first (s3-perf, 2026-11-20)
#include "brain.h"

#include <Arduino.h>

#include <map>
#include <string>

#include "packs.h"

namespace pet {
namespace brain {
namespace {

const ClipInfo CLIPS[] = {
    {"happy", "idle", ""},       {"alert", "idle", ""},         {"giggle", "pet", ""},
    {"ear_rub", "pet", ""},      {"belly", "pet", ""},          {"plead", "bored", ""},
    {"yuck", "", ""},            {"sulk", "hungry", ""},        {"sick", "", ""},
    {"lick", "", ""},            {"eat", "feed", "lick"},       {"bath", "wash", "shake"},
    {"shake", "", ""},           {"sleep", "nap", "wake"},      {"wake", "", ""},
    {"lie_down", "sleepy", ""},  {"ball_fetch", "play", ""},    {"ball_ready", "bored", ""},
    {"bone_get", "play", "bone_chew"}, {"bone_chew", "", ""},   {"beg", "hungry", ""},
    {"tug", "play", ""},         {"frisbee", "play", ""},       {"high_five", "pet", ""},
    {"sit_spin", "play", ""},    {"stand", "idle", ""},         {"nudge", "idle", ""},
    {"look_up", "idle", ""},     {"pounce", "idle", ""},
};

std::map<std::string, const char*> lastOf;

}  // namespace

const ClipInfo* info(const char* name) {
  if (!name) return nullptr;
  for (const ClipInfo& c : CLIPS)
    if (!strcmp(c.name, name)) return &c;
  return nullptr;
}

const char* byRole(const char* role) {
  const char* pick[12];
  int n = 0;
  for (const ClipInfo& c : CLIPS)
    if (!strcmp(c.role, role) && pk::clip(c.name) && n < 12) pick[n++] = c.name;
  if (!n) return nullptr;
  const char* last = lastOf[role];
  const char* p;
  do p = pick[random(n)];
  while (n > 1 && p == last);
  lastOf[role] = p;
  return p;
}

bool ownRole(const char* role) {
  return role && (!strcmp(role, "idle") || !strcmp(role, "hungry") || !strcmp(role, "sleepy") || !strcmp(role, "bored"));
}

}  // namespace brain
}  // namespace pet
