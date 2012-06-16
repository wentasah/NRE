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

#pragma once

#include <kobj/RichLocalEc.h>
#include <kobj/RichPt.h>
#include <kobj/UserSm.h>
#include <CPU.h>

namespace nul {

class Service;

class ServiceInstance {
	class ServiceCapsPt : public RichPt {
	public:
		ServiceCapsPt(Service *s,RichLocalEc *ec,capsel_t pt) : RichPt(ec,pt,0), _s(s) {
		}

		virtual ErrorCode portal(UtcbFrameRef &uf,capsel_t pid);

	private:
		Service *_s;
	};

public:
	ServiceInstance(Service* s,capsel_t pt,cpu_t cpu);

	cpu_t cpu() const {
		return _ec.cpu();
	}
	LocalEc &ec() {
		return _ec;
	}

private:
	ServiceInstance(const ServiceInstance&);
	ServiceInstance& operator=(const ServiceInstance&);

	Service *_s;
	RichLocalEc _ec;
	ServiceCapsPt _pt;
	UserSm _sm;
};

}
