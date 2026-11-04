// Display options the renderer reads while presenting.
#pragma once

namespace onevs100::render::display {

// Render target size as a percentage of the guest surface.
int ResolutionScale();

// Copy of the above for the surfaces the guest renders shadows into.
int ShadowScale();

// Draw the frame counter over the presented image.
bool FpsCounter();

// Width over height of the presented image.
float AspectRatio();

// Frames per second the swap paces the title to; 0 leaves it unpaced.
int FrameLimit();

}  // namespace onevs100::render::display
