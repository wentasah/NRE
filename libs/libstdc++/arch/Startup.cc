/*
 * TODO comment me
 *
 * Copyright (C) 2012, Nils Asmussen <nils@os.inf.tu-dresden.de>
 * Economic rights: Technische Universitaet Dresden (Germany)
 *
 * This file is part of NUL.
 *
 * NUL is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * NUL is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License version 2 for more details.
 */

#include <arch/Startup.h>
#include <kobj/GlobalEc.h>
#include <kobj/Pd.h>
#include <kobj/Pt.h>
#include <kobj/Sm.h>
#include <utcb/UtcbFrame.h>
#include <Exception.h>
#include <CPU.h>
#include <pthread.h>

using namespace nul;

EXTERN_C void abort();

// is overwritten by the root-task; all others don't need it
WEAK void *_stack;
static void *pool[2];

static void verbose_terminate() {
	try {
		throw;
	}
	catch(const Exception& e) {
		Serial::get() << e;
	}
	catch(...) {
		Serial::get() << "Uncatched, unknown exception\n";
	}
	abort();
}

void _post_init() {
	std::set_terminate(verbose_terminate);
	_startup_info.done = true;

	// force the linker to include the Pd, GlobalEc and pthread object-files
	// TODO is there a better way?
	pool[0] = &Pd::_cur;
	pool[1] = &GlobalEc::_cur;
	// FIXME why do we have to force the linker to include the pthread object-file? the libsupc++
	// calls functions of it, so it should be included automatically, right?
	pthread_cancel(0);
}
