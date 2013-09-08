// implement fork from user space

#include <inc/string.h>
#include <inc/lib.h>
#include <inc/x86.h>

// PTE_COW marks copy-on-write page table entries.
// It is one of the bits explicitly allocated to user processes (PTE_AVAIL).
#define PTE_COW		0x800
static void
print_regs(struct PushRegs *regs)
{
	cprintf("  edi  0x%08x\n", regs->reg_edi);
	cprintf("  esi  0x%08x\n", regs->reg_esi);
	cprintf("  ebp  0x%08x\n", regs->reg_ebp);
	cprintf("  oesp 0x%08x\n", regs->reg_oesp);
	cprintf("  ebx  0x%08x\n", regs->reg_ebx);
	cprintf("  edx  0x%08x\n", regs->reg_edx);
	cprintf("  ecx  0x%08x\n", regs->reg_ecx);
	cprintf("  eax  0x%08x\n", regs->reg_eax);
}

static void
print_utrapframe(struct UTrapframe *utf)
{
	cprintf("UTRAP frame at %p\n", utf);
  cprintf("   va  0x%08x\n", utf->utf_fault_va);
  cprintf("  err  0x%08x\n", utf->utf_err);
	print_regs(&utf->utf_regs);
  cprintf("  eip  0x%08x\n", utf->utf_eip);
  cprintf("  flag 0x%08x\n", utf->utf_eflags);
  cprintf("  esp  0x%08x\n", utf->utf_esp);
}


static inline pte_t
get_ro_pte(void *va)
{
  const pde_t *ppde = (pde_t *)vpd;
  const uint32_t pdx = PDX(va);
  const uint32_t pn = (pdx * NPTENTRIES) + PTX(va);
  if ((ppde[pdx] & PTE_P) == 0) return 0;
  return ((pte_t *)vpt)[pn];
}

//
// Custom page fault handler - if faulting page is copy-on-write,
// map in our own private writable copy.
//
static void
pgfault(struct UTrapframe *utf)
{
	void *addr = (void *) utf->utf_fault_va;
	uint32_t err = utf->utf_err;
	int r;

	// Check that the faulting access was (1) a write, and (2) to a
	// copy-on-write page.  If not, panic.
	// Hint:
	//   Use the read-only page table mappings at vpt
	//   (see <inc/memlayout.h>).

	// LAB 4: Your code here.
  if ((uintptr_t)addr >= UTOP) {
    if (FEC_WR & err) {
      panic("write above UTOP");
    }
    return;
  }
  const pte_t pte = get_ro_pte(addr);
  if (err != (FEC_WR | FEC_U | FEC_PR)
      || (pte & PTE_COW) == 0
      || (pte & PTE_P) == 0) {
    panic("pgfault(): I cannot handle this");
  }

	// Allocate a new page, map it at a temporary location (PFTEMP),
	// copy the data from the old page to the new page, then move the new
	// page to the old page's address.
	// Hint:
	//   You should make three system calls.
	//   No need to explicitly delete the old page's mapping.

	// LAB 4: Your code here.
  void * pg = (void *)PTE_ADDR(addr);
  // at this time, thisenv may not correct
  const envid_t envid = sys_getenvid();
  const int perm = (pte & PTE_SYSCALL) | PTE_W;
  const int ra = sys_page_alloc(envid, PFTEMP, perm);
  if (ra != 0) { panic("cannot alloc PFTEMP"); }
  (void)memmove(PFTEMP, pg, PGSIZE);

  const int rm = sys_page_map(envid, PFTEMP, envid, pg, perm);
  if (rm != 0) { panic("cannot map"); }
  const int ru = sys_page_unmap(envid, PFTEMP);
  if (ru != 0) { panic("cannot unmap"); }
  return;
}

//
// Map our virtual page pn (address pn*PGSIZE) into the target envid
// at the same virtual address.  If the page is writable or copy-on-write,
// the new mapping must be created copy-on-write, and then our mapping must be
// marked copy-on-write as well.  (Exercise: Why do we need to mark ours
// copy-on-write again if it was already copy-on-write at the beginning of
// this function?)
//
// Returns: 0 on success, < 0 on error.
// It is also OK to panic on error.
//
static int
duppage(envid_t envid, unsigned pn)
{
	// LAB 4: Your code here.
  void *va = (void *)(pn * PGSIZE);
  pte_t pte = get_ro_pte(va);
  if ((pte & PTE_P) == 0) {
    return 0;
  }

  uint32_t perm = pte & PTE_SYSCALL;
  if (perm & (PTE_W | PTE_COW)) {
    perm &= (~PTE_W);
    perm |= PTE_COW;
  }

  // remap child
  const int rmc = sys_page_map(thisenv->env_id, va, envid, va, perm);
  if (rmc != 0) { panic("fork(): cannot map page at %08x for child", va); }

  // remap myself
  const int rmp = sys_page_map(thisenv->env_id, va, thisenv->env_id, va, perm);
  if (rmp != 0) { panic("fork(): cannot map page at %08x for parent", va); }
	return 0;
}

static int
duppage_share(envid_t envid, unsigned pn)
{
  void *va = (void *)(pn * PGSIZE);
  pte_t pte = get_ro_pte(va);
  if ((pte & PTE_P) == 0) {
    return 0;
  }

  const uint32_t perm = pte & PTE_SYSCALL;
  const int rm = sys_page_map(thisenv->env_id, va, envid, va, perm);
  if (rm != 0) { panic("fork(): cannot map page at %08x for child", va); }
  return 0;
}

//
// User-level fork with copy-on-write.
// Set up our page fault handler appropriately.
// Create a child.
// Copy our address space and page fault handler setup to the child.
// Then mark the child as runnable and return.
//
// Returns: child's envid to the parent, 0 to the child, < 0 on error.
// It is also OK to panic on error.
//
// Hint:
//   Use vpd, vpt, and duppage.
//   Remember to fix "thisenv" in the child process.
//   Neither user exception stack should ever be marked copy-on-write,
//   so you must allocate a new page for the child's user exception stack.
//
envid_t
fork(void)
{
	// LAB 4: Your code here.
  // setup handler
  const envid_t pid = sys_getenvid();
  set_pgfault_handler(pgfault);

  // fork
  const envid_t cid = sys_exofork();
  if (cid == 0) { // child
    const envid_t id = sys_getenvid();
    return 0;
  } else if (cid < 0) {
    panic("fork(): error on sys_exofork()");
  }

  // parent
  // Copy page fault handler to child
  const int rp = sys_page_alloc(cid, (void *)(UXSTACKTOP - PGSIZE), PTE_W | PTE_U);
  if (rp != 0) { panic("ERROR on alloc UX for child"); }
  extern void _pgfault_upcall(void);
  const int ru = sys_env_set_pgfault_upcall(cid, _pgfault_upcall);
  if (ru != 0) { panic("ERROR on set _pgfault_upcall for child"); }

  // dup mappings
  const pde_t *ppde = (pde_t *)vpd;
  uint32_t i,j;
  for (i = 0; i < NPDENTRIES; i++) {
    if ((i * PTSIZE) >= UTOP) break;
    if ((ppde[i] & PTE_P) == 0) continue;
    for (j = 0; j < NPTENTRIES; j++) {
      if ((i * PTSIZE + j * PGSIZE) >= (UXSTACKTOP - PGSIZE)) break;
      const unsigned pn = (i * NPTENTRIES + j);
      duppage(cid, pn);
    }
  }
  const int rr = sys_env_set_status(cid, ENV_RUNNABLE);
  if (rr != 0) { panic("cannot set child as RUNNABLE"); }
  return cid;
}

// Challenge!
int
sfork(void)
{
  // setup handler
  const envid_t pid = sys_getenvid();
  set_pgfault_handler(pgfault);

  // fork
  const envid_t cid = sys_exofork();
  if (cid == 0) { // child
    const envid_t id = sys_getenvid();
    return 0;
  } else if (cid < 0) {
    panic("fork(): error on sys_exofork()");
  }

  // parent
  // Copy page fault handler to child
  const int rp = sys_page_alloc(cid, (void *)(UXSTACKTOP - PGSIZE), PTE_W | PTE_U);
  if (rp != 0) { panic("ERROR on alloc UX for child"); }
  extern void _pgfault_upcall(void);
  const int ru = sys_env_set_pgfault_upcall(cid, _pgfault_upcall);
  if (ru != 0) { panic("ERROR on set _pgfault_upcall for child"); }

  // dup shared mappings
  const pde_t *ppde = (pde_t *)vpd;
  uint32_t i,j;
  for (i = 0; i < NPDENTRIES; i++) {
    if ((i * PTSIZE) >= UTOP) break;
    if ((ppde[i] & PTE_P) == 0) continue;
    for (j = 0; j < NPTENTRIES; j++) {
      if ((i * PTSIZE + j * PGSIZE) >= (USTACKTOP - PGSIZE)) break;
      const unsigned pn = (i * NPTENTRIES + j);
      duppage_share(cid, pn);
    }
  }
  // dup COW stack
  duppage(cid, (USTACKTOP - PGSIZE) >> PGSHIFT);
  const int rr = sys_env_set_status(cid, ENV_RUNNABLE);
  if (rr != 0) { panic("cannot set child as RUNNABLE"); }
  return cid;
}
