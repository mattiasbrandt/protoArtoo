# Surface-owned polling

A surface asks for fresh readings through `window.PASurface` (`data/page_bootstrap.js`). The shell names which surface is on screen. Polling that belongs to any other surface stops. Leaving a surface changes only what the browser asks for: no sequence stops, no output releases, no drive frame is dropped, no latch clears.

`PASurface.poll(attempt, options)` registers one poll owned by the surface that is showing when it is created. `start()` marks it wanted and the reconciler (`syncSurfacePoll`) starts it only while `surfacePollWanted` is true: nobody has named a surface yet, or this poll's owner is the one on screen. `stop()` marks it unwanted. A poll that was running and then stopped because the operator left is stale until a later answer lands. A rejection is reported here and does not mark the surface fresh.

`PASurface.holdUnmount(decide)` lets the showing surface refuse to be taken off screen. `decide` returns true to hold. Sequences registers one (`data/seq.js`) so an unsaved edit asks before the surface goes. `releaseUnmount()` lets the navigation through. `stayOnSurface()` puts the address back. The estop stays on screen behind a hold.

`showing`, `isStale` and `unmountHeld` are the shell's half. A page module does not call them.
