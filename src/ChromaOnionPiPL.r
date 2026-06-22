#include "AEConfig.h"
#include "AE_EffectVers.h"

#ifndef AE_OS_WIN
	#include "AE_General.r"
#endif

resource 'PiPL' (16000) {
	{	/* array properties */
		Kind {
			AEEffect
		},
		Name {
			"ChromaOnion"
		},
		Category {
			"Utility"
		},

#ifdef AE_OS_WIN
	#if defined(AE_PROC_INTELx64)
		CodeWin64X86 {"EffectMain"},
	#elif defined(AE_PROC_ARM64)
		CodeWinARM64 {"EffectMain"},
	#endif
#elif defined(AE_OS_MAC)
		CodeMacIntel64 {"EffectMain"},
		CodeMacARM64 {"EffectMain"},
#endif

		AE_PiPL_Version {
			2,
			0
		},
		AE_Effect_Spec_Version {
			PF_PLUG_IN_VERSION,
			PF_PLUG_IN_SUBVERS
		},
		AE_Effect_Version {
			/* MUST equal PF_VERSION(MAJOR,MINOR,BUG,STAGE,BUILD) in ChromaOnion.h.
			   (subvers 1)<<15 | (build 4) = 32772  -> v0.1, develop build 4 */
			32772
		},
		AE_Effect_Info_Flags {
			0
		},
		AE_Effect_Global_OutFlags {
			33554434	/* WIDE_TIME_INPUT | DEEP_COLOR_AWARE */
		},
		AE_Effect_Global_OutFlags_2 {
			0x8000000	/* SUPPORTS_THREADED_RENDERING */
		},
		AE_Effect_Match_Name {
			"BAKU ChromaOnion"
		},
		AE_Reserved_Info {
			0
		},
		AE_Effect_Support_URL {
			"https://github.com/baku89/ChromaOnion"
		}
	}
};
