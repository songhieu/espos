#pragma once
// Making "my widgets" on the device: a sticky note (text, colour, size), a countdown (title,
// date, colour), quotes (one a day) and a checklist. Saved in s3.mywidgets (synced with the web).
namespace mywid {

using Done = void (*)(const char* id, int size);
void create(const char* type, Done done, int size);  // type: note | count | quote | list
void edit(const char* id, Done done);

}  // namespace mywid
