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

#include "scan.h"
#include "../colors.h"
#include "../controls.h"
#include "../menustat.h"
#include "../starmap.h"
#include "../races.h"
#include "../gameopt.h"
#include "../gamestr.h"
#include "../gendef.h"
#include "../globdata.h"
#include "../shipcont.h"
#include "../units.h"
#include "../hyper.h"
#include "../sis.h"
		// for DrawHyperCoords(), DrawStatusMessage()
#include "../settings.h"
#include "../setup.h"
#include "../sounds.h"
#include "../state.h"
#include "../uqmdebug.h"
#include "options.h"
#include "libs/inplib.h"
#include "libs/strlib.h"
#include "libs/graphics/gfx_common.h"
#include "libs/mathlib.h"
#include "libs/memlib.h"
#include "../util.h"
		// For get_fuel_to_sol()
#include <stdlib.h>
#include <ctype.h> 
		// For isdigit()
#include "../build.h"
		// For StartSphereTracking()

typedef enum {
	NORMAL_STARMAP,
	WAR_ERA_STARMAP,
	CONSTELLATION_MAP,
	HOMEWORLDS_MAP,
	RAINBOW_MAP,
	NUM_STARMAPS
} CURRENT_STARMAP_SHOWN;


static POINT cursorLoc;
static POINT mapOrigin;
static int zoomLevel;
static FRAME StarMapFrame;

static BOOLEAN show_war_era_situation;
static CURRENT_STARMAP_SHOWN which_starmap;

static inline long
signedDivWithError (long val, long divisor)
{
	int invert = 0;
	if (val < 0)
	{
		invert = 1;
		val = -val;
	}
	val = (val + ROUNDING_ERROR (divisor)) / divisor;
	return invert ? -val : val;
}

#define MAP_FIT_X ((MAX_X_UNIVERSE + 1) / ORIG_SIS_SCREEN_WIDTH + 1)
#define HD_COMP (IF_HD (2))

static inline COORD
universeToDispx (long ux)
{
	return signedDivWithError (((ux - mapOrigin.x) << zoomLevel)
			* ORIG_SIS_SCREEN_WIDTH, MAX_X_UNIVERSE + MAP_FIT_X)
			+ ((ORIG_SIS_SCREEN_WIDTH - 1) >> 1);
}
#define UNIVERSE_TO_DISPX(ux)  RES_SCALE (universeToDispx(ux))
#define ORIG_UNIVERSE_TO_DISPX(ux)  universeToDispx(ux)

static inline COORD
universeToDispy (long uy)
{
	return signedDivWithError (((mapOrigin.y - uy) << zoomLevel)
			* ORIG_SIS_SCREEN_HEIGHT, MAX_Y_UNIVERSE + 2)
			+ ((ORIG_SIS_SCREEN_HEIGHT - 1) >> 1);
}
#define UNIVERSE_TO_DISPY(uy)  RES_SCALE (universeToDispy(uy))
#define ORIG_UNIVERSE_TO_DISPY(uy)  universeToDispy(uy)

static inline COORD
dispxToUniverse (COORD dx)
{
	return (((long)(dx - ((ORIG_SIS_SCREEN_WIDTH - 1) >> 1))
			* (MAX_X_UNIVERSE + MAP_FIT_X)) >> zoomLevel)
			/ ORIG_SIS_SCREEN_WIDTH + mapOrigin.x;
}
#define DISP_TO_UNIVERSEX(dx) dispxToUniverse(RES_DESCALE (dx))
#define ORIG_DISP_TO_UNIVERSEX(dx) dispxToUniverse(dx)

static inline COORD
dispyToUniverse (COORD dy)
{
	return (((long)(((ORIG_SIS_SCREEN_HEIGHT - 1) >> 1) - dy)
			* (MAX_Y_UNIVERSE + 2)) >> zoomLevel)
			/ ORIG_SIS_SCREEN_HEIGHT + mapOrigin.y;
}
#define DISP_TO_UNIVERSEY(dy) dispyToUniverse(RES_DESCALE (dy))
#define ORIG_DISP_TO_UNIVERSEY(dy) dispyToUniverse(dy)

static BOOLEAN transition_pending;

static void
flashCurrentLocation (POINT *where, BOOLEAN force)
{
	static BOOLEAN redraw = FALSE;
	static BYTE c = 0;
	static int val = -2;
	static POINT universe;
	static TimeCount NextTime = 0;

	if (where)
		universe = *where;

	if (GetTimeCounter () >= NextTime)
	{
		NextTime = GetTimeCounter () + (ONE_SECOND / 16);

		if (c == 0x00 || c == 0x1A)
			val = -val;
		c += val;

		redraw = TRUE;
	}

	if (force || redraw)
	{
		Color OldColor;
		CONTEXT OldContext;
		STAMP s;

		OldContext = SetContext (SpaceContext);

		OldColor = SetContextForeGroundColor (
				BUILD_COLOR (MAKE_RGB15 (c, c, c), c));
		s.origin.x = UNIVERSE_TO_DISPX (universe.x) + HD_COMP;
		s.origin.y = UNIVERSE_TO_DISPY (universe.y) + HD_COMP;
		s.frame = IncFrameIndex (StarMapFrame);
		DrawFilledStamp (&s);
		SetContextForeGroundColor (OldColor);

		SetContext (OldContext);

		redraw = FALSE;
	}
}

static void
DrawCursor (COORD curs_x, COORD curs_y)
{
	STAMP s;

	s.origin.x = curs_x + HD_COMP;
	s.origin.y = curs_y + HD_COMP;
	s.frame = StarMapFrame;

	DrawStamp (&s);
}

static void
DrawReticule (POINT dest, BOOLEAN loc)
{
	STAMP s;

	s.origin = MAKE_POINT (
			UNIVERSE_TO_DISPX (dest.x) + HD_COMP,
			UNIVERSE_TO_DISPY (dest.y) + HD_COMP
		);
	s.frame = SetAbsFrameIndex (MiscDataFrame, 107 + loc);

	DrawStamp (&s);
}

static void
DrawAutoPilot (POINT *pDstPt)
{
	SIZE dx, dy,
				xincr, yincr,
				xerror, yerror,
				cycle, delta;
	POINT pt;
	STAMP s;

	if (!inHQSpace ())
		pt = CurStarDescPtr->star_pt;
	else
	{
		pt.x = LOGX_TO_UNIVERSE (GLOBAL_SIS (log_x));
		pt.y = LOGY_TO_UNIVERSE (GLOBAL_SIS (log_y));
	}

	if (IS_HD)
		s.frame = SetAbsFrameIndex (MiscDataFrame, 106);

	pt.x = UNIVERSE_TO_DISPX (pt.x) + HD_COMP;
	pt.y = UNIVERSE_TO_DISPY (pt.y) + HD_COMP;

	dx = UNIVERSE_TO_DISPX (pDstPt->x) + HD_COMP - pt.x;
	if (dx >= 0)
		xincr = 1;
	else
	{
		xincr = -1;
		dx = -dx;
	}
	dx <<= 1;

	dy = UNIVERSE_TO_DISPY (pDstPt->y) + HD_COMP - pt.y;
	if (dy >= 0)
		yincr = 1;
	else
	{
		yincr = -1;
		dy = -dy;
	}
	dy <<= 1;

	if (dx >= dy)
		cycle = dx;
	else
		cycle = dy;
	delta = xerror = yerror = cycle >> 1;

	SetContextForeGroundColor (
			BUILD_COLOR (MAKE_RGB15 (0x04, 0x04, 0x1F), 0x01));

	delta &= ~1;
	while (delta--)
	{
		if (IS_HD)
		{
			if (delta % 8 == 0 && delta != 0)
			{	// every eighth dot and not the last dot
				s.origin.x = pt.x;
				s.origin.y = pt.y;
				DrawFilledStamp (&s);
			}
		}
		else if (!(delta & 1))
			DrawPoint(&pt);

		if ((xerror -= dx) <= 0)
		{
			pt.x += xincr;
			xerror += cycle;
		}
		if ((yerror -= dy) <= 0)
		{
			pt.y += yincr;
			yerror += cycle;
		}
	}
}

static void
GetSphereRect (FLEET_INFO *FleetPtr, RECT *pRect, RECT *pRepairRect)
{
	long diameter;

	diameter = (long)(FleetPtr->known_strength * SPHERE_RADIUS_INCREMENT);
	pRect->extent.width = UNIVERSE_TO_DISPX (diameter)
			- UNIVERSE_TO_DISPX (0);
	if (pRect->extent.width < 0)
		pRect->extent.width = -pRect->extent.width;
	else if (pRect->extent.width == 0)
		pRect->extent.width = RES_SCALE (1);
	pRect->extent.height = UNIVERSE_TO_DISPY (diameter)
			- UNIVERSE_TO_DISPY (0);
	if (pRect->extent.height < 0)
		pRect->extent.height = -pRect->extent.height;
	else if (pRect->extent.height == 0)
		pRect->extent.height = RES_SCALE (1);

	pRect->corner.x = UNIVERSE_TO_DISPX (FleetPtr->known_loc.x);
	pRect->corner.y = UNIVERSE_TO_DISPY (FleetPtr->known_loc.y);
	pRect->corner.x -= pRect->extent.width >> 1;
	pRect->corner.y -= pRect->extent.height >> 1;

	{
		TEXT t;
		STRING locString;

		t.baseline.x = pRect->corner.x + (pRect->extent.width >> 1);
		t.baseline.y = pRect->corner.y + (pRect->extent.height >> 1) - RES_SCALE (1);
		t.align = ALIGN_CENTER;
		locString = SetAbsStringTableIndex (FleetPtr->race_strings, 1);
		t.CharCount = GetStringLength (locString);
		t.pStr = (UNICODE *)GetStringAddress (locString);
		TextRect (&t, pRepairRect, NULL);
		
		if (pRepairRect->corner.x <= 0)
			pRepairRect->corner.x = RES_SCALE (1);
		else if (pRepairRect->corner.x + pRepairRect->extent.width >=
				SIS_SCREEN_WIDTH)
			pRepairRect->corner.x =
					SIS_SCREEN_WIDTH - pRepairRect->extent.width - RES_SCALE (1);
		if (pRepairRect->corner.y <= 0)
			pRepairRect->corner.y = RES_SCALE (1);
		else if (pRepairRect->corner.y + pRepairRect->extent.height >=
				SIS_SCREEN_HEIGHT)
			pRepairRect->corner.y =
					SIS_SCREEN_HEIGHT - pRepairRect->extent.height - RES_SCALE (1);

		BoxUnion (pRepairRect, pRect, pRepairRect);
		pRepairRect->extent.width += RES_SCALE (1);
		pRepairRect->extent.height += RES_SCALE (1);
	}
}

// For showing the War-Era situation in starmap
static void
GetWarEraSphereRect (COUNT index, COUNT war_era_strengths[],
		POINT war_era_locations[], RECT *pRect, RECT *pRepairRect)
{
	long diameter = (long)(war_era_strengths[index] * 2);

	pRect->extent.width = UNIVERSE_TO_DISPX (diameter)
			- UNIVERSE_TO_DISPX (0);
	if (pRect->extent.width < 0)
		pRect->extent.width = -pRect->extent.width;
	else if (pRect->extent.width == 0)
		pRect->extent.width = RES_SCALE (1);
	pRect->extent.height = UNIVERSE_TO_DISPY (diameter)
			- UNIVERSE_TO_DISPY (0);
	if (pRect->extent.height < 0)
		pRect->extent.height = -pRect->extent.height;
	else if (pRect->extent.height == 0)
		pRect->extent.height = RES_SCALE (1);

	pRect->corner.x = UNIVERSE_TO_DISPX (war_era_locations[index].x);
	pRect->corner.y = UNIVERSE_TO_DISPY (war_era_locations[index].y);
	pRect->corner.x -= pRect->extent.width >> 1;
	pRect->corner.y -= pRect->extent.height >> 1;

	{
		if (pRepairRect->corner.x <= 0)
			pRepairRect->corner.x = RES_SCALE (1);
		else if (pRepairRect->corner.x + pRepairRect->extent.width >=
				SIS_SCREEN_WIDTH)
			pRepairRect->corner.x = SIS_SCREEN_WIDTH
				- pRepairRect->extent.width - RES_SCALE (1);
		if (pRepairRect->corner.y <= 0)
			pRepairRect->corner.y = RES_SCALE (1);
		else if (pRepairRect->corner.y + pRepairRect->extent.height >=
				SIS_SCREEN_HEIGHT)
			pRepairRect->corner.y = SIS_SCREEN_HEIGHT
				- pRepairRect->extent.height - RES_SCALE (1);

		BoxUnion (pRepairRect, pRect, pRepairRect);
		pRepairRect->extent.width += RES_SCALE (1);
		pRepairRect->extent.height += RES_SCALE (1);
	}
}

static void
DrawFuelCircles ()
{
	RECT r;
	long diameter;
	long diameter_no_return;
	POINT corner;
	Color OldColor;
	DWORD OnBoardFuel = GLOBAL_SIS (FuelOnBoard);

	diameter = OnBoardFuel << 1;

	if (!inHQSpace())
		corner = CurStarDescPtr->star_pt;
	else
	{
		corner.x = LOGX_TO_UNIVERSE (GLOBAL_SIS (log_x));
		corner.y = LOGY_TO_UNIVERSE (GLOBAL_SIS (log_y));
	}

	// Cap the diameter to a sane range
	if (diameter > MAX_X_UNIVERSE * 4)
		diameter = MAX_X_UNIVERSE * 4;

	/* Draw outer circle*/
	r.extent.width = UNIVERSE_TO_DISPX (diameter)
			- UNIVERSE_TO_DISPX (0);

	if (r.extent.width < 0)
		r.extent.width = -r.extent.width;

	r.extent.height = UNIVERSE_TO_DISPY (diameter)
			- UNIVERSE_TO_DISPY (0);

	if (r.extent.height < 0)
		r.extent.height = -r.extent.height;

	r.corner.x = UNIVERSE_TO_DISPX (corner.x)
			- (r.extent.width >> 1);
	r.corner.y = UNIVERSE_TO_DISPY (corner.y)
			- (r.extent.height >> 1);

	OldColor = SetContextForeGroundColor (
			BUILD_COLOR (MAKE_RGB15 (0x03, 0x03, 0x03), 0x22));
	DrawFilledOval (&r);

	/* Draw a second fuel circle showing the 'point of no return', past which there will
	 * not be enough fuel to return to Sol.
	 */
	if (GET_GAME_STATE(STARBASE_AVAILABLE) && optFuelRange)
	{
		diameter_no_return = OnBoardFuel - get_fuel_to_sol();

		if (diameter_no_return < 0)
			diameter_no_return = 0;

		if (diameter_no_return > MAX_X_UNIVERSE * 4)
			diameter_no_return = MAX_X_UNIVERSE * 4;

		r.extent.width = UNIVERSE_TO_DISPX(diameter_no_return)
			- UNIVERSE_TO_DISPX(0);

		if (r.extent.width < 0)
			r.extent.width = -r.extent.width;

		r.extent.height = UNIVERSE_TO_DISPY(diameter_no_return)
			- UNIVERSE_TO_DISPY(0);

		if (r.extent.height < 0)
			r.extent.height = -r.extent.height;

		r.corner.x = UNIVERSE_TO_DISPX(corner.x)
			- (r.extent.width >> 1);
		r.corner.y = UNIVERSE_TO_DISPY(corner.y)
			- (r.extent.height >> 1);

		SetContextForeGroundColor (
			BUILD_COLOR (MAKE_RGB15 (0x04, 0x04, 0x05), 0x22));
		DrawFilledOval (&r);
	}
	SetContextForeGroundColor(OldColor);
}

BOOLEAN
isHomeworld (BYTE Index)
{
	BOOLEAN raceBool = FALSE;

	switch (Index)
	{
		case CHMMR_DEFINED:
			if (GET_GAME_STATE (KNOW_CHMMR_HOMEWORLD)
				&& CheckAlliance (CHMMR_SHIP) != DEAD_GUY)
				raceBool = TRUE;
			break;
		case ORZ_DEFINED:
			if (GET_GAME_STATE (KNOW_ORZ_HOMEWORLD)
				&& CheckAlliance (ORZ_SHIP) != DEAD_GUY)
				raceBool = TRUE;
			break;
		case PKUNK_DEFINED:
			if (GET_GAME_STATE (KNOW_PKUNK_HOMEWORLD)
				&& CheckAlliance (PKUNK_SHIP) != DEAD_GUY)
				raceBool = TRUE;
			break;
		case SHOFIXTI_DEFINED:
			if (GET_GAME_STATE (KNOW_SHOFIXTI_HOMEWORLD)
				&& CheckAlliance (SHOFIXTI_SHIP) != DEAD_GUY)
				raceBool = TRUE;
			break;
		case SPATHI_DEFINED:
			if (GET_GAME_STATE (KNOW_SPATHI_HOMEWORLD)
				&& CheckAlliance (SPATHI_SHIP) != DEAD_GUY)
				raceBool = TRUE;
			break;
		case SUPOX_DEFINED:
			if (GET_GAME_STATE (KNOW_SUPOX_HOMEWORLD)
				&& CheckAlliance (SUPOX_SHIP) != DEAD_GUY)
				raceBool = TRUE;
			break;
		case THRADD_DEFINED:
			if (GET_GAME_STATE (KNOW_THRADD_HOMEWORLD)
				&& CheckAlliance (THRADDASH_SHIP) != DEAD_GUY)
				raceBool = TRUE;
			break;
		case UTWIG_DEFINED:
			if (GET_GAME_STATE (KNOW_UTWIG_HOMEWORLD)
				&& CheckAlliance (UTWIG_SHIP) != DEAD_GUY)
				raceBool = TRUE;
			break;
		case VUX_DEFINED:
			if (GET_GAME_STATE (KNOW_VUX_HOMEWORLD)
				&& CheckAlliance (VUX_SHIP) != DEAD_GUY)
				raceBool = TRUE;
			break;
		case YEHAT_DEFINED:
			if (GET_GAME_STATE (KNOW_YEHAT_HOMEWORLD)
				&& CheckAlliance (YEHAT_SHIP) != DEAD_GUY)
				raceBool = TRUE;
			break;
		case DRUUGE_DEFINED:
			if (GET_GAME_STATE (KNOW_DRUUGE_HOMEWORLD)
				&& CheckAlliance (DRUUGE_SHIP) != DEAD_GUY)
				raceBool = TRUE;
			break;
		case ILWRATH_DEFINED:
			if (GET_GAME_STATE (KNOW_ILWRATH_HOMEWORLD)
				&& CheckAlliance (ILWRATH_SHIP) != DEAD_GUY)
				raceBool = TRUE;
			break;
		case MYCON_DEFINED:
			if (GET_GAME_STATE (KNOW_MYCON_HOMEWORLD)
				&& CheckAlliance (MYCON_SHIP) != DEAD_GUY)
				raceBool = TRUE;
			break;
		case SLYLANDRO_DEFINED:
			if (GET_GAME_STATE (KNOW_SLYLANDRO_HOMEWORLD)
				&& Index == SLYLANDRO_DEFINED)
				raceBool = TRUE;
			break;
		case UMGAH_DEFINED:
			if (GET_GAME_STATE (KNOW_UMGAH_HOMEWORLD)
				&& CheckAlliance (UMGAH_SHIP) != DEAD_GUY)
				raceBool = TRUE;
			break;
		case ZOQFOT_DEFINED:
			if (GET_GAME_STATE (KNOW_ZOQFOT_HOMEWORLD)
				&& CheckAlliance (ZOQFOTPIK_SHIP) != DEAD_GUY)
				raceBool = TRUE;
			break;
		case SYREEN_DEFINED:
			if (GET_GAME_STATE (KNOW_SYREEN_HOMEWORLD)
				&& CheckAlliance (SYREEN_SHIP) != DEAD_GUY)
				raceBool = TRUE;
			break;
		case ANDROSYNTH_DEFINED:
			if (GET_GAME_STATE (KNOW_ANDROSYNTH_HOMEWORLD)
				&& Index == ANDROSYNTH_DEFINED
				&& CheckAlliance (ANDROSYNTH_SHIP) != DEAD_GUY)
				raceBool = TRUE;
			break;
	}

	return raceBool;
}

static void
DrawStarMap (COUNT race_update, RECT *pClipRect)
{
#define GRID_DELTA 500
	SIZE i;
	COUNT which_space;
	// long diameter;
	RECT r, old_r;
	POINT oldOrigin = {0, 0};
	STAMP s;
	FRAME star_frame;
	STAR_DESC *SDPtr;
	BOOLEAN draw_cursor;

	if (pClipRect == (RECT*)-1)
	{
		pClipRect = 0;
		draw_cursor = FALSE;
	}
	else
	{
		draw_cursor = TRUE;
	}

	SetContext (SpaceContext);
	if (pClipRect)
	{
		GetContextClipRect (&old_r);
		pClipRect->corner.x += old_r.corner.x;
		pClipRect->corner.y += old_r.corner.y;
		SetContextClipRect (pClipRect);
		pClipRect->corner.x -= old_r.corner.x;
		pClipRect->corner.y -= old_r.corner.y;
		// Offset the origin so that we draw the correct gfx in the cliprect
		oldOrigin = SetContextOrigin (MAKE_POINT (-pClipRect->corner.x,
				-pClipRect->corner.y));
	}

	if (transition_pending)
	{
		SetTransitionSource (NULL);
	}
	BatchGraphics ();
	
	which_space = GET_GAME_STATE (ARILOU_SPACE_SIDE);

	if (which_space <= 1)
	{
		SDPtr = &star_array[0];
		SetContextForeGroundColor (
				BUILD_COLOR (MAKE_RGB15 (0x00, 0x00, 0x07), 0x57));
		SetContextBackGroundColor (BLACK_COLOR);
	}
	else
	{
		SDPtr = &star_array[NUM_SOLAR_SYSTEMS + 1];
		SetContextForeGroundColor (
				BUILD_COLOR (MAKE_RGB15 (0x00, 0x0B, 0x00), 0x6D));
		SetContextBackGroundColor (
				BUILD_COLOR (MAKE_RGB15 (0x00, 0x08, 0x00), 0x6E));
	}
	ClearDrawable ();

	if (which_starmap != CONSTELLATION_MAP
			&& (race_update == 0 && which_space < 2)
		&& !(optInfiniteFuel || GLOBAL_SIS (FuelOnBoard) == 0))
	{	// Draw the fuel range circle
		DrawFuelCircles ();
	}

	{	// Horizontal lines
		r.corner.x = UNIVERSE_TO_DISPX (0);
		r.extent.width = (SIS_SCREEN_WIDTH << zoomLevel) - (RES_SCALE (1) << zoomLevel);
		r.extent.height = RES_SCALE (1);

		for (i = MAX_Y_UNIVERSE; i >= 0; i -= GRID_DELTA)
		{
			r.corner.y = UNIVERSE_TO_DISPY (i);
			DrawFilledRectangle (&r);
		}

		r.corner.y = UNIVERSE_TO_DISPY (0);
		DrawFilledRectangle (&r);

		// Vertical lines
		r.corner.y = UNIVERSE_TO_DISPY (MAX_Y_UNIVERSE) + RES_SCALE (1);
		r.extent.width = RES_SCALE (1);
		r.extent.height = (SIS_SCREEN_HEIGHT << zoomLevel) - RES_SCALE (1);

		for (i = 0; i < MAX_Y_UNIVERSE; i += GRID_DELTA)
		{
			r.corner.x = UNIVERSE_TO_DISPX (i);
			DrawFilledRectangle (&r);
		}

		// Edge rounding error compensation
		// so the bar wouldn't leak over the edge
		r.corner.x = UNIVERSE_TO_DISPX (MAX_X_UNIVERSE);
		r.corner.y = UNIVERSE_TO_DISPY (MAX_Y_UNIVERSE);
		r.extent.height = (SIS_SCREEN_HEIGHT << zoomLevel) + RES_SCALE (1);
		if (r.extent.height - RES_SCALE (1) > (-(UNIVERSE_TO_DISPY (MAX_Y_UNIVERSE) - UNIVERSE_TO_DISPY (0))))
			r.extent.height -= RES_SCALE (1);
		DrawFilledRectangle (&r);
	}

	star_frame = SetRelFrameIndex (StarMapFrame, 2);
	if (which_space <= 1 && which_starmap != CONSTELLATION_MAP)
	{
		COUNT index;
		HFLEETINFO hStarShip, hNextShip;
		static const Color race_colors[] =
		{
			RACE_COLORS
		};

		// JMS: For drawing SC1-era starmap.
		static const COUNT war_era_strengths[] =
		{
			WAR_ERA_STRENGTHS
		};
		static const POINT war_era_locations[] =
		{
			WAR_ERA_LOCATIONS
		};

		for (index = 0,
				hStarShip = GetHeadLink (&GLOBAL (avail_race_q));
				hStarShip != 0; ++index, hStarShip = hNextShip)
		{
			FLEET_INFO *FleetPtr;

			FleetPtr = LockFleetInfo (&GLOBAL (avail_race_q), hStarShip);
			hNextShip = _GetSuccLink (FleetPtr);

			if (FleetPtr->known_strength || 
				(show_war_era_situation && war_era_strengths[index]))
			{
				RECT repair_r;

				if (show_war_era_situation)
					GetWarEraSphereRect (index, war_era_strengths, 
							war_era_locations, &r, &repair_r);
				else
					GetSphereRect (FleetPtr, &r, &repair_r);

				if (r.corner.x < SIS_SCREEN_WIDTH
						&& r.corner.y < SIS_SCREEN_HEIGHT
						&& r.corner.x + r.extent.width > 0
						&& r.corner.y + r.extent.height > 0
						&& (pClipRect == 0
						|| (repair_r.corner.x < pClipRect->corner.x + pClipRect->extent.width
						&& repair_r.corner.y < pClipRect->corner.y + pClipRect->extent.height
						&& repair_r.corner.x + repair_r.extent.width > pClipRect->corner.x
						&& repair_r.corner.y + repair_r.extent.height > pClipRect->corner.y)))
				{
					Color c;
					TEXT t;
					STRING locString;

					c = race_colors[index];
					if (index + 1 == race_update)
						SetContextForeGroundColor (WHITE_COLOR);
					else
						SetContextForeGroundColor (c);

					if (!(which_starmap == WAR_ERA_STARMAP
							&& war_era_strengths[index] == 0))
						DrawOval (&r, 0, IS_HD);

					if (isPC (optWhichFonts))
						SetContextFont (TinyFont);
					else
						SetContextFont (TinyFontBold);

					t.baseline.x = r.corner.x + (r.extent.width >> 1);
					t.baseline.y = r.corner.y + (r.extent.height >> 1) - RES_SCALE (1);
					t.align = ALIGN_CENTER;
					
					locString = SetAbsStringTableIndex (
							FleetPtr->race_strings,
							(index == ANDROSYNTH_SHIP? 0 : 1));
					t.CharCount = GetStringLength (locString);
					t.pStr = (UNICODE *)GetStringAddress (locString);
					
					// For drawing War-Era starmap.
					if (show_war_era_situation)
					{
						switch (index)
						{
							case PKUNK_SHIP:
							case THRADDASH_SHIP:
							case DRUUGE_SHIP:
								t.pStr = GAME_STRING (STAR_STRING_BASE + 132);
								break;
						}
						t.CharCount = (COUNT)strlen (t.pStr);
					}

					TextRect (&t, &r, NULL);

					if (r.corner.x <= 0)
						t.baseline.x -= r.corner.x - RES_SCALE (1);
					else if (r.corner.x + r.extent.width >= SIS_SCREEN_WIDTH)
						t.baseline.x -= (r.corner.x + r.extent.width)
								- SIS_SCREEN_WIDTH + RES_SCALE (1);
					if (r.corner.y <= 0)
						t.baseline.y -= r.corner.y - RES_SCALE (1);
					else if (r.corner.y + r.extent.height >= SIS_SCREEN_HEIGHT)
						t.baseline.y -= (r.corner.y + r.extent.height)
								- SIS_SCREEN_HEIGHT + RES_SCALE (1);

					// The text color is slightly lighter than the color of
					// the SoI.
					c.r = (c.r >= 0xff - CC5TO8 (0x03)) ?
							0xff : c.r + CC5TO8 (0x03);
					c.g = (c.g >= 0xff - CC5TO8 (0x03)) ?
							0xff : c.g + CC5TO8 (0x03);
					c.b = (c.b >= 0xff - CC5TO8 (0x03)) ?
							0xff : c.b + CC5TO8 (0x03);

					SetContextForeGroundColor (c);
					
					if (!show_war_era_situation ||
							(show_war_era_situation && war_era_strengths[index]))
						font_DrawText (&t);
				}
			}

			UnlockFleetInfo (&GLOBAL (avail_race_q), hStarShip);
		}
	}

	// Kruzen: This draws the constellation lines on the constellation starmap.
	if (which_space <= 1 && which_starmap == CONSTELLATION_MAP)
	{
		Color oldColor;
		POINT *CNPtr;
		LINE l;
		BYTE c = 0x3F + IF_HD (0x11);
		CNPtr = &constel_array[0];

		oldColor = SetContextForeGroundColor (BUILD_COLOR_RGBA (c, c, c, 0xFF));

		while (CNPtr->x < MAX_X_UNIVERSE && CNPtr->y < MAX_Y_UNIVERSE)
		{
			l.first.x = UNIVERSE_TO_DISPX (CNPtr->x) + HD_COMP;
			l.first.y = UNIVERSE_TO_DISPY (CNPtr->y) + HD_COMP;
			CNPtr++;
			l.second.x = UNIVERSE_TO_DISPX (CNPtr->x) + HD_COMP;
			l.second.y = UNIVERSE_TO_DISPY (CNPtr->y) + HD_COMP;
			CNPtr++;
			DrawLine (&l, 1);
		}
	 	SetContextForeGroundColor (oldColor);
	}

	// This draws reticules over known alien Homeworlds
	if (which_space <= 1 && which_starmap == HOMEWORLDS_MAP)
	{
		COUNT i;

		for (i = 0; i < (NUM_SOLAR_SYSTEMS + 1); ++i)
		{
			BYTE Index = star_array[i].Index;

			if (isHomeworld (Index))
				DrawReticule (star_array[i].star_pt, TRUE);
		}
	}

	// This draws reticules over the Rainbow worlds
	if (which_space <= 1 && which_starmap == RAINBOW_MAP)
	{
		COUNT i;

		for (i = 0; i < (NUM_SOLAR_SYSTEMS + 1); ++i)
		{
			if (star_array[i].Index == RAINBOW_DEFINED)
				DrawReticule (star_array[i].star_pt, TRUE);
		}
	}

	do
	{	// Draws all the stars
		BYTE star_type;
		static COUNT i = 0;
		
		i = i >= NUM_SOLAR_SYSTEMS ? 0 : i;

		star_type = SDPtr->Type;

		s.origin.x = UNIVERSE_TO_DISPX (SDPtr->star_pt.x) + HD_COMP;
		s.origin.y = UNIVERSE_TO_DISPY (SDPtr->star_pt.y) + HD_COMP;
		if (which_space <= 1)
		{			
			if (optShowVisitedStars && isStarVisited (i)
					&& which_starmap == NORMAL_STARMAP
					&& SDPtr->Index != SOL_DEFINED)
			{
				s.frame = SetRelFrameIndex (visitedStarsFrame,
						STAR_TYPE (star_type)
						* NUM_STAR_COLORS
						+ STAR_COLOR (star_type));
			}
			else
			{
				s.frame = SetRelFrameIndex (star_frame,
						STAR_TYPE (star_type)
						* NUM_STAR_COLORS
						+ STAR_COLOR (star_type));
			}
		}
		else if (SDPtr->star_pt.x == ARILOU_HOME_X
				&& SDPtr->star_pt.y == ARILOU_HOME_Y)
			s.frame = SetRelFrameIndex (star_frame,
					SUPER_GIANT_STAR * NUM_STAR_COLORS + GREEN_BODY);
		else
			s.frame = SetRelFrameIndex (star_frame,
					GIANT_STAR * NUM_STAR_COLORS + GREEN_BODY);
		DrawStamp (&s);

		++i;
		++SDPtr;
	} while (SDPtr->star_pt.x <= MAX_X_UNIVERSE
			&& SDPtr->star_pt.y <= MAX_Y_UNIVERSE);

	if (GET_GAME_STATE (ARILOU_SPACE))
	{
		if (which_space <= 1)
		{
			s.origin.x = UNIVERSE_TO_DISPX (ARILOU_SPACE_X) + HD_COMP;
			s.origin.y = UNIVERSE_TO_DISPY (ARILOU_SPACE_Y) + HD_COMP;
		}
		else
		{
			s.origin.x = UNIVERSE_TO_DISPX (QUASI_SPACE_X) + HD_COMP;
			s.origin.y = UNIVERSE_TO_DISPY (QUASI_SPACE_Y) + HD_COMP;
		}
		s.frame = SetRelFrameIndex (star_frame,
				GIANT_STAR * NUM_STAR_COLORS + GREEN_BODY);
		DrawStamp (&s);
	}

	if (race_update == 0
			&& GLOBAL (autopilot.x) != ~0
			&& GLOBAL (autopilot.y) != ~0)
	{
		DrawAutoPilot (&GLOBAL (autopilot));
		if (IS_HD)
			DrawReticule (GLOBAL (autopilot), FALSE);
	}

	if (transition_pending)
	{
		GetContextClipRect (&r);
		ScreenTransition (optIPScaler, &r);
		transition_pending = FALSE;
	}

	if (pClipRect)
	{
		SetContextClipRect (&old_r);
		SetContextOrigin (oldOrigin);
	}

	if (race_update == 0 && draw_cursor)
	{
		GetContextClipRect (&r);
		LoadIntoExtraScreen (&r);
		DrawCursor (UNIVERSE_TO_DISPX (cursorLoc.x),
				UNIVERSE_TO_DISPY (cursorLoc.y));
		flashCurrentLocation (NULL, TRUE);
	}

	UnbatchGraphics ();
}

static void
EraseCursor (COORD curs_x, COORD curs_y)
{
	RECT r;

	curs_x += HD_COMP;
	curs_y += HD_COMP;

	GetFrameRect (StarMapFrame, &r);

	if ((r.corner.x += curs_x) < 0)
	{
		r.extent.width += r.corner.x;
		r.corner.x = 0;
	}
	else if (r.corner.x + r.extent.width >= SIS_SCREEN_WIDTH)
		r.extent.width = SIS_SCREEN_WIDTH - r.corner.x;
	if ((r.corner.y += curs_y) < 0)
	{
		r.extent.height += r.corner.y;
		r.corner.y = 0;
	}
	else if (r.corner.y + r.extent.height >= SIS_SCREEN_HEIGHT)
		r.extent.height = SIS_SCREEN_HEIGHT - r.corner.y;

#ifndef OLD
	RepairBackRect (&r);
#else /* NEW */
	r.extent.height += r.corner.y & 1;
	r.corner.y &= ~1;
	DrawStarMap (0, &r);
#endif /* OLD */
}

static void
ZoomStarMap (SIZE dir)
{
#define MAX_ZOOM_SHIFT 4
	if (dir > 0)
	{
		if (zoomLevel < MAX_ZOOM_SHIFT)
		{
			++zoomLevel;
			mapOrigin = cursorLoc;

			DrawStarMap (0, NULL);
			SleepThread (ONE_SECOND / 8);
		}
	}
	else if (dir < 0)
	{
		if (zoomLevel > 0)
		{
			if (zoomLevel > 1)
				mapOrigin = cursorLoc;
			else
			{
				mapOrigin.x = MAX_X_UNIVERSE >> 1;
				mapOrigin.y = MAX_Y_UNIVERSE >> 1;
			}
			--zoomLevel;

			DrawStarMap (0, NULL);
			SleepThread (ONE_SECOND / 8);
		}
	}
}

static void
UpdateCursorLocation (int sx, int sy, const POINT *newpt)
{
	STAMP s;
	POINT pt;

	pt.x = ORIG_UNIVERSE_TO_DISPX (cursorLoc.x);
	pt.y = ORIG_UNIVERSE_TO_DISPY (cursorLoc.y);

	if (newpt)
	{	// absolute move
		sx = sy = 0;
		s.origin.x = ORIG_UNIVERSE_TO_DISPX (newpt->x);
		s.origin.y = ORIG_UNIVERSE_TO_DISPY (newpt->y);
		cursorLoc = *newpt;
	}
	else
	{	// incremental move
		s.origin.x = pt.x + sx;
		s.origin.y = pt.y + sy;
	}

	if (sx)
	{
		cursorLoc.x = ORIG_DISP_TO_UNIVERSEX (s.origin.x) - sx;
		while (ORIG_UNIVERSE_TO_DISPX (cursorLoc.x) == pt.x)
			cursorLoc.x += sx;
		
		if (cursorLoc.x < 0)
			cursorLoc.x = 0;
		else if (cursorLoc.x > MAX_X_UNIVERSE)
			cursorLoc.x = MAX_X_UNIVERSE;

		s.origin.x = ORIG_UNIVERSE_TO_DISPX (cursorLoc.x);
	}

	if (sy)
	{
		cursorLoc.y = ORIG_DISP_TO_UNIVERSEY (s.origin.y) + sy;
		while (ORIG_UNIVERSE_TO_DISPY(cursorLoc.y) == pt.y)
			cursorLoc.y -= sy;

		if (cursorLoc.y < 0)
			cursorLoc.y = 0;
		else if (cursorLoc.y > MAX_Y_UNIVERSE)
			cursorLoc.y = MAX_Y_UNIVERSE;

		s.origin.y = ORIG_UNIVERSE_TO_DISPY (cursorLoc.y);
	}

	// ORIG_SIS_SCREEN_WIDTH/HEIGHT for viewport follows cursor mode
	if (s.origin.x < 0 || s.origin.y < 0
			|| s.origin.x >= ORIG_SIS_SCREEN_WIDTH 
			|| s.origin.y >= ORIG_SIS_SCREEN_HEIGHT)
	{
		mapOrigin = cursorLoc;
		DrawStarMap (0, NULL);
		
		s.origin.x = ORIG_UNIVERSE_TO_DISPX (cursorLoc.x);
		s.origin.y = ORIG_UNIVERSE_TO_DISPY (cursorLoc.y);
	}
	else
	{
		BatchGraphics ();
		EraseCursor (RES_SCALE (pt.x), RES_SCALE (pt.y));
		DrawCursor (RES_SCALE (s.origin.x), RES_SCALE (s.origin.y));
		flashCurrentLocation (NULL, TRUE);
		UnbatchGraphics ();
	}
}

#define CURSOR_INFO_BUFSIZE 256

int starIndex (POINT starPt)
{
	COUNT i;

	for (i = 0; i <= NUM_SOLAR_SYSTEMS; i++)
	{
		if (star_array[i].star_pt.x == starPt.x
			&& star_array[i].star_pt.y == starPt.y)
			break;
	}
	return i;
}

static void
UpdateCursorInfo (UNICODE *prevbuf)
{
	UNICODE buf[CURSOR_INFO_BUFSIZE] = "";
	POINT pt;
	STAR_DESC *SDPtr;
	STAR_DESC *BestSDPtr;

	if (which_starmap == NORMAL_STARMAP)
	{	// "(Star Search: F6 | Toggle Maps: F7)"
		utf8StringCopy (buf, sizeof (buf), GAME_STRING (
				FEEDBACK_STRING_BASE + 2 + is3DO (optWhichFonts)));
	}
	else
		utf8StringCopy (buf, sizeof (buf),
				GAME_STRING (FEEDBACK_STRING_BASE + 3 + which_starmap));

	pt.x = UNIVERSE_TO_DISPX (cursorLoc.x) + HD_COMP;
	pt.y = UNIVERSE_TO_DISPY (cursorLoc.y) + HD_COMP;

	SDPtr = BestSDPtr = 0;
	while ((SDPtr = FindStar (SDPtr, &cursorLoc, 75, 75)))
	{
		if (UNIVERSE_TO_DISPX (SDPtr->star_pt.x) + HD_COMP == pt.x
				&& UNIVERSE_TO_DISPY (SDPtr->star_pt.y) + HD_COMP == pt.y
				&& (BestSDPtr == 0
				|| STAR_TYPE (SDPtr->Type) >= STAR_TYPE (BestSDPtr->Type)))
			BestSDPtr = SDPtr;
	}

	if (BestSDPtr)
	{
		// JMS: For masking the names of QS portals not yet entered.
		BYTE QuasiPortalsKnown[] =
		{
			QS_PORTALS_KNOWN
		};
		
		// A star is near the cursor:
		// Snap cursor onto star
		cursorLoc = BestSDPtr->star_pt;
		
		if (GET_GAME_STATE(ARILOU_SPACE_SIDE) >= 2
			&& !(QuasiPortalsKnown[BestSDPtr->Postfix - 133]))
			utf8StringCopy (buf, sizeof (buf),
				GAME_STRING (STAR_STRING_BASE + 132));
		else
			GetClusterName (BestSDPtr, buf);
	}
	else
	{	// No star found. Reset the coordinates to the cursor's location
		cursorLoc.x = DISP_TO_UNIVERSEX (pt.x);
		if (cursorLoc.x < 0)
			cursorLoc.x = 0;
		else if (cursorLoc.x > MAX_X_UNIVERSE)
			cursorLoc.x = MAX_X_UNIVERSE;
		cursorLoc.y = DISP_TO_UNIVERSEY (pt.y);
		if (cursorLoc.y < 0)
			cursorLoc.y = 0;
		else if (cursorLoc.y > MAX_Y_UNIVERSE)
			cursorLoc.y = MAX_Y_UNIVERSE;
	}

	if (GET_GAME_STATE (ARILOU_SPACE))
	{
		POINT ari_pt;

		if (GET_GAME_STATE (ARILOU_SPACE_SIDE) <= 1)
		{
			ari_pt.x = ARILOU_SPACE_X;
			ari_pt.y = ARILOU_SPACE_Y;
		}
		else
		{
			ari_pt.x = QUASI_SPACE_X;
			ari_pt.y = QUASI_SPACE_Y;
		}

		if (UNIVERSE_TO_DISPX (ari_pt.x) + HD_COMP == pt.x
				&& UNIVERSE_TO_DISPY (ari_pt.y) + HD_COMP == pt.y)
		{
			cursorLoc = ari_pt;
			utf8StringCopy (buf, sizeof (buf),
					GAME_STRING (STAR_STRING_BASE + 132));
		}
	}

	DrawHyperCoords (cursorLoc);
	if (strcmp (buf, prevbuf) != 0)
	{
		strcpy (prevbuf, buf);
		
		// Cursor is on top of a star. Display its name.
		if (BestSDPtr)
		{
			if (optShowVisitedStars
				&& isStarVisited (starIndex (BestSDPtr->star_pt)))
			{
				UNICODE visBuf[CURSOR_INFO_BUFSIZE] = "";

				utf8StringCopy (visBuf, sizeof (visBuf), buf);
				snprintf (buf, sizeof buf, "%c %s %c", '(', visBuf, ')');
			}

			DrawSISMessage (buf);
		}
		// Cursor is elsewhere.
		else
		{
			// In HS, display default star search button name.
			if (GET_GAME_STATE (ARILOU_SPACE_SIDE) <= 1)
			{
				CONTEXT OldContext;
				OldContext = SetContext (OffScreenContext);
				
				if (show_war_era_situation)
					SetContextForeGroundColor (
							BUILD_COLOR (
								MAKE_RGB15 (0x18, 0x00, 0x00), 0x00));
				else
					SetContextForeGroundColor (
							BUILD_COLOR (
								MAKE_RGB15 (0x0E, 0xA7, 0xD9), 0x00));
						
				DrawSISMessageEx (buf, -1, -1, DSME_MYCOLOR);
				SetContext (OldContext);
			}
			// In QS, don't display star search button - the search is unusable.
			else
			{
				strcpy (buf, GAME_STRING (NAVIGATION_STRING_BASE + 1));
				DrawSISMessage (buf);
			}
		}
	}
}

static unsigned int
FuelRequired (void)
{
	COUNT fuel_required;
	DWORD f;
	POINT pt;

	if (!inHQSpace ())
		pt = CurStarDescPtr->star_pt;
	else
	{
		pt.x = LOGX_TO_UNIVERSE (GLOBAL_SIS (log_x));
		pt.y = LOGY_TO_UNIVERSE (GLOBAL_SIS (log_y));
	}
	pt.x -= cursorLoc.x;
	pt.y -= cursorLoc.y;

	f = (DWORD)((long)pt.x * pt.x + (long)pt.y * pt.y);
	if (f == 0 || GET_GAME_STATE (ARILOU_SPACE_SIDE) > 1)
		fuel_required = 0;
	else
		fuel_required = square_root (f) + (FUEL_TANK_SCALE / 20);

	return fuel_required;
}

static void
UpdateFuelRequirement (void)
{
	UNICODE buf[80];
	COUNT fuel_required = FuelRequired();

	sprintf (buf, "%s %u.%u",
			GAME_STRING (NAVIGATION_STRING_BASE + 4),
			fuel_required / FUEL_TANK_SCALE,
			(fuel_required % FUEL_TANK_SCALE) / 10);

	DrawStatusMessage (buf);
}

#define STAR_SEARCH_BUFSIZE 256

typedef struct starsearch_state
{
	// TODO: pMS field is probably not needed anymore
	MENU_STATE *pMS;
	UNICODE Text[STAR_SEARCH_BUFSIZE];
	UNICODE LastText[STAR_SEARCH_BUFSIZE];
	DWORD LastChangeTime;
	int FirstIndex;
	int CurIndex;
	int LastIndex;
	BOOLEAN SingleClust;
	BOOLEAN SingleMatch;
	UNICODE Buffer[STAR_SEARCH_BUFSIZE];
	const UNICODE *Prefix;
	const UNICODE *Cluster;
	int PrefixLen;
	int ClusterLen;
	int ClusterPos;
	int SortedStars[NUM_SOLAR_SYSTEMS];
} STAR_SEARCH_STATE;

static int
compStarName (const void *ptr1, const void *ptr2)
{
	int index1;
	int index2;

	index1 = *(const int *) ptr1;
	index2 = *(const int *) ptr2;
	if (star_array[index1].Postfix != star_array[index2].Postfix)
	{
		return utf8StringCompare (GAME_STRING (star_array[index1].Postfix),
				GAME_STRING (star_array[index2].Postfix));
	}

	if (star_array[index1].Prefix < star_array[index2].Prefix)
		return -1;
	
	if (star_array[index1].Prefix > star_array[index2].Prefix)
		return 1;

	return 0;
}

static void
SortStarsOnName (STAR_SEARCH_STATE *pSS)
{
	int i;
	int *sorted = pSS->SortedStars;

	for (i = 0; i < NUM_SOLAR_SYSTEMS; i++)
		sorted[i] = i;

	qsort (sorted, NUM_SOLAR_SYSTEMS, sizeof (int), compStarName);
}

static void
SplitStarName (STAR_SEARCH_STATE *pSS)
{
	UNICODE *buf = pSS->Buffer;
	UNICODE *next;
	UNICODE *sep = NULL;

	pSS->Prefix = 0;
	pSS->PrefixLen = 0;
	pSS->Cluster = 0;
	pSS->ClusterLen = 0;
	pSS->ClusterPos = 0;

	// skip leading space
	for (next = buf; *next != '\0' &&
			getCharFromString ((const UNICODE **)&next)
				== (isPC (optWhichFonts) ? UNICHAR_SPACE : UNICHAR_TAB);
			buf = next)
		;
	if (*buf == '\0')
	{	// no text
		return;
	}

	pSS->Prefix = buf;

	// See if player gave a prefix
	for (buf = next; *next != '\0' &&
			getCharFromString ((const UNICODE **)&next)
				!= (isPC (optWhichFonts) ? UNICHAR_SPACE : UNICHAR_TAB);
			buf = next)
		;
	if (*buf != '\0')
	{	// found possibly separating ' '
		sep = buf;
		// skip separating space
		for (buf = next; *next != '\0' &&
				getCharFromString ((const UNICODE **)&next)
					== (isPC (optWhichFonts) ? UNICHAR_SPACE : UNICHAR_TAB);
				buf = next)
			;
	}

	if (*buf == '\0')
	{	// reached the end -- cluster only
		pSS->Cluster = pSS->Prefix;
		pSS->ClusterLen = utf8StringCount (pSS->Cluster);
		pSS->ClusterPos = utf8StringCountN (pSS->Buffer, pSS->Cluster);
		pSS->Prefix = 0;
		return;
	}

	// consider the rest cluster name (whatever there is)
	pSS->Cluster = buf;
	pSS->ClusterLen = utf8StringCount (pSS->Cluster);
	pSS->ClusterPos = utf8StringCountN (pSS->Buffer, pSS->Cluster);
	*sep = '\0'; // split
	pSS->PrefixLen = utf8StringCount (pSS->Prefix);
}

static inline int
SkipStarCluster (int *sortedStars, int istar)
{
	int Postfix = star_array[sortedStars[istar]].Postfix;

	for (++istar; istar < NUM_SOLAR_SYSTEMS &&
			star_array[sortedStars[istar]].Postfix == Postfix;
			++istar)
		;
	return istar;
}

static int
FindNextStarIndex (STAR_SEARCH_STATE *pSS, int from, BOOLEAN WithinClust)
{
	int i;

	if (!pSS->Cluster)
		return -1; // nothing to search for

	for (i = from; i < NUM_SOLAR_SYSTEMS; ++i)
	{
		STAR_DESC *SDPtr = &star_array[pSS->SortedStars[i]];
		UNICODE FullName[STAR_SEARCH_BUFSIZE];
		UNICODE *ClusterName = GAME_STRING (SDPtr->Postfix);
		const UNICODE *sptr;
		const UNICODE *dptr;
		int dlen;
		int c;
		
		dlen = utf8StringCount (ClusterName);
		if (pSS->ClusterLen > dlen)
		{	// no match, skip the rest of cluster
			i = SkipStarCluster (pSS->SortedStars, i) - 1;
			continue;
		}

		for (c = 0, sptr = pSS->Cluster, dptr = ClusterName;
				c < pSS->ClusterLen; ++c)
		{
			UniChar sc = getCharFromString (&sptr);
			UniChar dc = getCharFromString (&dptr);

			if (UniChar_toUpper (sc) != UniChar_toUpper (dc))
				break;
		}

		if (c < pSS->ClusterLen)
		{	// no match here, skip the rest of cluster
			i = SkipStarCluster (pSS->SortedStars, i) - 1;
			continue;
		}

		if (pSS->Prefix && !SDPtr->Prefix)
			// we were given a prefix but found a singular star;
			// that is a no match
			continue;

		if (WithinClust)
			// searching within clusters; any prefix is a match
			break;

		if (!pSS->Prefix)
		{	// searching for cluster name only
			// return only the first stars in a cluster
			if (i == 0 || SDPtr->Postfix !=
					star_array[pSS->SortedStars[i - 1]].Postfix)
			{	// found one
				break;
			}
			else
			{	// another star in the same cluster, skip cluster
				i = SkipStarCluster (pSS->SortedStars, i) - 1;
				continue;
			}
		}

		// check prefix
		GetClusterName (SDPtr, FullName);
		dlen = utf8StringCount (FullName);
		if (pSS->PrefixLen > dlen)
			continue;

		for (c = 0, sptr = pSS->Prefix, dptr = FullName;
				c < pSS->PrefixLen; ++c)
		{
			UniChar sc = getCharFromString (&sptr);
			UniChar dc = getCharFromString (&dptr);

			if (UniChar_toUpper (sc) != UniChar_toUpper (dc))
				break;
		}

		if (c >= pSS->PrefixLen)
			break; // found one
	}

	return (i < NUM_SOLAR_SYSTEMS) ? i : -1;
}

static void
DrawMatchedStarName (TEXTENTRY_STATE *pTES)
{
	STAR_SEARCH_STATE *pSS = (STAR_SEARCH_STATE *) pTES->CbParam;
	UNICODE buf[STAR_SEARCH_BUFSIZE] = "";
	SIZE ExPos = 0;
	SIZE CurPos = -1;
	STAR_DESC *SDPtr = &star_array[pSS->SortedStars[pSS->CurIndex]];
	COUNT flags;

	if (pSS->SingleClust || pSS->SingleMatch)
	{	// draw full star name
		GetClusterName (SDPtr, buf);
		ExPos = -1;
		flags = DSME_SETFR;
	}
	else
	{	// draw substring match
		UNICODE *pstr = buf;

		strcpy (pstr, pSS->Text);
		ExPos = pSS->ClusterPos;
		pstr = skipUTF8Chars (pstr, pSS->ClusterPos);

		strcpy (pstr, GAME_STRING (SDPtr->Postfix));
		ExPos += pSS->ClusterLen;
		CurPos = pTES->CursorPos;

		flags = DSME_CLEARFR;
		if (pTES->JoystickMode)
			flags |= DSME_BLOCKCUR;
	}
	
	DrawSISMessageEx (buf, CurPos, ExPos, flags);
	DrawHyperCoords (cursorLoc);
}

static void
MatchNextStar (STAR_SEARCH_STATE *pSS, BOOLEAN Reset)
{
	if (Reset)
		pSS->FirstIndex = -1; // reset cache
	
	if (pSS->FirstIndex < 0)
	{	// first time after changes
		pSS->CurIndex = -1;
		pSS->LastIndex = -1;
		pSS->SingleClust = FALSE;
		pSS->SingleMatch = FALSE;
		strcpy (pSS->Buffer, pSS->Text);
		SplitStarName (pSS);
	}

	pSS->CurIndex = FindNextStarIndex (pSS, pSS->CurIndex + 1,
			pSS->SingleClust);
	if (pSS->FirstIndex < 0) // first search
		pSS->FirstIndex = pSS->CurIndex;
	
	if (pSS->CurIndex >= 0)
	{	// remember as last (searching forward-only)
		pSS->LastIndex = pSS->CurIndex;
	}
	else
	{	// wrap around
		pSS->CurIndex = pSS->FirstIndex;

		if (pSS->FirstIndex == pSS->LastIndex && pSS->FirstIndex != -1)
		{
			if (!pSS->Prefix)
			{	// only one cluster matching
				pSS->SingleClust = TRUE;
			}
			else
			{	// exact match
				pSS->SingleMatch = TRUE;
			}
		}
	}
}

static BOOLEAN
OnStarNameChange (TEXTENTRY_STATE *pTES)
{
	STAR_SEARCH_STATE *pSS = (STAR_SEARCH_STATE *) pTES->CbParam;
	COUNT flags;
	BOOLEAN ret = TRUE;

	if (strcmp (pSS->Text, pSS->LastText) != 0)
	{	// string changed
		pSS->LastChangeTime = GetTimeCounter ();
		strcpy (pSS->LastText, pSS->Text);
		
		// reset the search
		MatchNextStar (pSS, TRUE);
	}

	if (pSS->CurIndex < 0)
	{	// nothing found
		if (pSS->Text[0] == '\0')
			flags = DSME_SETFR;
		else
			flags = DSME_CLEARFR;
		if (pTES->JoystickMode)
			flags |= DSME_BLOCKCUR;

		ret = DrawSISMessageEx (pSS->Text, pTES->CursorPos, -1, flags);
	}
	else
	{
		STAR_DESC *SDPtr;

		// move the cursor to the found star
		SDPtr = &star_array[pSS->SortedStars[pSS->CurIndex]];
		UpdateCursorLocation (0, 0, &SDPtr->star_pt);

		DrawMatchedStarName (pTES);
		UpdateFuelRequirement ();
	}

	return ret;
}

static BOOLEAN
OnStarNameFrame (TEXTENTRY_STATE *pTES)
{
	STAR_SEARCH_STATE *pSS = (STAR_SEARCH_STATE *) pTES->CbParam;

	if (PulsedInputState.menu[KEY_MENU_NEXT])
	{	// search for next match
		STAR_DESC *SDPtr;

		MatchNextStar (pSS, FALSE);

		if (pSS->CurIndex < 0)
		{	// nothing found
			if (PulsedInputState.menu[KEY_MENU_NEXT])
				PlayMenuSound (MENU_SOUND_FAILURE);
			return TRUE;
		}

		// move the cursor to the found star
		SDPtr = &star_array[pSS->SortedStars[pSS->CurIndex]];
		UpdateCursorLocation (0, 0, &SDPtr->star_pt);

		DrawMatchedStarName (pTES);
		UpdateFuelRequirement ();
	}

	flashCurrentLocation (NULL, FALSE);

	SleepThread (ONE_SECOND / 30);
	
	return TRUE;
}

BOOLEAN
coords_only (UNICODE *s)
{
	BYTE i, count = 0;
	BYTE countD = 0, countC = 0;
	BYTE j = strlen (s);
	//const char *pattern = "^\d*(\.\d+)?:\d*(\.\d+)?$";

	for (i = 0; i < j; i++)
	{
		if (s[i] == '.')
		{
			count++;
			countD++;
		}
		else if (s[i] == ':')
		{
			count++;
			countC++;
		}
		else if (isdigit (s[i]) == 0)
			return FALSE;
		else
			count++;
	}
	return i == j && countD <= 2 && countC == 1;
}

static BOOLEAN
DoStarSearch (MENU_STATE *pMS)
{
	TEXTENTRY_STATE tes;
	STAR_SEARCH_STATE *pss;
	BOOLEAN success;

	pss = HMalloc (sizeof (*pss));
	if (!pss)
		return FALSE;

	DrawSISMessageEx ("", 0, 0, DSME_SETFR);

	TextEntry3DO = is3DO (optWhichFonts);

	pss->pMS = pMS;
	pss->LastChangeTime = 0;
	pss->Text[0] = '\0';
	pss->LastText[0] = '\0';
	pss->FirstIndex = -1;
	SortStarsOnName (pss);

	// text entry setup
	tes.Initialized = FALSE;
	tes.BaseStr = pss->Text;
	tes.MaxSize = sizeof (pss->Text);
	tes.CursorPos = 0;
	tes.CbParam = pss;
	tes.ChangeCallback = OnStarNameChange;
	tes.FrameCallback = OnStarNameFrame;

	SetMenuSounds (MENU_SOUND_ARROWS, MENU_SOUND_SELECT);
	SetDefaultMenuRepeatDelay ();
	success = DoTextEntry (&tes);

	if (coords_only (tes.BaseStr))
	{
		POINT coord;

		coord.x = (COORD)(atof (strtok (tes.BaseStr, ":")) * 10);
		coord.y = (COORD)(atof (strtok (NULL, ":")) * 10);

		if (coord.x > MAX_X_UNIVERSE || coord.y > MAX_Y_UNIVERSE
			|| coord.x < 0 || coord.y < 0)
			success = FALSE;
		else
			UpdateCursorLocation (0, 0, &coord);

		success = TRUE;
	}
	
	DrawSISMessageEx (pss->Text, -1, -1, DSME_CLEARFR);

	HFree (pss);

	TextEntry3DO = FALSE;

	return success;
} 

static BOOLEAN
DoMoveCursor (MENU_STATE *pMS)
{
#define MIN_ACCEL_DELAY (ONE_SECOND / 60)
#define MAX_ACCEL_DELAY (ONE_SECOND / 8)
#define STEP_ACCEL_DELAY (ONE_SECOND / 120)
	static UNICODE last_buf[CURSOR_INFO_BUFSIZE];
	DWORD TimeIn = GetTimeCounter ();
	static COUNT moveRepeats;
	BOOLEAN isMove = FALSE;

	if (!pMS->Initialized)
	{
		POINT universe;

		pMS->Initialized = TRUE;
		pMS->InputFunc = DoMoveCursor;

		if (!inHQSpace ())
			universe = CurStarDescPtr->star_pt;
		else
		{
			universe.x = LOGX_TO_UNIVERSE (GLOBAL_SIS (log_x));
			universe.y = LOGY_TO_UNIVERSE (GLOBAL_SIS (log_y));
		}
		flashCurrentLocation (&universe, FALSE);

		last_buf[0] = '\0';
		UpdateCursorInfo (last_buf);
		UpdateFuelRequirement ();

		return TRUE;
	}
	else if (PulsedInputState.menu[KEY_MENU_CANCEL])
	{
		return FALSE;
	}
	else if (PulsedInputState.menu[KEY_MENU_SELECT])
	{
		// printf("Fuel Available: %d | Fuel Requirement: %d\n", GLOBAL_SIS (FuelOnBoard), FuelRequired());

		if (optBubbleWarp)
		{
			if (GLOBAL_SIS (FuelOnBoard) >= FuelRequired () || optInfiniteFuel)
			{
				GLOBAL (autopilot) = cursorLoc;
				PlayMenuSound (MENU_SOUND_BUBBLEWARP);

				if (!optInfiniteFuel)
					DeltaSISGauges (0, -FuelRequired(), 0);

				if (LOBYTE (GLOBAL (CurrentActivity)) == IN_INTERPLANETARY)
				{
					// We're in a solar system; exit it.
					GLOBAL (CurrentActivity) |= END_INTERPLANETARY;
					// Set a hook to move to the new location:
					debugHook = doInstantMove;
				}
				else 
				{	// Move to the new location immediately.
					doInstantMove ();
				}
				
				return FALSE;
			}
			else
				PlayMenuSound (MENU_SOUND_FAILURE);
		}
		else
		{
			GLOBAL (autopilot) = cursorLoc;
			DrawStarMap (0, NULL);
		}
	}
	else if (PulsedInputState.menu[KEY_MENU_SEARCH])
	{
		if (GET_GAME_STATE (ARILOU_SPACE_SIDE) <= 1)
		{	// HyperSpace search
			POINT oldpt = cursorLoc;

			if (!DoStarSearch (pMS))
			{	// search failed or canceled - return cursor
				UpdateCursorLocation (0, 0, &oldpt);
			}
			FlushCursorRect ();
			// make sure cmp fails
			strcpy (last_buf, "  <random garbage>  ");
			UpdateCursorInfo (last_buf);
			UpdateFuelRequirement ();

			SetMenuRepeatDelay (MIN_ACCEL_DELAY, MAX_ACCEL_DELAY,
					STEP_ACCEL_DELAY, TRUE);
			SetMenuSounds (MENU_SOUND_NONE, MENU_SOUND_NONE);
		}
		else
		{	// no search in QuasiSpace
			PlayMenuSound (MENU_SOUND_FAILURE);
		}
	}
	else if (PulsedInputState.menu[KEY_MENU_TOGGLEMAP]
		&& GET_GAME_STATE (ARILOU_SPACE_SIDE) <= 1)
	{
		BYTE NewState;

		FlushInput ();

		NewState = which_starmap;

		if (NewState == RAINBOW_MAP)
			NewState = NORMAL_STARMAP;
		else
			++NewState;

		if (NewState == WAR_ERA_STARMAP
			&& !GET_GAME_STATE (STARBASE_AVAILABLE))
		{
			++NewState;
		}
		if (NewState == HOMEWORLDS_MAP && !KNOW_A_HOMEWORLD)
		{
			if (!GET_GAME_STATE (TRADED_WITH_MELNORME))
				NewState = NORMAL_STARMAP;
			else
				++NewState;
		}
		else if (NewState == RAINBOW_MAP
				&& !GET_GAME_STATE (TRADED_WITH_MELNORME))
			NewState = NORMAL_STARMAP;

		if (NewState != which_starmap)
			which_starmap = NewState;

		PlayMenuSound (MENU_SOUND_MOVE);
		
		if (which_starmap == WAR_ERA_STARMAP) 
			show_war_era_situation = TRUE;
		else
			show_war_era_situation = FALSE;
	
		DrawStarMap (0, NULL);
		last_buf[0] = '\0';
		UpdateCursorInfo (last_buf);
		SleepThread (ONE_SECOND / 8);
	}
	else
	{
		SBYTE sx, sy;
		SIZE ZoomIn, ZoomOut;

		ZoomIn = ZoomOut = 0;
		if (PulsedInputState.menu[KEY_MENU_ZOOM_IN])
			ZoomIn = 1;
		else if (PulsedInputState.menu[KEY_MENU_ZOOM_OUT])
			ZoomOut = 1;

		ZoomStarMap (ZoomIn - ZoomOut);

		sx = sy = 0;
		if (PulsedInputState.menu[KEY_MENU_LEFT])    sx = -1;
		if (PulsedInputState.menu[KEY_MENU_RIGHT])   sx = 1;
		if (PulsedInputState.menu[KEY_MENU_UP])      sy = -1;
		if (PulsedInputState.menu[KEY_MENU_DOWN])    sy = 1;

		// Double the cursor speed when the Zoom Out key is held down
		if (DirKeysPress () && CurrentInputState.menu[KEY_MENU_ZOOM_OUT])
		{
			sx *= 2;
			sy *= 2;
		}

		if (sx != 0 || sy != 0)
		{
			UpdateCursorLocation (sx, sy, NULL);
			UpdateCursorInfo (last_buf);
			UpdateFuelRequirement ();
			isMove = TRUE;
		}

		SleepThreadUntil (TimeIn + MIN_ACCEL_DELAY);
	}

	if (isMove)
		++moveRepeats;
	else
		moveRepeats = 0;

	flashCurrentLocation (NULL, FALSE);

	return !(GLOBAL (CurrentActivity) & CHECK_ABORT);
}

static void
RepairMap (COUNT update_race, RECT *pLastRect, RECT *pNextRect)
{
	RECT r;

	/* make a rect big enough for text */
	r.extent.width = 50;
	r.corner.x = (pNextRect->corner.x + (pNextRect->extent.width >> 1))
			- (r.extent.width >> 1);
	if (r.corner.x < 0)
		r.corner.x = 0;
	else if (r.corner.x + r.extent.width >= SIS_SCREEN_WIDTH)
		r.corner.x = SIS_SCREEN_WIDTH - r.extent.width;
	r.extent.height = 9;
	r.corner.y = (pNextRect->corner.y + (pNextRect->extent.height >> 1))
			- r.extent.height;
	if (r.corner.y < 0)
		r.corner.y = 0;
	else if (r.corner.y + r.extent.height >= SIS_SCREEN_HEIGHT)
		r.corner.y = SIS_SCREEN_HEIGHT - r.extent.height;
	BoxUnion (pLastRect, &r, &r);
	BoxUnion (pNextRect, &r, &r);
	*pLastRect = *pNextRect;

	if (r.corner.x < 0)
	{
		r.extent.width += r.corner.x;
		r.corner.x = 0;
	}
	if (r.corner.x + r.extent.width > SIS_SCREEN_WIDTH)
		r.extent.width = SIS_SCREEN_WIDTH - r.corner.x;
	if (r.corner.y < 0)
	{
		r.extent.height += r.corner.y;
		r.corner.y = 0;
	}
	if (r.corner.y + r.extent.height > SIS_SCREEN_HEIGHT)
		r.extent.height = SIS_SCREEN_HEIGHT - r.corner.y;

	r.extent.height += r.corner.y & 1;
	r.corner.y &= ~1;
	
	DrawStarMap (update_race, &r);
}

static void
UpdateMap (void)
{
	BYTE ButtonState, VisibleChange;
	BOOLEAN MapDrawn, Interrupted;
	COUNT index;
	HFLEETINFO hStarShip, hNextShip;
	COUNT GrowthFactor;

	FlushInput ();
	ButtonState = 1; /* assume a button down */

	MapDrawn = Interrupted = FALSE;
	for (index = 1,
			hStarShip = GetHeadLink (&GLOBAL (avail_race_q));
			hStarShip; ++index, hStarShip = hNextShip)
	{
		FLEET_INFO *FleetPtr;

		FleetPtr = LockFleetInfo (&GLOBAL (avail_race_q), hStarShip);
		hNextShip = _GetSuccLink (FleetPtr);

		if (ButtonState)
		{
			if (!AnyButtonPress (TRUE))
				ButtonState = 0;
		}
		else if ((Interrupted = (BOOLEAN)(
				Interrupted || AnyButtonPress (TRUE)
				)))
			MapDrawn = TRUE;

		if (FleetPtr->known_strength)
		{
			SIZE dx, dy, delta;
			RECT r, last_r, temp_r0, temp_r1;

			dx = FleetPtr->loc.x - FleetPtr->known_loc.x;
			dy = FleetPtr->loc.y - FleetPtr->known_loc.y;
			if (dx || dy)
			{
				SIZE xincr, yincr,
						xerror, yerror,
						cycle;

				if (dx >= 0)
					xincr = 1;
				else
				{
					xincr = -1;
					dx = -dx;
				}
				dx <<= 1;

				if (dy >= 0)
					yincr = 1;
				else
				{
					yincr = -1;
					dy = -dy;
				}
				dy <<= 1;

				if (dx >= dy)
					cycle = dx;
				else
					cycle = dy;
				delta = xerror = yerror = cycle >> 1;

				if (!MapDrawn)
				{
					DrawStarMap ((COUNT)~0, NULL);
					MapDrawn = TRUE;
				}

				GetSphereRect (FleetPtr, &temp_r0, &last_r);
				last_r.extent.width += RES_SCALE (1);
				last_r.extent.height += RES_SCALE (1);
				VisibleChange = FALSE;
				do
				{
					do
					{
						if ((xerror -= dx) <= 0)
						{
							FleetPtr->known_loc.x += xincr;
							xerror += cycle;
						}
						if ((yerror -= dy) <= 0)
						{
							FleetPtr->known_loc.y += yincr;
							yerror += cycle;
						}
						GetSphereRect (FleetPtr, &temp_r1, &r);
					} while (delta--
							&& ((delta & 0x1F)
							|| (temp_r0.corner.x == temp_r1.corner.x
							&& temp_r0.corner.y == temp_r1.corner.y)));

					if (ButtonState)
					{
						if (!AnyButtonPress (TRUE))
							ButtonState = 0;
					}
					else if ((Interrupted = (BOOLEAN)(
								Interrupted || AnyButtonPress (TRUE)
								)))
					{
						MapDrawn = TRUE;
						goto DoneSphereMove;
					}

					r.extent.width += RES_SCALE (1);
					r.extent.height += RES_SCALE (1);
					if (temp_r0.corner.x != temp_r1.corner.x
							|| temp_r0.corner.y != temp_r1.corner.y)
					{
						VisibleChange = TRUE;
						RepairMap (index, &last_r, &r);
						SleepThread (ONE_SECOND / 24);
					}
				} while (delta >= 0);
				if (VisibleChange)
					RepairMap ((COUNT)~0, &last_r, &r);

DoneSphereMove:
				FleetPtr->known_loc = FleetPtr->loc;
			}

			delta = FleetPtr->actual_strength - FleetPtr->known_strength;
			if (delta)
			{
				if (!MapDrawn)
				{
					DrawStarMap ((COUNT)~0, NULL);
					MapDrawn = TRUE;
				}

				if (delta > 0)
					dx = 1;
				else
				{
					delta = -delta;
					dx = -1;
				}
				--delta;

				GetSphereRect (FleetPtr, &temp_r0, &last_r);
				last_r.extent.width += RES_SCALE (1);
				last_r.extent.height += RES_SCALE (1);
				VisibleChange = FALSE;

				// printf("%s: %d\n", raceName (index), FleetPtr->actual_strength);

				GrowthFactor = delta > 0 ? FleetPtr->actual_strength : FleetPtr->known_strength;

				do
				{
					do
					{
						FleetPtr->known_strength += dx;
						GetSphereRect (FleetPtr, &temp_r1, &r);
					} while (delta--
							&& ((delta & 0xF)
							|| temp_r0.extent.height == temp_r1.extent.height));

					if (ButtonState)
					{
						if (!AnyButtonPress (TRUE))
							ButtonState = 0;
					}
					else if ((Interrupted = (BOOLEAN)(
								Interrupted || AnyButtonPress (TRUE)
								)))
					{
						MapDrawn = TRUE;
						goto DoneSphereGrowth;
					}
					r.extent.width += RES_SCALE (1);
					r.extent.height += RES_SCALE (1);
					if (temp_r0.extent.height != temp_r1.extent.height)
					{
						VisibleChange = TRUE;
						RepairMap (index, &last_r, &r);
						SleepThread (ONE_SECOND / (12 + GrowthFactor / 44));
					}
				} while (delta >= 0);
				if (VisibleChange || temp_r0.extent.width != temp_r1.extent.width)
					RepairMap ((COUNT)~0, &last_r, &r);

DoneSphereGrowth:
				FleetPtr->known_strength = FleetPtr->actual_strength;
			}
		}

		UnlockFleetInfo (&GLOBAL (avail_race_q), hStarShip);
	}
}

BOOLEAN
StarMap (void)
{
	MENU_STATE MenuState;
	POINT universe;
	//FRAME OldFrame;
	RECT clip_r;
	CONTEXT OldContext;

	memset (&MenuState, 0, sizeof (MenuState));

	// JMS: For showing SC1-era starmap / starmap with constellations.
	show_war_era_situation = FALSE; 
	which_starmap = NORMAL_STARMAP;

	zoomLevel = 0;
	mapOrigin.x = MAX_X_UNIVERSE >> 1;
	mapOrigin.y = MAX_Y_UNIVERSE >> 1;
	StarMapFrame = SetAbsFrameIndex (MiscDataFrame, 48);

	if (!inHQSpace ())
		universe = CurStarDescPtr->star_pt;
	else
	{
		universe.x = LOGX_TO_UNIVERSE (GLOBAL_SIS (log_x));
		universe.y = LOGY_TO_UNIVERSE (GLOBAL_SIS (log_y));
	}

	cursorLoc = GLOBAL (autopilot);
	if (cursorLoc.x == ~0 && cursorLoc.y == ~0)
		cursorLoc = universe;

	if (optWhichMenu == OPT_PC)
	{
		if (actuallyInOrbit)
			DrawMenuStateStrings (PM_ALT_SCAN, 1);
		else
			DrawMenuStateStrings (PM_ALT_STARMAP, 0);
	}

	MenuState.InputFunc = DoMoveCursor;
	MenuState.Initialized = FALSE;

	transition_pending = TRUE;
	if (GET_GAME_STATE (ARILOU_SPACE_SIDE) <= 1)
		UpdateMap ();
	else
	{	// This zooms the Quasi map in by 2 if within the local Quasi star cluster.
		if ((universe.x <= ARILOU_HOME_X && universe.y <= ARILOU_HOME_Y) 
			&& (universe.x >= 4480 && universe.y >= 4580))
			zoomLevel = 2;
	}
	
	if (optSubmenu)
	{
		if(optCustomBorder)
		{
			if(optWhichMenu != OPT_PC)
				DrawBorder (14, FALSE);
			DrawBorder (18 + optControllerType, FALSE);
		} 
		else
			DrawSubmenu (4 + optControllerType);
	}

	DrawStarMap (0, (RECT*)-1);
	transition_pending = FALSE;
	
	BatchGraphics ();
	OldContext = SetContext (SpaceContext);
	GetContextClipRect (&clip_r);
	SetContext (OldContext);
	LoadIntoExtraScreen (&clip_r);
	DrawCursor (UNIVERSE_TO_DISPX (cursorLoc.x),
			UNIVERSE_TO_DISPY (cursorLoc.y));
	UnbatchGraphics ();

	SetMenuSounds (MENU_SOUND_NONE, MENU_SOUND_NONE);
	SetMenuRepeatDelay (MIN_ACCEL_DELAY, MAX_ACCEL_DELAY, STEP_ACCEL_DELAY,
			TRUE);
#if defined(ANDROID) || defined(__ANDROID__)
	TFB_SetOnScreenKeyboard_Starmap();
	DoInput(&MenuState, FALSE);
	TFB_SetOnScreenKeyboard_Menu();
#else // ANDROID
	DoInput(&MenuState, FALSE);
#endif
	SetMenuSounds (MENU_SOUND_ARROWS, MENU_SOUND_SELECT);
	SetDefaultMenuRepeatDelay ();

	DrawHyperCoords (universe);
	DrawSISMessage (NULL);
	DrawStatusMessage (NULL);

	if (optSubmenu)
		DrawMineralHelpers (TRUE);

	if (GLOBAL (autopilot.x) == universe.x
			&& GLOBAL (autopilot.y) == universe.y)
		GLOBAL (autopilot.x) = GLOBAL (autopilot.y) = ~0;

	return (GLOBAL (autopilot.x) != ~0
			&& GLOBAL (autopilot.y) != ~0);
}
