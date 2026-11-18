#pragma once
// Java (J2ME) games: the "j2me" engine of packages (app_j2me.cpp). The class library is the runtime package
// dev.espos.j2me (classlib.jar); each game package holds game.jar (+ icon.jpg).
namespace j2me_app {
void open(const char* pkgId, const char* arg);
}
