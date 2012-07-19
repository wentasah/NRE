/*
 * Copyright (C) 2012, Nils Asmussen <nils@os.inf.tu-dresden.de>
 * Economic rights: Technische Universitaet Dresden (Germany)
 *
 * This file is part of NRE (NOVA runtime environment).
 *
 * NRE is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * NRE is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License version 2 for more details.
 */

#pragma once

#include <arch/Types.h>
#include <arch/Defines.h>

typedef word_t spinlock_t;

static inline void lock(spinlock_t *val) {
	asm volatile (
		"mov	$1,%%" EXPAND(REG(cx)) ";"
		"1:"
		"	xor		%%" EXPAND(REG(ax)) ",%%" EXPAND(REG(ax)) ";"
		"	lock	cmpxchg %%" EXPAND(REG(cx)) ",(%0);"
		"	jz		2f;"
		/* improves the performance and lowers the power-consumption of spinlocks */
		"	pause;"
		"	jmp		1b;"
		"2:;"
		/* outputs */
		:
		/* inputs */
		: "D" (val)
		/* eax, ecx and cc will be clobbered; we need memory as well because *l is changed */
		: EXPAND(REG(ax)), EXPAND(REG(cx)), "cc", "memory"
	);
}
static inline void unlock(spinlock_t *val) {
	*val = 0;
}

#ifdef __cplusplus
namespace nre {

class SpinLock {
public:
	SpinLock() : _val() {
	}

	void down() {
		lock(&_val);
	}
	void up() {
		unlock(&_val);
	}

private:
	SpinLock(const SpinLock&);
	SpinLock& operator=(const SpinLock&);

	spinlock_t _val;
};

}
#endif
