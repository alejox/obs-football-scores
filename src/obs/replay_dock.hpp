// OBS adapter: the "Football · Replays" dock. It follows the match the linked sources
// show, lists its plays and drives the replay cursor the pitch, the commentary, the
// timeline and the momentum read. UI thread.
#pragma once

namespace fscore::obs {

// From obs_module_load: adds the dock and its hotkeys.
void create_replay_dock();
// When OBS starts closing (OBS_FRONTEND_EVENT_EXIT): stops the dock's feeds and hotkeys
// while Qt and the services still exist.
void shutdown_replay_dock();

} // namespace fscore::obs
