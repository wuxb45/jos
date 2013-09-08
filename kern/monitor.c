// Simple command-line kernel monitor useful for
// controlling the kernel and exploring the system interactively.

#include <inc/stdio.h>
#include <inc/string.h>
#include <inc/memlayout.h>
#include <inc/assert.h>
#include <inc/x86.h>

#include <kern/env.h>
#include <kern/cpu.h>
#include <kern/console.h>
#include <kern/monitor.h>
#include <kern/kdebug.h>
#include <kern/trap.h>
#include <kern/pmap.h>
#include <kern/spinlock.h>

#define CMDBUF_SIZE	80      // enough for one VGA text line
static struct spinlock monitor_lock = {
#ifdef DEBUG_SPINLOCK
	.name = "monitor_lock",
#endif
};

struct Command {
  const char *name;
  const char *desc;
  // return -1 to force monitor to exit
  int (*func) (int argc, char **argv, struct Trapframe * tf);
};

static struct Command commands[] = {
  {"help", "Display this list of commands", mon_help},
  {"kerninfo", "Display information about the kernel", mon_kerninfo},
  {"backtrace", "Backtrace Current Call-Stack", mon_backtrace},
  {"envbacktrace", "Backtrace Env", mon_envbacktrace},
  {"showmap", "Show Paging Translation Mappings", mon_showmappings},
  {"permset", "Set/Unset page permissions", mon_permset},
  {"dumppa", "Dump data in Physical Address", mon_dumppa},
  {"dumpva", "Dump data in Virtual Address", mon_dumpva},
  {"resume", "Resume from Break Point", mon_resume},
  {"step", "Stepping one instruction", mon_step},
  {"cpuid", "Get CPUID of monitor", mon_cpuid},
  {"uscan", "Scanning Page Mapping of User Env", mon_uscan},
  {"kscan", "Scanning Page Mapping of Kernel", mon_kscan},
  {"envs", "Show Running Envs", mon_envs},
};

#define NCOMMANDS (sizeof(commands)/sizeof(commands[0]))

unsigned read_eip();

/***** Implementations of basic kernel monitor commands *****/

int
mon_help(int argc, char **argv, struct Trapframe *tf)
{
  int i;

  for (i = 0; i < NCOMMANDS; i++)
    cprintf("%s - %s\n", commands[i].name, commands[i].desc);
  return 0;
}

int
mon_kerninfo(int argc, char **argv, struct Trapframe *tf)
{
  extern char entry[], etext[], edata[], end[];

  cprintf("Special kernel symbols:\n");
  cprintf("  entry  %08x (virt)  %08x (phys)\n", entry, entry - KERNBASE);
  cprintf("  etext  %08x (virt)  %08x (phys)\n", etext, etext - KERNBASE);
  cprintf("  edata  %08x (virt)  %08x (phys)\n", edata, edata - KERNBASE);
  cprintf("  end    %08x (virt)  %08x (phys)\n", end, end - KERNBASE);
  cprintf("Kernel executable memory footprint: %dKB\n",
          (end - entry + 1023) / 1024);
  return 0;
}

int
mon_backtrace(int argc, char **argv, struct Trapframe *tf)
{
  // Your code here.
  uint32_t ebp, eip;
  uint32_t arg;
  int i, nargs;
  struct Eipdebuginfo info = { };
  cprintf("Stack backtrace:\n");

  //current func
  eip = read_eip();
  ebp = read_ebp();
  while (ebp) {
    // get debuginfo
    const int r = debuginfo_eip((uintptr_t) eip, &info);
    if (r != 0) return 0;
    nargs = info.eip_fn_narg;
    // print stack info
    cprintf("  ebp %08x  eip %08x  args[%d] ", ebp, eip, nargs);
    for (i = 0; i < nargs; i++) {
      arg = *(((uint32_t *) ebp) + 2 + i);
      cprintf(" %08x", arg);
    }
    // print symbol info
    cprintf("\n        %s:%d:   %.*s+%d\n", info.eip_file, info.eip_line,
            info.eip_fn_namelen, info.eip_fn_name, (eip - info.eip_fn_addr));
    // trace back: next eip,ebp
    eip = *(((uint32_t *) ebp) + 1);
    ebp = *((uint32_t *) ebp);
  }
  return 0;
}

int
mon_envbacktrace(int argc, char **argv, struct Trapframe *tf)
{
  const envid_t envid = (argc < 2)?0:strtol(argv[1], NULL, 16);
  struct Env *e = NULL;
  const int r = envid2env(envid, &e, 0);
  if (r < 0) {
    cprintf("no such Env\n");
    return 0;
  }
  cprintf("backtrace Env: %08x\n", e->env_id);
  
  lcr3(PADDR(e->env_pgdir));
  uint32_t ebp, eip;
  uint32_t arg;
  int i, nargs;
  struct Eipdebuginfo info = { };
  cprintf("Stack backtrace:\n");

  //current func
  eip = e->env_tf.tf_eip;
  ebp = e->env_tf.tf_regs.reg_ebp;
  while (ebp) {
    // get debuginfo
    const int r = debuginfo_eip((uintptr_t) eip, &info);
    if (r != 0) return 0;
    nargs = info.eip_fn_narg;
    // print stack info
    cprintf("  ebp %08x  eip %08x  args[%d] ", ebp, eip, nargs);
    for (i = 0; i < nargs; i++) {
      arg = *(((uint32_t *) ebp) + 2 + i);
      cprintf(" %08x", arg);
    }
    // print symbol info
    cprintf("\n        %s:%d:   %.*s+%d\n", info.eip_file, info.eip_line,
            info.eip_fn_namelen, info.eip_fn_name, (eip - info.eip_fn_addr));
    // trace back: next eip,ebp
    eip = *(((uint32_t *) ebp) + 1);
    ebp = *((uint32_t *) ebp);
  }
  lcr3(PADDR(kern_pgdir));
  return 0;
}

int
mon_showmappings(int argc, char ** argv, struct Trapframe *tf)
{
  cprintf("Show Mappings:\n");
  if ((argc < 2)
  || ((argv[1][0] != '0') && (argv[1][1] != 'x'))
  || ((argc >= 3) && (argv[2][0] != '0') && (argv[2][1] != 'x'))) {
    cprintf("  Usage:\n    # showmap 0x<HEXADDR> 0x<HEXADDR>\n");
    cprintf("  or:\n    # showmap 0x<HEXADDR>\n");
    return 0;
  }
  uintptr_t start, end, paddr, vaddr;
  start = PTE_ADDR(strtol(&argv[1][2], NULL, 16));
  if (argc == 2) {
    end = start + PGSIZE;
  } else {
    end = PTE_ADDR(strtol(&argv[2][2], NULL, 16));
  }
  cprintf("Mappings in [0x%08x, 0x%08x):\n", start, end);
  //pde_t * curr_pgdir = (pde_t *)rcr3();
  //cprintf("cr3: %08x, kern_pgdir: %08x\n", (uintptr_t)curr_pgdir, kern_pgdir);
  for (vaddr = start; vaddr < end; vaddr += PGSIZE) {
    // TODO: use pgdir_walk()
    //paddr = check_va2pa(kern_pgdir, vaddr);
    pte_t * ppte = pgdir_walk(kern_pgdir, (void *)vaddr, 0);
    if (ppte == NULL || ((*ppte) & PTE_P) == 0) {
      cprintf("  [%08x] -> [--------]\n", vaddr);
    } else {
      paddr = PTE_ADDR(*ppte);
      cprintf("  [%08x] -> [%08x]\n", vaddr, paddr);
    }
  }
  return 0;
}

int
mon_permset(int argc, char ** argv, struct Trapframe *tf)
{
  uint32_t newperm;
  if (argc < 3) {
    cprintf("  Usage:\n  # permset 0x<HEXADDR> {+W | -W | +U | -U}\n");
    return 0;
  }

  uintptr_t va;
  va = PTE_ADDR(strtol(&argv[1][2], NULL, 16));
  pte_t * ppte = pgdir_walk(kern_pgdir, (void *)va, 0);
  if (ppte == NULL) {
    cprintf("VA Not mapped\n");
    return 0;
  }
  cprintf("Original pte: 0x%08x\n", *ppte);
  if (strcmp(argv[2], "+W") == 0) {
    *ppte |= PTE_W;
  } else if (strcmp(argv[2], "-W") == 0) {
    *ppte &= (~PTE_W);
  } else if (strcmp(argv[2], "+U") == 0) {
    *ppte |= PTE_U;
  } else if (strcmp(argv[2], "-U") == 0) {
    *ppte &= (~PTE_U);
  } else {
    cprintf("Unknown option: %s\n", argv[2]);
    return 0;
  }
  cprintf("Updated pte : 0x%08x\n", *ppte);
  tlb_invalidate(kern_pgdir, (void *)va);
  return 0;
}

int
dump_helper(uintptr_t page_addr, int pa)
{
  uint32_t a;
  uint32_t ix;
  uint32_t *ptr;
  if (pa) {
    ptr = (uint32_t *)(page_addr + KERNBASE);
  } else {
    ptr = (uint32_t *)(page_addr);
  }

  // check for mapped
  pte_t * ppte = pgdir_walk(kern_pgdir, ptr, 0);
  if (ppte == NULL || ((*ppte) & PTE_P) == 0) {
    cprintf("Page not Mapped: [0x%08x]\n", page_addr);
    return 0;
  }

  for (ix = 0; ix < (PGSIZE / sizeof(uint32_t)); ix++) {
    if ((ix % 16) == 0) {
      a = (uint32_t)(ptr+ix);
      cprintf("\n[%08x]: ", a);
    } else {
      cprintf(" ");
    }
    cprintf("[%08x]", ptr[ix]);
  }
  cprintf("\n");
  return 0;
}

int
mon_dumpva(int argc, char ** argv, struct Trapframe *tf)
{
  uint32_t n, i;
  uint32_t ix, va;

  if (argc < 2) {
    cprintf("  Usage:\n  # dumpva 0x<HEXADDR> [nr_pages]\n");
    return 0;
  }

  if (argc >= 3) {
    n = strtol(argv[2], NULL, 16);
  } else {
    n = 1;
  }

  va = PTE_ADDR(strtol(&argv[1][2], NULL, 16));
  for (i = 0; i < n; i++) {
    dump_helper(va, 0);
    va += PGSIZE;
  }
  return 0;
}

int
mon_dumppa(int argc, char ** argv, struct Trapframe *tf)
{
  uint32_t n, i;
  uint32_t ix, pa;

  if (argc < 2) {
    cprintf("  Usage:\n  # dumpva 0x<HEXADDR> [nr_pages]\n");
    return 0;
  }

  if (argc >= 3) {
    n = strtol(argv[2], NULL, 16);
  } else {
    n = 1;
  }

  pa = PTE_ADDR(strtol(&argv[1][2], NULL, 16));
  for (i = 0; i < n; i++) {
    dump_helper(pa, 1);
    pa += PGSIZE;
  }
  return 0;
}

int
mon_resume(int argc, char ** argv, struct Trapframe *tf)
{
  if (curenv) {
    return -1;
  } else {
    cprintf("resume: no curenv!\n");
    return 0;
  }
}

int
mon_step(int argc, char ** argv, struct Trapframe *tf)
{
  if (curenv) {
    curenv->env_tf.tf_eflags |= 0x100;
    return -1;
  } else {
    cprintf("step: no curenv!\n");
    return 0;
  }
}

int
mon_cpuid(int argc, char ** argv, struct Trapframe *tf)
{
  cprintf("CPUID: %d\n", cpunum());
  return 0;
}

int
mon_uscan(int argc, char ** argv, struct Trapframe *tf)
{
  if (argc < 2) {
    if (curenv != 0) {
      pde_t *pgdir = curenv->env_pgdir;
      paging_smart_scan(pgdir);
    } else {
      cprintf("No curenv\n");
    }
  } else {
    const uint32_t id = strtol(argv[1], NULL, 16);
    struct Env * env;
    const int re = envid2env(id, &env, 0);
    if (re != 0) {
      cprintf("Bad envid\n");
      return 0;
    }
    paging_smart_scan(env->env_pgdir);
  }
  return 0;
}

int
mon_kscan(int argc, char ** argv, struct Trapframe *tf)
{
  paging_smart_scan(kern_pgdir);
  return 0;
}

int
mon_envs(int argc, char ** argv, struct Trapframe *tf)
{
  int i;
  for (i = 0; i < NENV; i++) {
    if (envs[i].env_status != ENV_FREE) {
      char * status;
      switch (envs[i].env_status) {
        case ENV_RUNNING: status = "RUNNING"; break;
        case ENV_RUNNABLE: status = "RUNNABLE"; break;
        case ENV_DYING: status = "DYING"; break;
        case ENV_NOT_RUNNABLE: status = "NOT_RUNNABLE"; break;
        default: status = "???"; break;
      }
      char * type;
      switch (envs[i].env_type) {
        case ENV_TYPE_USER: type = "USER"; break;
        case ENV_TYPE_IDLE: type = "IDLE"; break;
        case ENV_TYPE_FS: type = "FS"; break;
        default: type = "???"; break;
      }
      cprintf("ENV[%08x] %s %s\n", envs[i].env_id, type, status);
    }
  }
  return 0;
}

/***** Kernel monitor command interpreter *****/

#define WHITESPACE "\t\r\n "
#define MAXARGS 16

static int
runcmd(char *buf, struct Trapframe *tf)
{
  int argc;
  char *argv[MAXARGS];
  int i;

  // Parse the command buffer into whitespace-separated arguments
  argc = 0;
  argv[argc] = 0;
  while (1) {
    // gobble whitespace
    while (*buf && strchr(WHITESPACE, *buf))
      *buf++ = 0;
    if (*buf == 0)
      break;

    // save and scan past next arg
    if (argc == MAXARGS - 1) {
      cprintf("Too many arguments (max %d)\n", MAXARGS);
      return 0;
    }
    argv[argc++] = buf;
    while (*buf && !strchr(WHITESPACE, *buf))
      buf++;
  }
  argv[argc] = 0;

  // Lookup and invoke the command
  if (argc == 0)
    return 0;
  for (i = 0; i < NCOMMANDS; i++) {
    if (strcmp(argv[0], commands[i].name) == 0)
      return commands[i].func(argc, argv, tf);
  }
  cprintf("Unknown command '%s'\n", argv[0]);
  return 0;
}

void
monitor(struct Trapframe *tf)
{
  char *buf;
  spin_lock(&monitor_lock);

  cprintf("Welcome to the JOS kernel monitor!\n");
  cprintf("Type 'help' for a list of commands.\n");
  cprintf("CPUID: %d\n", cpunum());

	if (tf != NULL)
		print_trapframe(tf);

  while (1) {
    buf = readline("K> ");
    if (buf != NULL)
      if (runcmd(buf, tf) < 0)
        break;
  }
  spin_unlock(&monitor_lock);
}

// return EIP of caller.
// does not work if inlined.
// putting at the end of the file seems to prevent inlining.
unsigned
read_eip()
{
  uint32_t callerpc;
  __asm __volatile("movl 4(%%ebp), %0":"=r"(callerpc));
  return callerpc;
}
