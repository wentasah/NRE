/*
 * (c) 2012 Nils Asmussen <nils@os.inf.tu-dresden.de>
 *     economic rights: Technische Universität Dresden (Germany)
 *
 * This file is part of TUD:OS and distributed under the terms of the
 * GNU General Public License 2.
 * Please see the COPYING-GPL-2 file for details.
 */

#include <ex/Exception.h>
#include <kobj/Pd.h>
#include <kobj/GlobalEc.h>
#include <kobj/Sc.h>
#include <kobj/Pt.h>
#include <kobj/Sm.h>
#include <utcb/UtcbFrame.h>
#include <mem/RegionList.h>
#include <stream/Log.h>
#include <arch/Elf.h>
#include <ScopedPtr.h>

// TODO there is a case when a service is not notified that a client died (non-existent portal or something)
// TODO perhaps we can build a very small boot-task, which establishes an equal environment for all childs
// that is, it abstracts away the differences between the root-task and others, so that we can have a
// common library for all except the tiny boot-task which has its own little lib
// TODO we can't do synchronous IPC with untrusted tasks

using namespace nul;

enum {
	MAX_CLIENTS		= 32,
	MAX_SERVICES	= 32,
};

class Service {
public:
	Service(const char *name,cap_t pt) : _name(name), _pt(pt) {
	}

	const char *name() const {
		return _name;
	}
	cap_t pt() const {
		return _pt;
	}

private:
	const char *_name;
	cap_t _pt;
};

class ServiceRegistry {
public:
	ServiceRegistry() : _srvs() {
	}

	void reg(const Service& s) {
		for(size_t i = 0; i < MAX_SERVICES; ++i) {
			if(_srvs[i] == 0) {
				_srvs[i] = new Service(s);
				return;
			}
		}
		throw Exception("All service slots in use");
	}
	const Service& find(const char *name) const {
		for(size_t i = 0; i < MAX_SERVICES; ++i) {
			if(strcmp(_srvs[i]->name(),name) == 0)
				return *_srvs[i];
		}
		throw Exception("Unknown service");
	}

private:
	Service *_srvs[MAX_SERVICES];
};

class ElfException : public Exception {
public:
	ElfException(const char *msg) : Exception(msg) {
	}
};

struct Child {
	const char *cmdline;
	Pd *pd;
	GlobalEc *ec;
	Sc *sc;
	Pt *pf_pt;
	Pt *start_pt;
	Pt *init_pt;
	Pt *reg_pt;
	Pt *get_pt;
	RegionList regs;
	uintptr_t stack;
	uintptr_t utcb;
	uintptr_t hip;

	Child(const char *cmdline) : cmdline(cmdline), pd(), ec(), sc(), pf_pt(), start_pt(), init_pt(),
			reg_pt(), get_pt(), regs(), stack(), utcb(), hip() {
	}
	~Child() {
		if(pd)
			delete pd;
		if(ec)
			delete ec;
		if(sc)
			delete sc;
		if(pf_pt)
			delete pf_pt;
		if(start_pt)
			delete start_pt;
		if(init_pt)
			delete init_pt;
		if(reg_pt)
			delete reg_pt;
		if(get_pt)
			delete get_pt;
	}
};

PORTAL static void portal_initcaps(cap_t pid);
PORTAL static void portal_register(cap_t pid);
PORTAL static void portal_getservice(cap_t pid);
PORTAL static void portal_startup(cap_t pid);
PORTAL static void portal_pf(cap_t pid);
static void load_mod(uintptr_t addr,size_t size,const char *cmdline);

static size_t child = 0;
static Child *childs[MAX_CLIENTS];
static cap_t portal_caps;
static ServiceRegistry registry;
static Sm *sm;
static LocalEc *regec;

void start_childs() {
	int i = 0;
	const Hip &hip = Hip::get();
	sm = new Sm(1);
	portal_caps = CapSpace::get().allocate(MAX_CLIENTS * Hip::get().service_caps(),Hip::get().service_caps());
	// we need a dedicated Ec for the register-portal which does always accept one capability from
	// the caller
	regec = new LocalEc(0);
	UtcbFrameRef reguf(regec->utcb());
	reguf.set_receive_crd(Crd(CapSpace::get().allocate(),0,DESC_CAP_ALL));

	for(Hip::mem_const_iterator it = hip.mem_begin(); it != hip.mem_end(); ++it) {
		// we are the first one :)
		if(it->type == Hip_mem::MB_MODULE && i++ >= 1) {
			// map the memory of the module
			UtcbFrame uf;
			uf.set_receive_crd(Crd(0,31,DESC_MEM_ALL));
			uf << CapRange(it->addr >> ExecEnv::PAGE_SHIFT,it->size,DESC_MEM_ALL);
			// we assume that the cmdline does not cross pages
			if(it->aux)
				uf << CapRange(it->aux >> ExecEnv::PAGE_SHIFT,1,DESC_MEM_ALL);
			CPU::current().map_pt->call(uf);
			if(it->aux) {
				// ensure that its terminated at the end of the page
				char *end = reinterpret_cast<char*>(Util::roundup<uintptr_t>(it->aux,ExecEnv::PAGE_SIZE) - 1);
				*end = '\0';
			}

			load_mod(it->addr,it->size,reinterpret_cast<char*>(it->aux));
		}
	}
}

static Child *get_child(cap_t pid) {
	return childs[((pid - portal_caps) / Hip::get().service_caps())];
}

static void destroy_child(cap_t pid) {
	size_t i = (pid - portal_caps) / Hip::get().service_caps();
	delete childs[i];
	childs[i] = 0;
}

static void portal_initcaps(cap_t pid) {
	UtcbFrameRef uf;
	Child *c = get_child(pid);
	if(!c)
		return;

	uf.delegate(c->pd->cap(),0);
	uf.delegate(c->ec->cap(),1);
	uf.delegate(c->sc->cap(),2);
}

static void portal_register(cap_t pid) {
	UtcbFrameRef uf;
	Child *c = get_child(pid);
	if(!c)
		return;

	try {
		TypedItem cap;
		uf.get_typed(cap);
		String name;
		uf >> name;
		registry.reg(Service(name.str(),cap.crd().cap()));

		uf.clear();
		uf.set_receive_crd(Crd(CapSpace::get().allocate(),0,DESC_CAP_ALL));
		uf << 1;
	}
	catch(Exception& e) {
		uf.clear();
		uf << 0;
	}
}

static void portal_getservice(cap_t pid) {
	UtcbFrameRef uf;
	Child *c = get_child(pid);
	if(!c)
		return;

	try {
		String name;
		uf >> name;
		const Service& s = registry.find(name.str());

		uf.clear();
		uf.delegate(s.pt());
		uf << 1;
	}
	catch(Exception& e) {
		uf.clear();
		uf << 0;
	}
}

static void portal_startup(cap_t pid) {
	UtcbExcFrameRef uf;
	Child *c = get_child(pid);
	if(!c)
		return;

	uf->mtd = MTD_RIP_LEN | MTD_RSP | MTD_GPR_ACDB;
	uf->eip = *reinterpret_cast<uint32_t*>(uf->esp);
	uf->esp = c->stack + (uf->esp & (ExecEnv::PAGE_SIZE - 1));
	uf->eax = c->ec->cpu();
	uf->ecx = c->hip;
	uf->edx = c->utcb;
	uf->ebx = 1;
}

static void portal_pf(cap_t pid) {
	UtcbExcFrameRef uf;
	Child *c = get_child(pid);
	if(!c)
		return;

	uintptr_t pfaddr = uf->qual[1];
	uintptr_t eip = uf->eip;

	sm->down();
	Serial::get().writef("Child '%s': Pagefault for %p @ %p, error=%#x\n",c->cmdline,pfaddr,eip,uf->qual[0]);

	pfaddr &= ~(ExecEnv::PAGE_SIZE - 1);
	uintptr_t src;
	uint flags = c->regs.find(pfaddr,src);
	// not found or already mapped?
	if(!flags || (flags & RegionList::M)) {
		uintptr_t *addr,addrs[32];
		Serial::get().writef("Unable to resolve fault; killing child\n");
		ExecEnv::collect_backtrace(c->ec->stack(),uf->ebp,addrs,sizeof(addrs));
		Serial::get().writef("Backtrace:\n");
		addr = addrs;
		while(*addr != 0) {
			Serial::get().writef("\t%p\n",*addr);
			addr++;
		}
		destroy_child(pid);
	}
	else {
		uint perms = flags & RegionList::RWX;
		uf.delegate(CapRange(src >> ExecEnv::PAGE_SHIFT,1,
				DESC_TYPE_MEM | (perms << 2),pfaddr >> ExecEnv::PAGE_SHIFT));
		c->regs.map(pfaddr);
	}
	sm->up();
}

static void load_mod(uintptr_t addr,size_t size,const char *cmdline) {
	ElfEh *elf = reinterpret_cast<ElfEh*>(addr);

	// check ELF
	if(size < sizeof(ElfEh) || sizeof(ElfPh) > elf->e_phentsize || size < elf->e_phoff + elf->e_phentsize * elf->e_phnum)
		throw ElfException("Invalid ELF");
	if(!(elf->e_ident[0] == 0x7f && elf->e_ident[1] == 'E' && elf->e_ident[2] == 'L' && elf->e_ident[3] == 'F'))
		throw ElfException("Invalid signature");

	// create child
	Child *c = new Child(cmdline);
	try {
		// we have to create the portals first to be able to delegate them to the new Pd
		cap_t portals = portal_caps + child * Hip::get().service_caps();
		c->pf_pt = new Pt(CPU::current().ec,portals + CapSpace::EV_PAGEFAULT,portal_pf,MTD_GPR_BSD | MTD_QUAL | MTD_RIP_LEN);
		c->start_pt = new Pt(CPU::current().ec,portals + CapSpace::EV_STARTUP,portal_startup,MTD_RSP);
		c->init_pt = new Pt(CPU::current().ec,portals + CapSpace::SRV_INIT,portal_initcaps,0);
		c->reg_pt = new Pt(regec,portals + CapSpace::SRV_REGISTER,portal_register,0);
		c->get_pt = new Pt(CPU::current().ec,portals + CapSpace::SRV_GET,portal_getservice,0);
		// now create Pd and pass portals
		c->pd = new Pd(Crd(portals,Util::bsr(Hip::get().service_caps()),DESC_CAP_ALL));
		// TODO wrong place
		c->utcb = 0x7FFFF000;
		c->ec = new GlobalEc(reinterpret_cast<GlobalEc::startup_func>(elf->e_entry),0,0,c->pd,c->utcb);

		// check load segments and add them to regions
		for(size_t i = 0; i < elf->e_phnum; i++) {
			ElfPh *ph = reinterpret_cast<ElfPh*>(addr + elf->e_phoff + i * elf->e_phentsize);
			if(ph->p_type != 1)
				continue;
			if(size < ph->p_offset + ph->p_filesz)
				throw ElfException("Load segment invalid");

			uint perms;
			if(ph->p_flags & PF_R)
				perms |= RegionList::R;
			if(ph->p_flags & PF_W)
				perms |= RegionList::W;
			if(ph->p_flags & PF_X)
				perms |= RegionList::X;
			// TODO actually it would be better to do that later
			if(ph->p_filesz < ph->p_memsz)
				memset(reinterpret_cast<void*>(addr + ph->p_offset + ph->p_filesz),0,ph->p_memsz - ph->p_filesz);
			c->regs.add(ph->p_vaddr,ph->p_memsz,addr + ph->p_offset,perms);
		}

		// he needs a stack and utcb
		c->stack = c->regs.find_free(ExecEnv::STACK_SIZE);
		c->regs.add(c->stack,ExecEnv::STACK_SIZE,c->ec->stack(),RegionList::RW);
		// TODO give the child his own Hip
		c->hip = reinterpret_cast<uintptr_t>(&Hip::get());
		c->regs.add(c->hip,ExecEnv::PAGE_SIZE,c->hip,RegionList::R);

		sm->down();
		Serial::get().writef("Starting client '%s'...\n",c->cmdline);
		c->regs.write(Serial::get());
		Serial::get().writef("\n");
		sm->up();

		// start child; we have to put the child into the list before that
		childs[child++] = c;
		c->sc = new Sc(c->ec,Qpd(),c->pd);
	}
	catch(...) {
		delete c;
		childs[--child] = 0;
	}
}
