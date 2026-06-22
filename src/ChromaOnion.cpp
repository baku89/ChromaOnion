/*
	ChromaOnion.cpp

	An onion-skin preview effect for After Effects.

	Composites a user-defined range of frames before and after the current
	time (like the standard Echo effect, but with independent before/after
	ranges). Optional modes:
	  - Opacity : plain reduced-opacity ghosting.
	  - Chroma  : rainbow tint — past frames tend toward red, future frames
	              toward blue, so motion direction reads as colour.
	  - Edge    : edge-detection overlay for clearer silhouettes.
	Chroma and Edge can be combined.

	MIT License. See LICENSE.
*/

#include "ChromaOnion.h"

#include <vector>
#include <algorithm>
#include <cmath>

/* ------------------------------------------------------------------ */
/* Helpers                                                            */
/* ------------------------------------------------------------------ */

static inline double clamp01(double v)
{
	return v < 0.0 ? 0.0 : (v > 1.0 ? 1.0 : v);
}

/* Full-saturation, full-value HSV -> RGB. hue in [0,360). */
static void HueToRGB(double hue, double *r, double *g, double *b)
{
	double h = hue / 60.0;
	int    i = (int)std::floor(h) % 6;
	if (i < 0) i += 6;
	double f = h - std::floor(h);
	double q = 1.0 - f;

	switch (i) {
		case 0: *r = 1;  *g = f;  *b = 0;  break;
		case 1: *r = q;  *g = 1;  *b = 0;  break;
		case 2: *r = 0;  *g = 1;  *b = f;  break;
		case 3: *r = 0;  *g = q;  *b = 1;  break;
		case 4: *r = f;  *g = 0;  *b = 1;  break;
		default:*r = 1;  *g = 0;  *b = q;  break;
	}
}

struct GhostOptions {
	double	weight;		// overall alpha multiplier 0..1
	bool	chroma;		// apply rainbow tint
	double	tintAmt;	// 0..1
	bool	edge;		// edge-detection overlay
	double	tintR, tintG, tintB;	// tint colour (chroma)
};

template <typename PixT>
static inline double SampleLuma(const PF_EffectWorld *w, int x, int y, double maxv)
{
	if (x < 0) x = 0; else if (x >= w->width)  x = w->width  - 1;
	if (y < 0) y = 0; else if (y >= w->height) y = w->height - 1;
	const PixT *p = (const PixT *)((const char *)w->data + (size_t)y * w->rowbytes) + x;
	return (0.299 * p->red + 0.587 * p->green + 0.114 * p->blue) / maxv;
}

/* Simple central-difference edge magnitude of luma, 0..~1. */
template <typename PixT>
static inline double EdgeMag(const PF_EffectWorld *w, int x, int y, double maxv)
{
	double gx = SampleLuma<PixT>(w, x + 1, y, maxv) - SampleLuma<PixT>(w, x - 1, y, maxv);
	double gy = SampleLuma<PixT>(w, x, y + 1, maxv) - SampleLuma<PixT>(w, x, y - 1, maxv);
	double mag = std::sqrt(gx * gx + gy * gy) * 3.0;	// gain so edges read clearly
	return clamp01(mag);
}

/* Composite one checked-out source world onto the destination ("source over"). */
template <typename PixT>
static void CompositeWorld(const PF_EffectWorld *src,
						   PF_EffectWorld       *dst,
						   double                maxv,
						   const GhostOptions   &o)
{
	int width  = src->width  < dst->width  ? src->width  : dst->width;
	int height = src->height < dst->height ? src->height : dst->height;

	for (int y = 0; y < height; ++y) {
		const PixT *srow = (const PixT *)((const char *)src->data + (size_t)y * src->rowbytes);
		PixT       *drow = (PixT *)((char *)dst->data + (size_t)y * dst->rowbytes);

		for (int x = 0; x < width; ++x) {
			const PixT *sp = srow + x;

			double sa = sp->alpha / maxv;
			double sr = sp->red   / maxv;
			double sg = sp->green / maxv;
			double sb = sp->blue  / maxv;

			double cr = sr, cg = sg, cb = sb, ca = sa;

			if (o.edge) {
				double e = EdgeMag<PixT>(src, x, y, maxv);
				ca = sa * e;					// edges only, where the frame is opaque
				if (o.chroma) { cr = o.tintR; cg = o.tintG; cb = o.tintB; }
				else          { cr = cg = cb = 1.0; }
			} else if (o.chroma) {
				double L  = 0.299 * sr + 0.587 * sg + 0.114 * sb;
				double tr = L * o.tintR, tg = L * o.tintG, tb = L * o.tintB;
				cr = sr + (tr - sr) * o.tintAmt;
				cg = sg + (tg - sg) * o.tintAmt;
				cb = sb + (tb - sb) * o.tintAmt;
			}

			double ae = ca * o.weight;
			if (ae <= 0.0) continue;

			PixT *dp = drow + x;
			double da = dp->alpha / maxv;
			double dr = dp->red   / maxv;
			double dg = dp->green / maxv;
			double db = dp->blue  / maxv;

			double inv = 1.0 - ae;
			double oa = ae + da * inv;
			double orr = cr * ae + dr * inv;
			double og  = cg * ae + dg * inv;
			double ob  = cb * ae + db * inv;

			dp->alpha = (decltype(dp->alpha))(clamp01(oa)  * maxv + 0.5);
			dp->red   = (decltype(dp->red))  (clamp01(orr) * maxv + 0.5);
			dp->green = (decltype(dp->green))(clamp01(og)  * maxv + 0.5);
			dp->blue  = (decltype(dp->blue)) (clamp01(ob)  * maxv + 0.5);
		}
	}
}

static void CompositeDispatch(const PF_EffectWorld *src,
							  PF_EffectWorld       *dst,
							  bool                  deep,
							  const GhostOptions   &o)
{
	if (deep) CompositeWorld<PF_Pixel16>(src, dst, (double)PF_MAX_CHAN16, o);
	else      CompositeWorld<PF_Pixel8> (src, dst, (double)PF_MAX_CHAN8,  o);
}

/* ------------------------------------------------------------------ */
/* Effect entry points                                                */
/* ------------------------------------------------------------------ */

static PF_Err
About(PF_InData *in_data, PF_OutData *out_data, PF_ParamDef *params[], PF_LayerDef *output)
{
	PF_SPRINTF(out_data->return_msg, "%s, v%d.%d\r%s",
			   NAME, MAJOR_VERSION, MINOR_VERSION, DESCRIPTION);
	return PF_Err_NONE;
}

static PF_Err
GlobalSetup(PF_InData *in_data, PF_OutData *out_data, PF_ParamDef *params[], PF_LayerDef *output)
{
	out_data->my_version = PF_VERSION(MAJOR_VERSION, MINOR_VERSION,
									  BUG_VERSION, STAGE_VERSION, BUILD_VERSION);

	out_data->out_flags  = PF_OutFlag_WIDE_TIME_INPUT |	// we sample other times
						   PF_OutFlag_DEEP_COLOR_AWARE;	// 8- and 16-bpc

	out_data->out_flags2 = PF_OutFlag2_SUPPORTS_THREADED_RENDERING;

	return PF_Err_NONE;
}

static PF_Err
ParamsSetup(PF_InData *in_data, PF_OutData *out_data, PF_ParamDef *params[], PF_LayerDef *output)
{
	PF_Err		err = PF_Err_NONE;
	PF_ParamDef	def;

	AEFX_CLR_STRUCT(def);
	PF_ADD_SLIDER("Frames Before", FRAMES_BEFORE_MIN, FRAMES_BEFORE_MAX,
				  FRAMES_BEFORE_MIN, FRAMES_BEFORE_MAX, FRAMES_BEFORE_DFLT,
				  FRAMES_BEFORE_DISK_ID);

	AEFX_CLR_STRUCT(def);
	PF_ADD_SLIDER("Frames After", FRAMES_AFTER_MIN, FRAMES_AFTER_MAX,
				  FRAMES_AFTER_MIN, FRAMES_AFTER_MAX, FRAMES_AFTER_DFLT,
				  FRAMES_AFTER_DISK_ID);

	AEFX_CLR_STRUCT(def);
	PF_ADD_SLIDER("Frame Step", FRAME_STEP_MIN, FRAME_STEP_MAX,
				  FRAME_STEP_MIN, FRAME_STEP_MAX, FRAME_STEP_DFLT,
				  FRAME_STEP_DISK_ID);

	AEFX_CLR_STRUCT(def);
	PF_ADD_POPUP("Color Mode", 2, COLOR_MODE_OPACITY,
				 "Opacity|Chroma (Rainbow)", COLOR_MODE_DISK_ID);

	AEFX_CLR_STRUCT(def);
	PF_ADD_SLIDER("Onion Opacity", ONION_OPACITY_MIN, ONION_OPACITY_MAX,
				  ONION_OPACITY_MIN, ONION_OPACITY_MAX, ONION_OPACITY_DFLT,
				  ONION_OPACITY_DISK_ID);

	AEFX_CLR_STRUCT(def);
	PF_ADD_CHECKBOX("Fade By Distance", "", TRUE, 0, FADE_BY_DISTANCE_DISK_ID);

	AEFX_CLR_STRUCT(def);
	PF_ADD_SLIDER("Tint Amount", TINT_AMOUNT_MIN, TINT_AMOUNT_MAX,
				  TINT_AMOUNT_MIN, TINT_AMOUNT_MAX, TINT_AMOUNT_DFLT,
				  TINT_AMOUNT_DISK_ID);

	AEFX_CLR_STRUCT(def);
	PF_ADD_CHECKBOX("Edge Detect", "", FALSE, 0, EDGE_DETECT_DISK_ID);

	AEFX_CLR_STRUCT(def);
	PF_ADD_CHECKBOX("Show Current Frame", "", TRUE, 0, SHOW_CURRENT_DISK_ID);

	out_data->num_params = CO_NUM_PARAMS;
	return err;
}

struct Ghost {
	A_long	signedFrames;	// offset in frames (negative = past)
	A_long	dist;			// absolute distance in frames
	double	hue;			// tint hue
};

static PF_Err
Render(PF_InData *in_data, PF_OutData *out_data, PF_ParamDef *params[], PF_LayerDef *output)
{
	PF_Err err = PF_Err_NONE, err2 = PF_Err_NONE;

	A_long before  = params[CO_FRAMES_BEFORE]->u.sd.value;
	A_long after   = params[CO_FRAMES_AFTER]->u.sd.value;
	A_long step    = params[CO_FRAME_STEP]->u.sd.value;
	if (step < 1) step = 1;

	A_long colorMode = params[CO_COLOR_MODE]->u.pd.value;
	double onionOp   = params[CO_ONION_OPACITY]->u.sd.value / 100.0;
	bool   fade      = params[CO_FADE_BY_DISTANCE]->u.bd.value != 0;
	double tintAmt   = params[CO_TINT_AMOUNT]->u.sd.value / 100.0;
	bool   edge      = params[CO_EDGE_DETECT]->u.bd.value != 0;
	bool   showCur   = params[CO_SHOW_CURRENT]->u.bd.value != 0;
	bool   chroma    = (colorMode == COLOR_MODE_CHROMA);

	bool   deep      = PF_WORLD_IS_DEEP(output);

	/* Start from transparent black. */
	ERR(PF_FILL(NULL, NULL, output));

	A_long maxDist = (before > after ? before : after) * step;

	/* Build the ghost list. */
	std::vector<Ghost> ghosts;
	for (A_long i = 1; i <= before; ++i) {
		A_long fo = i * step;
		Ghost g;
		g.signedFrames = -fo;
		g.dist         = fo;
		double t = (maxDist > 0) ? (double)(-fo) / (double)maxDist : 0.0;	// -1..0
		g.hue = (t + 1.0) * 0.5 * 240.0;	// past -> red(0)
		ghosts.push_back(g);
	}
	for (A_long i = 1; i <= after; ++i) {
		A_long fo = i * step;
		Ghost g;
		g.signedFrames = fo;
		g.dist         = fo;
		double t = (maxDist > 0) ? (double)(fo) / (double)maxDist : 0.0;		// 0..1
		g.hue = (t + 1.0) * 0.5 * 240.0;	// future -> blue(240)
		ghosts.push_back(g);
	}

	/* Draw farthest first so nearer frames land on top. */
	std::sort(ghosts.begin(), ghosts.end(),
			  [](const Ghost &a, const Ghost &b) { return a.dist > b.dist; });

	for (size_t n = 0; n < ghosts.size() && !err; ++n) {
		const Ghost &g = ghosts[n];

		PF_ParamDef cp;
		AEFX_CLR_STRUCT(cp);

		A_long when = in_data->current_time + g.signedFrames * in_data->time_step;

		ERR(PF_CHECKOUT_PARAM(in_data, CO_INPUT, when,
							  in_data->time_step, in_data->time_scale, &cp));

		if (!err && cp.u.ld.data) {
			GhostOptions o;
			o.chroma  = chroma;
			o.tintAmt = tintAmt;
			o.edge    = edge;
			HueToRGB(g.hue, &o.tintR, &o.tintG, &o.tintB);

			double w = onionOp;
			if (fade && maxDist > 0) {
				double frac = (double)g.dist / (double)maxDist;	// (0,1]
				w *= (1.0 - 0.75 * frac);
			}
			o.weight = clamp01(w);

			CompositeDispatch(&cp.u.ld, output, deep, o);
		}

		ERR2(PF_CHECKIN_PARAM(in_data, &cp));
	}

	/* Current frame on top, untinted, full opacity. */
	if (!err && showCur && params[CO_INPUT]->u.ld.data) {
		GhostOptions o;
		o.weight = 1.0;
		o.chroma = false;
		o.tintAmt = 0.0;
		o.edge = false;
		o.tintR = o.tintG = o.tintB = 1.0;
		CompositeDispatch(&params[CO_INPUT]->u.ld, output, deep, o);
	}

	return err;
}

extern "C" DllExport
PF_Err PluginDataEntryFunction2(
	PF_PluginDataPtr	inPtr,
	PF_PluginDataCB2	inPluginDataCallBackPtr,
	SPBasicSuite		*inSPBasicSuitePtr,
	const char			*inHostName,
	const char			*inHostVersion)
{
	PF_Err result = PF_Err_INVALID_CALLBACK;

	result = PF_REGISTER_EFFECT_EXT2(
		inPtr,
		inPluginDataCallBackPtr,
		NAME,			// Name
		MATCH_NAME,		// Match Name
		CATEGORY,		// Category
		AE_RESERVED_INFO,
		"EffectMain",	// Entry point
		"https://github.com/baku89/ChromaOnion");	// support URL

	return result;
}

PF_Err
EffectMain(
	PF_Cmd		cmd,
	PF_InData	*in_data,
	PF_OutData	*out_data,
	PF_ParamDef	*params[],
	PF_LayerDef	*output)
{
	PF_Err err = PF_Err_NONE;

	try {
		switch (cmd) {
			case PF_Cmd_ABOUT:        err = About(in_data, out_data, params, output);       break;
			case PF_Cmd_GLOBAL_SETUP: err = GlobalSetup(in_data, out_data, params, output); break;
			case PF_Cmd_PARAMS_SETUP: err = ParamsSetup(in_data, out_data, params, output); break;
			case PF_Cmd_RENDER:       err = Render(in_data, out_data, params, output);      break;
			default: break;
		}
	} catch (PF_Err &thrown_err) {
		err = thrown_err;
	}

	return err;
}
