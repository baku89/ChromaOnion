/*
	ChromaOnion.h

	An onion-skin preview effect for After Effects.
	Composites a range of frames before and after the current time, with
	optional rainbow (chroma) tinting and edge-detection overlay.

	MIT License. See LICENSE.
*/

#pragma once

#ifndef CHROMAONION_H
#define CHROMAONION_H

#include "AEConfig.h"
#include "entry.h"
#include "AE_Effect.h"
#include "AE_EffectCB.h"
#include "AE_Macros.h"
#include "AE_EffectSuites.h"
#include "Param_Utils.h"
#include "AEFX_SuiteHelper.h"

#define MAJOR_VERSION	0
#define MINOR_VERSION	1
#define BUG_VERSION		0
#define STAGE_VERSION	PF_Stage_DEVELOP
#define BUILD_VERSION	6

#define NAME			"ChromaOnion"
#define DESCRIPTION		"Onion-skin preview: composite frames before/after the current time, \
with optional rainbow tint and edge detection.\rMIT License."
#define MATCH_NAME		"BAKU ChromaOnion"
#define CATEGORY		"Utility"

/* Parameter order. INPUT is always index 0. */
enum {
	CO_INPUT = 0,
	CO_FRAMES_BEFORE,
	CO_FRAMES_AFTER,
	CO_TINT,			// 0..100: 0 = Opacity look, 100 = Chroma (additive rainbow)
	CO_ONION_OPACITY,	// 0..100
	CO_FADE_BY_DISTANCE,// checkbox
	CO_EDGE_DETECT,		// checkbox
	CO_EDGE_INTENSITY,	// float slider (edge gain)
	CO_NUM_PARAMS
};

/* Unique, stable disk IDs for each param (gaps left for removed params). */
enum {
	FRAMES_BEFORE_DISK_ID = 1,
	FRAMES_AFTER_DISK_ID = 2,
	TINT_DISK_ID = 7,
	ONION_OPACITY_DISK_ID = 5,
	FADE_BY_DISTANCE_DISK_ID = 6,
	EDGE_DETECT_DISK_ID = 8,
	EDGE_INTENSITY_DISK_ID = 9
};

/* Parameter ranges. */
#define FRAMES_BEFORE_MIN	0
#define FRAMES_BEFORE_MAX	30
#define FRAMES_BEFORE_DFLT	1

#define FRAMES_AFTER_MIN	0
#define FRAMES_AFTER_MAX	30
#define FRAMES_AFTER_DFLT	1

#define TINT_MIN			0
#define TINT_MAX			100
#define TINT_DFLT			100

#define ONION_OPACITY_MIN	0
#define ONION_OPACITY_MAX	100
#define ONION_OPACITY_DFLT	100

#define EDGE_INTENSITY_MIN	0.0
#define EDGE_INTENSITY_MAX	8.0
#define EDGE_INTENSITY_DFLT	2.0

extern "C" {

	DllExport
	PF_Err
	EffectMain (
		PF_Cmd			cmd,
		PF_InData		*in_data,
		PF_OutData		*out_data,
		PF_ParamDef		*params[],
		PF_LayerDef		*output,
		void			*extra );

}

#endif // CHROMAONION_H
