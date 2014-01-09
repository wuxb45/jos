#ifndef JOS_KERN_E1000_H
#define JOS_KERN_E1000_H

#include <kern/e1000_hw.h>
#define E1000_VENDOR_ID (0x8086)
#define E1000_BAR0_BASE (KSTACKTOP + PGSIZE)

int pci_e1000_attach(struct pci_func *pcif);
#endif	// JOS_KERN_E1000_H
