//Copyright Paul Reiche, Fred Ford. 1992-2002

/*
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#include "planets.h"

#include "scan.h"
#include "lander.h"
#include "../colors.h"
#include "../element.h"
#include "../settings.h"
#include "../controls.h"
#include "../sounds.h"
#include "../gameopt.h"
#include "../shipcont.h"
#include "../setup.h"
#include "../uqmdebug.h"
#include "../resinst.h"
#include "../nameref.h"
#include "../starmap.h"
#include "../util.h"
#include "options.h"
#include "libs/graphics/gfx_common.h"


// PlanetOrbitMenu() items
enum PlanetMenuItems
{
	// XXX: Must match the enum in menustat.h
	SCAN = 0,
	STARMAP,
	EQUIP_DEVICE,
	CARGO,
	ROSTER,
	GAME_MENU,
	NAVIGATION,
};

CONTEXT PlanetContext;
		// Context for rotating planet view and lander surface view
BOOLEAN actuallyInOrbit = FALSE;
		// For determining if the player is in actual scanning orbit

static void
CreatePlanetContext (void)
{
	CONTEXT oldContext;
	RECT r;

	assert (PlanetContext == NULL);

	// PlanetContext rect is relative to SpaceContext
	oldContext = SetContext (SpaceContext);
	GetContextClipRect (&r);

	PlanetContext = CreateContext ("PlanetContext");
	SetContext (PlanetContext);
	SetContextFGFrame (Screen);
	r.extent.height -= MAP_HEIGHT + MAP_BORDER_HEIGHT;
	SetContextClipRect (&r);

	SetContext (oldContext);
}

static void
DestroyPlanetContext (void)
{
	if (PlanetContext)
	{
		DestroyContext (PlanetContext);
		PlanetContext = NULL;
	}
}

void
DrawScannedObjects (BOOLEAN Reversed)
{
	HELEMENT hElement, hNextElement;

	for (hElement = Reversed ? GetTailElement () : GetHeadElement ();
			hElement; hElement = hNextElement)
	{
		ELEMENT *ElementPtr;

		LockElement (hElement, &ElementPtr);
		hNextElement = Reversed ?
				GetPredElement (ElementPtr) :
				GetSuccElement (ElementPtr);

		if (ElementPtr->state_flags & APPEARING)
		{
			STAMP s;

			s.origin = ElementPtr->current.location;
			s.frame = ElementPtr->next.image.frame;
			DrawStamp (&s);
		}

		UnlockElement (hElement);
	}
}

void
DrawPlanetSurfaceBorder (void)
{
	CONTEXT oldContext;
	RECT oldClipRect;
	RECT clipRect;
	RECT r;

	oldContext = SetContext (SpaceContext);
	GetContextClipRect (&oldClipRect);

	// Expand the context clip-rect so that we can tweak the existing border
	clipRect = oldClipRect;
	clipRect.corner.x -= RES_SCALE (1);
	clipRect.extent.width += RES_SCALE (2);
	clipRect.extent.height += RES_SCALE (1);
	SetContextClipRect (&clipRect);

	BatchGraphics ();

	// Border bulk
	SetContextForeGroundColor (
			BUILD_COLOR (MAKE_RGB15 (0x0A, 0x0A, 0x0A), 0x08));
	r.corner.x = 0;
	r.corner.y = clipRect.extent.height - MAP_HEIGHT - MAP_BORDER_HEIGHT;
	r.extent.width = clipRect.extent.width;
	r.extent.height = MAP_BORDER_HEIGHT - RES_SCALE (2);
	DrawFilledRectangle (&r);

	SetContextForeGroundColor (SIS_BOTTOM_RIGHT_BORDER_COLOR);
	
	// Border top shadow line
	r.extent.width -= RES_SCALE (1);
	r.extent.height = RES_SCALE (1);
	r.corner.x = RES_SCALE (1);
	r.corner.y -= RES_SCALE (1);
	DrawFilledRectangle (&r);
	
	// XXX: We will need bulk left and right rects here if MAP_WIDTH changes

	// Right shadow line
	r.extent.width = RES_SCALE (1);
	r.extent.height = MAP_HEIGHT + RES_SCALE (2);
	r.corner.y += MAP_BORDER_HEIGHT - RES_SCALE (1);
	r.corner.x = clipRect.extent.width - RES_SCALE (1);
	DrawFilledRectangle (&r);

	SetContextForeGroundColor (SIS_LEFT_BORDER_COLOR);
	
	// Left shadow line
	r.corner.x -= MAP_WIDTH + RES_SCALE (1);
	DrawFilledRectangle (&r);

	// Border bottom shadow line
	r.extent.width = MAP_WIDTH + RES_SCALE (2);
	r.extent.height = RES_SCALE (1);
	DrawFilledRectangle (&r);

	if (optSuperPC == OPT_PC)
	{
		r.corner.x = RES_SCALE (UQM_MAP_WIDTH - SC2_MAP_WIDTH)
				- SIS_ORG_X + RES_SCALE (1);
		r.corner.y = clipRect.extent.height - MAP_HEIGHT - RES_SCALE (1);
		r.extent.width = RES_SCALE (1);
		r.extent.height = MAP_HEIGHT;
		SetContextForeGroundColor (SIS_BOTTOM_RIGHT_BORDER_COLOR);
		DrawFilledRectangle (&r);
		r.corner.x += RES_SCALE (1);
		r.extent.width = RES_SCALE (4);
		r.corner.y -= RES_SCALE (1);
		r.extent.height += RES_SCALE (2);
		SetContextForeGroundColor (
				BUILD_COLOR_RGBA (0x52, 0x52, 0x52, 0xFF));
		DrawFilledRectangle (&r);
		r.corner.x += RES_SCALE (4);
		r.extent.width = RES_SCALE (1);
		r.corner.y += RES_SCALE (1);
		r.extent.height -= RES_SCALE (2);
		SetContextForeGroundColor (SIS_LEFT_BORDER_COLOR);
		DrawFilledRectangle (&r);

		DrawBorder (30, FALSE);
	}
	else
		DrawBorder (10, FALSE);
	
	UnbatchGraphics ();

	SetContextClipRect (&oldClipRect);
	SetContext (oldContext);
}

typedef enum
{
	DRAW_ORBITAL_FULL,
	DRAW_ORBITAL_WAIT,
	DRAW_ORBITAL_UPDATE,
	DRAW_ORBITAL_FROM_STARMAP,

} DRAW_ORBITAL_MODE;

static void
DrawOrbitalDisplay (DRAW_ORBITAL_MODE Mode)
{
	RECT r;

	actuallyInOrbit = TRUE;

	SetContext (SpaceContext);
	GetContextClipRect (&r);

	BatchGraphics ();
	
	if (Mode != DRAW_ORBITAL_UPDATE)
	{
		SetTransitionSource (NULL);
		DrawSISFrame ();
		DrawSISMessage (NULL);
		DrawSISTitle (GLOBAL_SIS (PlanetName));
		DrawStarBackGround ();
		DrawPlanetSurfaceBorder ();
	}

	if (Mode == DRAW_ORBITAL_WAIT)
	{
		STAMP s, ss;
		BOOLEAN never = FALSE;

		SetContext (GetScanContext (NULL));

		s.origin.x = 0;
		s.origin.y = 0;
		s.frame = SetAbsFrameIndex (CaptureDrawable
				(LoadGraphic (ORBENTER_PMAP_ANIM)), never);

		if (optSuperPC == OPT_PC)
			s.origin.x -= RES_SCALE ((UQM_MAP_WIDTH - SC2_MAP_WIDTH) / 2);

		if (never)
		{
			PLANET_DESC *pPlanetDesc;
			PLANET_ORBIT *Orbit = &pSolarSysState->Orbit;
			int PlanetScale = RES_BOOL (319, 512);
			int PlanetRescale = 1275;

			pPlanetDesc = pSolarSysState->pOrbitalDesc;
			GeneratePlanetSurface (
					pPlanetDesc, NULL, PlanetScale, PlanetScale);
			ss.origin.x = RES_SCALE (ORIG_SIS_SCREEN_WIDTH >> 1);
			ss.origin.y = RES_SCALE (191);
			
			ss.frame = RES_BOOL (Orbit->SphereFrame, CaptureDrawable (
					RescaleFrame (
						Orbit->SphereFrame, PlanetRescale, PlanetRescale
					)));

			DrawStamp (&ss);
			DestroyDrawable (ReleaseDrawable (ss.frame));
		}

		DrawStamp (&s);
		DestroyDrawable (ReleaseDrawable (s.frame));
	}
	else if (Mode == DRAW_ORBITAL_FULL)
	{
		DrawDefaultPlanetSphere ();
		DrawMenuStateStrings (PM_SCAN, SCAN);
	}
	else if (Mode == DRAW_ORBITAL_FROM_STARMAP)
	{
		DrawDefaultPlanetSphere ();
		DrawMenuStateStrings (PM_SCAN, STARMAP);
	}
	else
		DrawMenuStateStrings (PM_SCAN, SCAN);

	if (Mode != DRAW_ORBITAL_WAIT)
	{
		SetContext (GetScanContext (NULL));
		DrawPlanet (0, BLACK_COLOR);
		if (optSuperPC == OPT_PC)
			InitPCLander();// PC-DOS lander UI pops after "Entering planetary orbit" frame
	}

	if (Mode != DRAW_ORBITAL_UPDATE)
	{
		ScreenTransition (optIPScaler, &r);
	}

	UnbatchGraphics ();

	// for later RepairBackRect()
	
	LoadIntoExtraScreen (&r);
}

// Initialise the surface graphics, and start the planet music.
// Called from the GenerateFunctions.generateOribital() function
// (when orbit is entered; either from IP, or from loading a saved game)
// and when "starmap" is selected from orbit and then cancelled;
// also after in-orbit comm and after defeating planet guards in combat.
// SurfDefFrame contains surface definition images when a planet comes
// with its own bitmap (currently only for Earth)
void
LoadPlanet (FRAME SurfDefFrame)
{
	bool WaitMode = !(LastActivity & CHECK_LOAD);
	PLANET_DESC *pPlanetDesc;

#ifdef DEBUG
	if (disableInteractivity)
		return;
#endif

	assert (pSolarSysState->InOrbit && !pSolarSysState->TopoFrame);

	CreatePlanetContext ();

	if (WaitMode)
	{
		DrawOrbitalDisplay (DRAW_ORBITAL_WAIT);
	}

	StopMusic ();

	pPlanetDesc = pSolarSysState->pOrbitalDesc;
	GeneratePlanetSurface (pPlanetDesc, SurfDefFrame, NULL, NULL);
	SetPlanetMusic (pPlanetDesc->data_index & ~PLANET_SHIELDED);
	GeneratePlanetSide ();

	if (optIPScaler != OPT_3DO)
		SleepThread (ONE_SECOND);

	if (!PLRPlaying ((MUSIC_REF)~0))
		PlayMusic (LanderMusic, TRUE, 1);

	if (WaitMode)
	{
		if (optIPScaler == OPT_3DO)
			ZoomInPlanetSphere ();
		DrawOrbitalDisplay (DRAW_ORBITAL_UPDATE);
	}
	else
	{	// to fix moon suffix on load
		if (worldIsMoon (pSolarSysState, pSolarSysState->pOrbitalDesc))
		{
			if (!(GetNamedPlanetaryBody ()) && isPC (optWhichFonts)
					&& (pSolarSysState->pOrbitalDesc->data_index
							< PRECURSOR_STARBASE
					&& pSolarSysState->pOrbitalDesc->data_index
							!= DESTROYED_STARBASE
					&& pSolarSysState->pOrbitalDesc->data_index
							!= PRECURSOR_STARBASE))
			{
				snprintf (
						(GLOBAL_SIS (PlanetName))
						+ strlen (GLOBAL_SIS (PlanetName)),
						3, "-%c%c", 'A'
						+ moonIndex (
							pSolarSysState, pSolarSysState->pOrbitalDesc),
							'\0');
			}
		}
	 	DrawOrbitalDisplay (DRAW_ORBITAL_FULL);
	}
}

void
FreePlanet (void)
{
	COUNT i, j;
	PLANET_ORBIT *Orbit = &pSolarSysState->Orbit;

	UninitSphereRotation ();

	StopMusic ();

	for (i = 0; i < ARRAY_SIZE (pSolarSysState->PlanetSideFrame); ++i)
	{
		DestroyDrawable (
				ReleaseDrawable (pSolarSysState->PlanetSideFrame[i]));
		pSolarSysState->PlanetSideFrame[i] = 0;
	}

//    FreeLanderData ();

	DestroyStringTable (ReleaseStringTable (pSolarSysState->XlatRef));
	pSolarSysState->XlatRef = 0;
	DestroyDrawable (ReleaseDrawable (pSolarSysState->TopoFrame));
	pSolarSysState->TopoFrame = 0;

	if (optScanStyle == OPT_PC)
	{
		COUNT k;

		for (k = 0; k < NUM_SCAN_TYPES; k++)
		{
			DestroyDrawable (
					ReleaseDrawable (pSolarSysState->ScanFrame[k]));
			pSolarSysState->ScanFrame[k] = 0;
		}
	}
	Orbit->scanType = 0;

	DestroyColorMap (ReleaseColorMap (pSolarSysState->OrbitalCMap));
	pSolarSysState->OrbitalCMap = 0;

	HFree (Orbit->lpTopoData);
	Orbit->lpTopoData = 0;
	DestroyDrawable (ReleaseDrawable (Orbit->TopoZoomFrame));
	Orbit->TopoZoomFrame = 0;
	DestroyDrawable (ReleaseDrawable (Orbit->SphereFrame));
	Orbit->SphereFrame = NULL;

	DestroyDrawable (ReleaseDrawable (Orbit->TintFrame));
	Orbit->TintFrame = 0;
	Orbit->TintColor = BLACK_COLOR;

	DestroyDrawable (ReleaseDrawable (Orbit->ObjectFrame));
	Orbit->ObjectFrame = 0;
	DestroyDrawable (ReleaseDrawable (Orbit->WorkFrame));
	Orbit->WorkFrame = 0;

	HFree (Orbit->TopoColors);
	Orbit->TopoColors = NULL;
	HFree (Orbit->ScratchArray);
	Orbit->ScratchArray = NULL;
	if (Orbit->map_rotate && Orbit->light_diff)
	{
		for (j = 0; j <= MAP_HEIGHT; j++)
		{
			HFree (Orbit->map_rotate[j]);
			HFree (Orbit->light_diff[j]);
		}
	}

	HFree (Orbit->map_rotate);
	Orbit->map_rotate = NULL;
	HFree (Orbit->light_diff);
	Orbit->light_diff = NULL;

	DestroyStringTable (ReleaseStringTable (
			pSolarSysState->SysInfo.PlanetInfo.DiscoveryString
			));
	pSolarSysState->SysInfo.PlanetInfo.DiscoveryString = 0;
	FreeLanderFont (&pSolarSysState->SysInfo.PlanetInfo);

	// Need to make sure our own CONTEXTs are not active because
	// we will destroy them now
	SetContext (SpaceContext);
	DestroyPlanetContext ();
	DestroyScanContext ();
	DestroyPCLanderContext ();

	actuallyInOrbit = FALSE;
}

void
LoadStdLanderFont (PLANET_INFO *info)
{
	info->LanderFont = LoadFont (LANDER_FONT);
	info->LanderFontEff = CaptureDrawable (
			LoadGraphic (LANDER_FONTEFF_PMAP_ANIM));
}

void
FreeLanderFont (PLANET_INFO *info)
{
	DestroyFont (info->LanderFont);
	info->LanderFont = NULL;
	DestroyDrawable (ReleaseDrawable (info->LanderFontEff));
	info->LanderFontEff = NULL;
}

static BOOLEAN
DoPlanetOrbit (MENU_STATE *pMS)
{
	BOOLEAN select = (BOOLEAN)PulsedInputState.menu[KEY_MENU_SELECT];
	BOOLEAN handled;

	if ((GLOBAL (CurrentActivity) & (CHECK_ABORT | CHECK_LOAD))
			|| GLOBAL_SIS (CrewEnlisted) == (COUNT)~0)
		return FALSE;

	// XXX: pMS actually refers to pSolarSysState->MenuState
	handled = DoMenuChooser (pMS, PM_SCAN);
	if (handled)
		return TRUE;

	if (!select)
		return TRUE;

	SetFlashRect (NULL, FALSE);

	switch (pMS->CurState)
	{
		case SCAN:
			ScanSystem ();
			if (GLOBAL (CurrentActivity) & START_ENCOUNTER)
			{	// Found Fwiffo on Pluto
				return FALSE;
			}
			break;
		case EQUIP_DEVICE:
			select = DevicesMenu ();
			if (GLOBAL (CurrentActivity) & START_ENCOUNTER)
			{	// Invoked Talking Pet, a Caster or Sun Device over Chmmr,
				// or a Caster for Ilwrath
				// Going into conversation
				return FALSE;
			}
			break;
		case CARGO:
			CargoMenu ();
			break;
		case ROSTER:
			select = RosterMenu ();
			break;
		case GAME_MENU:
			if (!GameOptions ())
				return FALSE; // abort or load
			break;
		case STARMAP:
		{
			BOOLEAN AutoPilotSet;
			InputFrameCallback *oldCallback;

			// Deactivate planet rotation
			oldCallback = SetInputCallback (NULL);

			RepairSISBorder ();

			AutoPilotSet = StarMap ();
			if (GLOBAL (CurrentActivity) & CHECK_ABORT)
				return FALSE;

			// Reactivate planet rotation
			SetInputCallback (oldCallback);

			if (!AutoPilotSet)
			{	// Redraw the orbital display
				DrawOrbitalDisplay (DRAW_ORBITAL_FROM_STARMAP);//WAS FULL
				break;
			}
			// Fall through !!!
		}
		case NAVIGATION:
			return FALSE;
	}

	if (!(GLOBAL (CurrentActivity) & CHECK_ABORT))
	{
		if (select)
		{	// 3DO menu jumps to NAVIGATE after a successful submenu run
			if (optWhichMenu != OPT_PC)
				pMS->CurState = NAVIGATION;
			if (pMS->CurState != STARMAP)
				DrawMenuStateStrings (PM_SCAN, pMS->CurState);
		}
		SetFlashRect (SFR_MENU_3DO, FALSE);
	}

	return TRUE;
}

static void
on_input_frame (void)
{
	RotatePlanetSphere (TRUE, NULL, TRANSPARENT);
}

void
PlanetOrbitMenu (void)
{
	MENU_STATE MenuState;
	InputFrameCallback *oldCallback;

	memset (&MenuState, 0, sizeof MenuState);
	
	SetFlashRect (SFR_MENU_3DO, FALSE);

	MenuState.CurState = SCAN;
	SetMenuSounds (MENU_SOUND_ARROWS, MENU_SOUND_SELECT);
	oldCallback = SetInputCallback (on_input_frame);

	MenuState.InputFunc = DoPlanetOrbit;
	DoInput (&MenuState, TRUE);

	SetInputCallback (oldCallback);

	SetFlashRect (NULL, FALSE);
	if (!(GLOBAL(CurrentActivity) & CHECK_LOAD))
		DrawMenuStateStrings (PM_STARMAP, -NAVIGATION);
}
