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
#include "AE_EffectCBSuites.h"

#include <vector>
#include <algorithm>
#include <cmath>
#include <type_traits>

/* ------------------------------------------------------------------ */
/* Helpers                                                            */
/* ------------------------------------------------------------------ */

static inline double clamp01(double v)
{
	return v < 0.0 ? 0.0 : (v > 1.0 ? 1.0 : v);
}

enum PixelDepth { PD_8 = 0, PD_16, PD_32 };

static inline double DepthMax(PixelDepth d)
{
	return d == PD_32 ? 1.0 : (d == PD_16 ? (double)PF_MAX_CHAN16 : (double)PF_MAX_CHAN8);
}

/* Write a normalised [0,1] channel value into the destination type:
   round+scale for integer worlds, store directly (clamped) for float. */
template <typename C>
static inline C StoreChan(double norm, double maxv)
{
	if (std::is_floating_point<C>::value)
		return (C)(clamp01(norm));
	else
		return (C)(clamp01(norm) * maxv + 0.5);
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

enum CompMode {
	COMP_OVER,	// source-over with alpha (Opacity mode + current base)
	COMP_ADD	// additive, channel-masked (Chroma mode)
};

struct GhostOptions {
	CompMode	mode;
	bool		edge;		// edge-detection overlay
	double		edgeGain;	// edge detection gain

	/* COMP_OVER */
	double		weight;		// source alpha multiplier 0..1

	/* COMP_ADD: per-channel mask (already normalised so the weighted sum of all
	   frames' masks is 1 per channel) and an alpha weight. */
	double		maskR, maskG, maskB;
	double		alphaW;
};

template <typename PixT>
static inline double SampleLuma(const PF_EffectWorld *w, int x, int y, double maxv)
{
	if (x < 0) x = 0; else if (x >= w->width)  x = w->width  - 1;
	if (y < 0) y = 0; else if (y >= w->height) y = w->height - 1;
	const PixT *p = (const PixT *)((const char *)w->data + (size_t)y * w->rowbytes) + x;
	return (0.299 * p->red + 0.587 * p->green + 0.114 * p->blue) / maxv;
}

/* Sobel edge magnitude of luma, 0..1, scaled by gain. */
template <typename PixT>
static inline double EdgeMag(const PF_EffectWorld *w, int x, int y, double maxv, double gain)
{
	double l00 = SampleLuma<PixT>(w, x - 1, y - 1, maxv);
	double l10 = SampleLuma<PixT>(w, x,     y - 1, maxv);
	double l20 = SampleLuma<PixT>(w, x + 1, y - 1, maxv);
	double l01 = SampleLuma<PixT>(w, x - 1, y,     maxv);
	double l21 = SampleLuma<PixT>(w, x + 1, y,     maxv);
	double l02 = SampleLuma<PixT>(w, x - 1, y + 1, maxv);
	double l12 = SampleLuma<PixT>(w, x,     y + 1, maxv);
	double l22 = SampleLuma<PixT>(w, x + 1, y + 1, maxv);

	double gx = (l20 + 2 * l21 + l22) - (l00 + 2 * l01 + l02);
	double gy = (l02 + 2 * l12 + l22) - (l00 + 2 * l10 + l20);
	double mag = std::sqrt(gx * gx + gy * gy) * gain;
	return clamp01(mag);
}

/* Composite one source world onto the destination. */
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

			double e = o.edge ? EdgeMag<PixT>(src, x, y, maxv, o.edgeGain) : 1.0;

			PixT  *dp = drow + x;
			double da = dp->alpha / maxv;
			double dr = dp->red   / maxv;
			double dg = dp->green / maxv;
			double db = dp->blue  / maxv;

			if (o.mode == COMP_ADD) {
				/* Additive, channel-masked. Where every frame agrees the masks
				   sum to 1 per channel, so the original colour is reconstructed;
				   motion shows up as coloured fringes. */
				double base_r, base_g, base_b, aIn;
				if (o.edge) {
					base_r = base_g = base_b = e;	// coloured edges
					aIn = sa * e;
				} else {
					base_r = sr; base_g = sg; base_b = sb;
					aIn = sa;
				}
				double orr = dr + base_r * o.maskR;
				double og  = dg + base_g * o.maskG;
				double ob  = db + base_b * o.maskB;
				double oa  = da + aIn * o.alphaW;

				dp->alpha = StoreChan<decltype(dp->alpha)>(oa,  maxv);
				dp->red   = StoreChan<decltype(dp->red)>  (orr, maxv);
				dp->green = StoreChan<decltype(dp->green)>(og,  maxv);
				dp->blue  = StoreChan<decltype(dp->blue)> (ob,  maxv);
			} else {
				/* Source-over. */
				double cr = sr, cg = sg, cb = sb, ca = sa;
				if (o.edge) {
					ca = sa * e;
					cr = cg = cb = 1.0;		// white edges
				}
				double ae = ca * o.weight;
				if (ae <= 0.0) continue;

				double inv = 1.0 - ae;
				double oa  = ae + da * inv;
				double orr = cr * ae + dr * inv;
				double og  = cg * ae + dg * inv;
				double ob  = cb * ae + db * inv;

				dp->alpha = StoreChan<decltype(dp->alpha)>(oa,  maxv);
				dp->red   = StoreChan<decltype(dp->red)>  (orr, maxv);
				dp->green = StoreChan<decltype(dp->green)>(og,  maxv);
				dp->blue  = StoreChan<decltype(dp->blue)> (ob,  maxv);
			}
		}
	}
}

static void CompositeDispatch(const PF_EffectWorld *src,
							  PF_EffectWorld       *dst,
							  PixelDepth            depth,
							  const GhostOptions   &o)
{
	double maxv = DepthMax(depth);
	switch (depth) {
		case PD_32: CompositeWorld<PF_Pixel32>(src, dst, maxv, o); break;
		case PD_16: CompositeWorld<PF_Pixel16>(src, dst, maxv, o); break;
		default:    CompositeWorld<PF_Pixel8> (src, dst, maxv, o); break;
	}
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

	out_data->out_flags2 = PF_OutFlag2_SUPPORTS_SMART_RENDER |	// SmartFX (enables 32-bpc)
						   PF_OutFlag2_FLOAT_COLOR_AWARE     |	// 32-bpc float
						   PF_OutFlag2_SUPPORTS_THREADED_RENDERING;

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
	PF_ADD_SLIDER("Tint", TINT_MIN, TINT_MAX,
				  TINT_MIN, TINT_MAX, TINT_DFLT, TINT_DISK_ID);

	AEFX_CLR_STRUCT(def);
	PF_ADD_SLIDER("Onion Opacity", ONION_OPACITY_MIN, ONION_OPACITY_MAX,
				  ONION_OPACITY_MIN, ONION_OPACITY_MAX, ONION_OPACITY_DFLT,
				  ONION_OPACITY_DISK_ID);

	AEFX_CLR_STRUCT(def);
	PF_ADD_CHECKBOX("Fade By Distance", "", TRUE, 0, FADE_BY_DISTANCE_DISK_ID);

	AEFX_CLR_STRUCT(def);
	PF_ADD_CHECKBOX("Edge Detect", "", FALSE, 0, EDGE_DETECT_DISK_ID);

	AEFX_CLR_STRUCT(def);
	PF_ADD_FLOAT_SLIDERX("Edge Intensity",
						 EDGE_INTENSITY_MIN, EDGE_INTENSITY_MAX,
						 EDGE_INTENSITY_MIN, EDGE_INTENSITY_MAX,
						 EDGE_INTENSITY_DFLT, 1, PF_ValueDisplayFlag_NONE, 0,
						 EDGE_INTENSITY_DISK_ID);

	out_data->num_params = CO_NUM_PARAMS;
	return err;
}

struct Ghost {
	A_long	signedFrames;	// offset in frames (negative = past)
	A_long	dist;			// absolute distance in frames
	double	hue;			// tint hue (Chroma)
	double	weight;			// onion opacity * distance fade
};

/* dst = a*(1-t) + b*t, straight per-channel. a may alias dst. */
template <typename PixT>
static void LerpWorldT(const PF_EffectWorld *a, const PF_EffectWorld *b,
					   PF_EffectWorld *dst, double maxv, double t)
{
	int width  = dst->width;
	int height = dst->height;
	if (a->width  < width)  width  = a->width;
	if (b->width  < width)  width  = b->width;
	if (a->height < height) height = a->height;
	if (b->height < height) height = b->height;

	double it = 1.0 - t;
	for (int y = 0; y < height; ++y) {
		const PixT *ar = (const PixT *)((const char *)a->data + (size_t)y * a->rowbytes);
		const PixT *br = (const PixT *)((const char *)b->data + (size_t)y * b->rowbytes);
		PixT       *dr = (PixT *)((char *)dst->data + (size_t)y * dst->rowbytes);
		for (int x = 0; x < width; ++x) {
			dr[x].alpha = StoreChan<decltype(dr[x].alpha)>((ar[x].alpha * it + br[x].alpha * t) / maxv, maxv);
			dr[x].red   = StoreChan<decltype(dr[x].red)>  ((ar[x].red   * it + br[x].red   * t) / maxv, maxv);
			dr[x].green = StoreChan<decltype(dr[x].green)>((ar[x].green * it + br[x].green * t) / maxv, maxv);
			dr[x].blue  = StoreChan<decltype(dr[x].blue)> ((ar[x].blue  * it + br[x].blue  * t) / maxv, maxv);
		}
	}
}

static void LerpWorld(const PF_EffectWorld *a, const PF_EffectWorld *b,
					  PF_EffectWorld *dst, PixelDepth depth, double t)
{
	double maxv = DepthMax(depth);
	switch (depth) {
		case PD_32: LerpWorldT<PF_Pixel32>(a, b, dst, maxv, t); break;
		case PD_16: LerpWorldT<PF_Pixel16>(a, b, dst, maxv, t); break;
		default:    LerpWorldT<PF_Pixel8> (a, b, dst, maxv, t); break;
	}
}

/*
	Render one onion-skin look into dst.

	chroma == false (Opacity): the current frame is the opaque base, ghost frames
	         are overlaid "over" at reduced opacity.

	chroma == true  (Chroma): every frame — including the current one as the
	         middle (green) frame — is tinted red(past)->green(now)->blue(future)
	         and combined ADDITIVELY, with per-channel masks normalised so that
	         where all frames agree the original colour is reconstructed and
	         motion shows as coloured fringes.

	The continuous Tint parameter cross-fades between these two looks.
*/
/* Parameters read once for a render. */
struct OnionParams {
	A_long	before;
	A_long	after;
	double	tint;		// 0 = Opacity, 1 = Chroma
	double	onionOp;
	bool	fade;
	bool	edge;
	double	edgeGain;
};

/*
	Render one onion-skin look into dst, pulling each frame from worlds[] where
	index = signedFrames + before (so the current frame is worlds[before]).

	chroma == false (Opacity): the current frame is the opaque base, ghost frames
	         are overlaid "over" at reduced opacity.

	chroma == true  (Chroma): every frame — including the current one as the
	         middle (green) frame — is tinted red(past)->green(now)->blue(future)
	         and combined ADDITIVELY, with per-channel masks normalised so that
	         where all frames agree the original colour is reconstructed and
	         motion shows as coloured fringes.

	The continuous Tint parameter cross-fades between these two looks.
*/
static PF_Err
ComposeOnion(PF_InData *in_data, PF_EffectWorld **worlds,
			 PF_EffectWorld *dst, PixelDepth depth,
			 const OnionParams &p, bool chroma)
{
	PF_Err err = PF_Err_NONE;

	A_long maxDist = (p.before > p.after ? p.before : p.after);
	PF_EffectWorld *current = worlds[p.before];	// offset 0

	/* Start from transparent black. */
	ERR(PF_FILL(NULL, NULL, dst));

	/*
		Lay down the current frame as an opaque, untinted base when:
		  - Opacity mode (always), or
		  - Edge Detect is on (so the current frame stays as-is and the
		    surrounding frames' edges are overlaid on top — never green-tinted).
		Otherwise (pure Chroma) the current frame is blended into the additive
		stack as the middle/green frame instead.
	*/
	bool currentAsBase = (!chroma) || p.edge;

	if (currentAsBase && !err && current && current->data) {
		GhostOptions o;
		AEFX_CLR_STRUCT(o);
		o.mode   = COMP_OVER;
		o.weight = 1.0;
		o.edge   = false;
		CompositeDispatch(current, dst, depth, o);
	}

	/* Build the frame list (one frame per step of 1). */
	std::vector<Ghost> ghosts;
	for (A_long i = 1; i <= p.before; ++i) {
		Ghost g;
		g.signedFrames = -i;
		g.dist         = i;
		double t = (maxDist > 0) ? (double)(-i) / (double)maxDist : 0.0;	// -1..0
		g.hue = (t + 1.0) * 0.5 * 240.0;	// past -> red(0)
		ghosts.push_back(g);
	}
	for (A_long i = 1; i <= p.after; ++i) {
		Ghost g;
		g.signedFrames = i;
		g.dist         = i;
		double t = (maxDist > 0) ? (double)(i) / (double)maxDist : 0.0;	// 0..1
		g.hue = (t + 1.0) * 0.5 * 240.0;	// future -> blue(240)
		ghosts.push_back(g);
	}
	/* Pure Chroma blends the current frame as the middle (green) frame. When it
	   is drawn as the untinted base (Opacity, or Edge Detect) we skip it here. */
	if (chroma && !currentAsBase) {
		Ghost g;
		g.signedFrames = 0;
		g.dist         = 0;
		g.hue          = 120.0;	// green = centre of the past->future sweep
		ghosts.push_back(g);
	}

	/* Per-frame weight (onion opacity * distance fade). */
	for (size_t n = 0; n < ghosts.size(); ++n) {
		double w = p.onionOp;
		if (p.fade && maxDist > 0) {
			double frac = (double)ghosts[n].dist / (double)maxDist;	// [0,1]
			w *= (1.0 - 0.75 * frac);
		}
		ghosts[n].weight = clamp01(w);
	}

	/* Chroma: accumulate the per-channel mask normaliser so identical frames
	   reconstruct the original colour. */
	double sMaskR = 0, sMaskG = 0, sMaskB = 0, sumW = 0;
	if (chroma) {
		for (size_t n = 0; n < ghosts.size(); ++n) {
			double mr, mg, mb;
			HueToRGB(ghosts[n].hue, &mr, &mg, &mb);
			double w = ghosts[n].weight;
			sMaskR += mr * w; sMaskG += mg * w; sMaskB += mb * w;
			sumW   += w;
		}
	}

	/* Draw farthest first so nearer frames land on top (matters for "over"). */
	std::sort(ghosts.begin(), ghosts.end(),
			  [](const Ghost &a, const Ghost &b) { return a.dist > b.dist; });

	for (size_t n = 0; n < ghosts.size(); ++n) {
		const Ghost &g = ghosts[n];
		PF_EffectWorld *src = worlds[g.signedFrames + p.before];
		if (!src || !src->data) continue;

		GhostOptions o;
		AEFX_CLR_STRUCT(o);
		/* The current frame is shown as-is; only the surrounding frames get
		   edge detection, overlaid on top of it. */
		o.edge     = p.edge && (g.signedFrames != 0);
		o.edgeGain = p.edgeGain;

		if (chroma) {
			o.mode = COMP_ADD;
			double mr, mg, mb;
			HueToRGB(g.hue, &mr, &mg, &mb);
			o.maskR  = (sMaskR > 1e-6) ? mr * g.weight / sMaskR : 0.0;
			o.maskG  = (sMaskG > 1e-6) ? mg * g.weight / sMaskG : 0.0;
			o.maskB  = (sMaskB > 1e-6) ? mb * g.weight / sMaskB : 0.0;
			o.alphaW = (sumW   > 1e-6) ? g.weight / sumW         : 0.0;
		} else {
			o.mode   = COMP_OVER;
			o.weight = g.weight;
		}

		CompositeDispatch(src, dst, depth, o);
	}

	return err;
}

/* Render both looks (as needed) and cross-fade by Tint into the output. */
static PF_Err
RenderOnion(PF_InData *in_data, PF_OutData *out_data, const PF_WorldSuite2 *wsP,
			PF_EffectWorld **worlds, PF_EffectWorld *output,
			PixelDepth depth, PF_PixelFormat format, const OnionParams &p)
{
	PF_Err err = PF_Err_NONE;
	const double EPS = 0.001;

	if (p.tint <= EPS) {
		err = ComposeOnion(in_data, worlds, output, depth, p, false);
	} else if (p.tint >= 1.0 - EPS) {
		err = ComposeOnion(in_data, worlds, output, depth, p, true);
	} else {
		/* Cross-fade: Opacity look in output, Chroma look in a temp world. */
		err = ComposeOnion(in_data, worlds, output, depth, p, false);
		if (!err) {
			PF_EffectWorld temp;
			AEFX_CLR_STRUCT(temp);
			err = wsP->PF_NewWorld(in_data->effect_ref, output->width, output->height,
								   TRUE, format, &temp);
			if (!err) {
				err = ComposeOnion(in_data, worlds, &temp, depth, p, true);
				if (!err) LerpWorld(output, &temp, output, depth, p.tint);
				wsP->PF_DisposeWorld(in_data->effect_ref, &temp);
			}
		}
	}
	return err;
}

/* Read a slider (int) or checkbox param at the current time as a long. */
static A_long ReadIntParam(PF_InData *in_data, int index)
{
	PF_ParamDef def; AEFX_CLR_STRUCT(def);
	A_long v = 0;
	if (!PF_CHECKOUT_PARAM(in_data, index, in_data->current_time,
						   in_data->time_step, in_data->time_scale, &def)) {
		switch (def.param_type) {
			case PF_Param_CHECKBOX:     v = def.u.bd.value; break;
			case PF_Param_FLOAT_SLIDER: v = (A_long)def.u.fs_d.value; break;
			default:                    v = def.u.sd.value; break;	// PF_Param_SLIDER
		}
	}
	PF_CHECKIN_PARAM(in_data, &def);
	return v;
}

/* Merge src into dst (both PF_LRect); empties handled. */
static void UnionLRect(const PF_LRect *src, PF_LRect *dst)
{
	bool srcEmpty = (src->left >= src->right) || (src->top >= src->bottom);
	if (srcEmpty) return;
	bool dstEmpty = (dst->left >= dst->right) || (dst->top >= dst->bottom);
	if (dstEmpty) { *dst = *src; return; }
	if (src->left   < dst->left)   dst->left   = src->left;
	if (src->top    < dst->top)    dst->top    = src->top;
	if (src->right  > dst->right)  dst->right  = src->right;
	if (src->bottom > dst->bottom) dst->bottom = src->bottom;
}

static PF_Err
PreRender(PF_InData *in_data, PF_OutData *out_data, PF_PreRenderExtra *extra)
{
	PF_Err err = PF_Err_NONE;

	A_long before = ReadIntParam(in_data, CO_FRAMES_BEFORE);
	A_long after  = ReadIntParam(in_data, CO_FRAMES_AFTER);
	if (before < 0) before = 0;
	if (after  < 0) after  = 0;

	PF_RenderRequest req = extra->input->output_request;

	PF_LRect resultR = {0, 0, 0, 0};
	PF_LRect maxR    = {0, 0, 0, 0};
	bool     first   = true;

	for (A_long off = -before; off <= after && !err; ++off) {
		A_long          checkout_id = off + before;
		A_long          when        = in_data->current_time + off * in_data->time_step;
		PF_CheckoutResult res;
		AEFX_CLR_STRUCT(res);

		ERR(extra->cb->checkout_layer(in_data->effect_ref, CO_INPUT, checkout_id,
									  &req, when, in_data->time_step,
									  in_data->time_scale, &res));
		if (err) break;

		if (first) {
			resultR = res.result_rect;
			maxR    = res.max_result_rect;
			first   = false;
		} else {
			UnionLRect(&res.result_rect,     &resultR);
			UnionLRect(&res.max_result_rect, &maxR);
		}
	}

	extra->output->result_rect     = resultR;
	extra->output->max_result_rect = maxR;
	extra->output->solid           = FALSE;
	extra->output->flags           = 0;

	return err;
}

static PF_Err
SmartRender(PF_InData *in_data, PF_OutData *out_data, PF_SmartRenderExtra *extra)
{
	PF_Err err = PF_Err_NONE, err2 = PF_Err_NONE;

	OnionParams p;
	p.before   = ReadIntParam(in_data, CO_FRAMES_BEFORE);
	p.after    = ReadIntParam(in_data, CO_FRAMES_AFTER);
	if (p.before < 0) p.before = 0;
	if (p.after  < 0) p.after  = 0;
	p.tint     = ReadIntParam(in_data, CO_TINT) / 100.0;
	p.onionOp  = ReadIntParam(in_data, CO_ONION_OPACITY) / 100.0;
	p.fade     = ReadIntParam(in_data, CO_FADE_BY_DISTANCE) != 0;
	p.edge     = ReadIntParam(in_data, CO_EDGE_DETECT) != 0;

	{	/* Edge Intensity is a float param. */
		PF_ParamDef def; AEFX_CLR_STRUCT(def);
		p.edgeGain = 2.0;
		if (!PF_CHECKOUT_PARAM(in_data, CO_EDGE_INTENSITY, in_data->current_time,
							   in_data->time_step, in_data->time_scale, &def)) {
			p.edgeGain = def.u.fs_d.value;
		}
		PF_CHECKIN_PARAM(in_data, &def);
	}

	AEFX_SuiteScoper<PF_WorldSuite2> wsP =
		AEFX_SuiteScoper<PF_WorldSuite2>(in_data, kPFWorldSuite, kPFWorldSuiteVersion2, out_data);

	/* Check out the input layer pixels first — AE requires at least one input
	   checkout before checking out the output. */
	A_long count = p.before + p.after + 1;
	std::vector<PF_EffectWorld *> worlds(count, (PF_EffectWorld *)NULL);
	for (A_long i = 0; i < count && !err; ++i) {
		ERR(extra->cb->checkout_layer_pixels(in_data->effect_ref, i, &worlds[i]));
	}

	PF_EffectWorld *output = NULL;
	ERR(extra->cb->checkout_output(in_data->effect_ref, &output));

	PF_PixelFormat format = PF_PixelFormat_ARGB32;
	if (!err && output) ERR(wsP->PF_GetPixelFormat(output, &format));

	PixelDepth depth = (format == PF_PixelFormat_ARGB128) ? PD_32
					 : (format == PF_PixelFormat_ARGB64)  ? PD_16
					 : PD_8;

	if (!err && output) {
		err = RenderOnion(in_data, out_data, wsP.get(), worlds.data(), output, depth, format, p);
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
	PF_LayerDef	*output,
	void		*extra)
{
	PF_Err err = PF_Err_NONE;

	try {
		switch (cmd) {
			case PF_Cmd_ABOUT:        err = About(in_data, out_data, params, output);       break;
			case PF_Cmd_GLOBAL_SETUP: err = GlobalSetup(in_data, out_data, params, output); break;
			case PF_Cmd_PARAMS_SETUP: err = ParamsSetup(in_data, out_data, params, output); break;
			case PF_Cmd_SMART_PRE_RENDER:
				err = PreRender(in_data, out_data, (PF_PreRenderExtra *)extra);
				break;
			case PF_Cmd_SMART_RENDER:
				err = SmartRender(in_data, out_data, (PF_SmartRenderExtra *)extra);
				break;
			default: break;
		}
	} catch (PF_Err &thrown_err) {
		err = thrown_err;
	}

	return err;
}
