#pragma once
#include "main.h"

// Viewers of the live stream who logged in with a website token get a camera entity in the game,
// placed where they're looking from. The demo stream always includes the cameras, so viewers can
// see each other. Players only see them after turning them on with .showspecs.

#define SPEC_CAMERA_CLASS "hltv_camera"
#define SPEC_CAMERA_MODEL "models/hlcoop_v2/camera_v2.mdl"

// true for camera entities, which are recorded even though they're hidden from most players
bool isSpecCamera(const edict_t& ed);

#ifdef HLCOOP_BUILD
void specCamerasMapInit(); // precaches the model
void specCamerasMapEnd(); // the entities are about to be removed
void updateSpecCameras(); // creates, moves and removes cameras to match the stream viewers
bool cmd_showspecs(CBasePlayer* plr, const CommandArgs& args);
#endif
