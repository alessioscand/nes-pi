/*
 * neo-pi - entry point
 */
#include "kernel.h"
#include <circle/startup.h>

int main (void)
{
	// cannot return here because some destructors used in CKernel are not implemented

	static CKernel Kernel;	// large object: keep it off the 128 KB stack
	if (!Kernel.Initialize ())
	{
		halt ();
		return EXIT_HALT;
	}

	TShutdownMode ShutdownMode = Kernel.Run ();

	switch (ShutdownMode)
	{
	case ShutdownReboot:
		reboot ();
		return EXIT_REBOOT;

	case ShutdownHalt:
	default:
		halt ();
		return EXIT_HALT;
	}
}
