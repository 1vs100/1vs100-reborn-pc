#pragma once
#include <string>

// Dialogue is one voice at a time; Xbox music/effects keep their own mixer.
bool PlayLocalDialogue(const std::string& cue);
void StopLocalDialogue();
