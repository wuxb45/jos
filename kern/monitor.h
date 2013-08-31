#ifndef JOS_KERN_MONITOR_H
#define JOS_KERN_MONITOR_H
#ifndef JOS_KERNEL
#error "This is a JOS kernel header; user programs should not #include it"
#endif

struct Trapframe;

// Activate the kernel monitor,
// optionally providing a trap frame indicating the current state
// (NULL if none).
void monitor(struct Trapframe *tf);

// Functions implementing monitor commands.
#define DECLARE_MON(name) \
  int mon_##name (int, char **, struct Trapframe *)

DECLARE_MON(help);
DECLARE_MON(kerninfo);
DECLARE_MON(backtrace);
DECLARE_MON(showmappings);
DECLARE_MON(permset);
DECLARE_MON(dumpva);
DECLARE_MON(dumppa);
DECLARE_MON(resume);
DECLARE_MON(step);
DECLARE_MON(cpuid);
DECLARE_MON(pgscan);

#endif                          // !JOS_KERN_MONITOR_H
