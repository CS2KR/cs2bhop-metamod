#include "bhop_checkpoint.h"
#include "utils/simplecmds.h"

SCMD(bhop_checkpoint, SCFL_CHECKPOINT)
{
	BhopPlayer *player = g_pBhopPlayerManager->ToPlayer(controller);
	player->checkpointService->SetCheckpoint();
	return MRES_SUPERCEDE;
}

SCMD_LINK(bhop_cp, bhop_checkpoint);
SCMD_LINK(bhop_saveloc, bhop_checkpoint);

SCMD(bhop_teleport, SCFL_CHECKPOINT)
{
	BhopPlayer *player = g_pBhopPlayerManager->ToPlayer(controller);
	player->checkpointService->TpToCheckpoint();
	return MRES_SUPERCEDE;
}

SCMD_LINK(bhop_tp, bhop_teleport);
SCMD_LINK(bhop_loadloc, bhop_teleport);

SCMD(bhop_undo, SCFL_CHECKPOINT)
{
	BhopPlayer *player = g_pBhopPlayerManager->ToPlayer(controller);
	player->checkpointService->UndoTeleport();
	return MRES_SUPERCEDE;
}

SCMD(bhop_prevcp, SCFL_CHECKPOINT)
{
	BhopPlayer *player = g_pBhopPlayerManager->ToPlayer(controller);
	player->checkpointService->TpToPrevCp();
	return MRES_SUPERCEDE;
}

SCMD_LINK(bhop_pcp, bhop_prevcp);
SCMD_LINK(bhop_prevloc, bhop_prevcp);

SCMD(bhop_nextcp, SCFL_CHECKPOINT)
{
	BhopPlayer *player = g_pBhopPlayerManager->ToPlayer(controller);
	player->checkpointService->TpToNextCp();
	return MRES_SUPERCEDE;
}

SCMD_LINK(bhop_ncp, bhop_nextcp);
SCMD_LINK(bhop_nextloc, bhop_nextcp);

SCMD(bhop_setstartpos, SCFL_CHECKPOINT | SCFL_MAP | SCFL_PREFERENCE)
{
	BhopPlayer *player = g_pBhopPlayerManager->ToPlayer(controller);
	player->checkpointService->SetStartPosition();
	return MRES_SUPERCEDE;
}

SCMD_LINK(bhop_ssp, bhop_setstartpos);

SCMD(bhop_clearstartpos, SCFL_CHECKPOINT)
{
	BhopPlayer *player = g_pBhopPlayerManager->ToPlayer(controller);
	player->checkpointService->ClearStartPosition();
	return MRES_SUPERCEDE;
}

SCMD_LINK(bhop_csp, bhop_clearstartpos);
