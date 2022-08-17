#pragma once

#include "dolphin/context.h"
#include "dolphin/os.h"
#include "util/preprocessor.h"
#include <ogc/machine/asm.h>

// Back up the FPU context and enable the FPU from an interrupt
#define INTERRUPT_FPU_ENABLE() interrupt_fpu_save CONCAT(_interrupt_fpu_save_, __COUNTER__)

class interrupt_fpu_save {
	OSContext context;

public:
	interrupt_fpu_save()
	{
		PPCMtmsr(PPCMfmsr() | MSR_FP);
		OSSaveFPUContext(&context);
	}

	~interrupt_fpu_save()
	{
		OSLoadFPUContext(&context);
		PPCMtmsr(PPCMfmsr() & ~MSR_FP);
	}
};