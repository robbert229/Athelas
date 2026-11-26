// The relaunch handover off Windows (handover.cpp's twin): none, on purpose. kernel::Relaunch execs the next image in
// the same process here, so the old window is gone before the new one exists and there is no instance left to keep
// a picture up or to tell. A real handover needs the old process to live on: fork and exec the child with a pipe's
// write end (or an eventfd) it writes once its first frames are up, and the parent waiting on it with its window
// shown. That also changes the process id a launcher waits on. ROTK has one image and never relaunches; The Two
// Towers and The Fellowship of the Ring do, and want it when they come to Linux.
#include "app/app.h"

namespace app {

bool StartHandover() { return false; }
bool HandingOver() { return false; }
void FinishHandover() {}

}  // namespace app
