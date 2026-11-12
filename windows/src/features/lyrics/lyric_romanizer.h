#pragma once
#include "lyrics.h"

namespace noitch {
// Uses the system ICU and Japanese IME only for the duration of a lyrics lookup.
void RomanizeLyrics(Lyrics& lyrics);
}
