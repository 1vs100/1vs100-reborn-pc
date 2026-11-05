// Display options the renderer reads while presenting.
#pragma once

#include <cstdint>

namespace onevs100::render::display {

// Render target size as a percentage of the guest surface.
int ResolutionScale();

// Copy of the above for the surfaces the guest renders shadows into.
int ShadowScale();

// Show the guest frame rate in the debug overlay at startup.
bool FpsCounter();

// Guest thread, at each Swap.
void CountFrame();

// Swaps since start.
uint64_t FrameCount();

// Width over height of the presented image.
float AspectRatio();

// Frames per second the swap paces the title to; 0 leaves it unpaced.
int FrameLimit();

}  // namespace onevs100::render::display
