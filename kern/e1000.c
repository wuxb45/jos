#include <kern/pci.h>
#include <kern/e1000.h>

// LAB 6: Your driver code here
int
pci_e1000_attach(struct pci_func *pcif)
{
  // Exec 3
  pci_func_enable(pcif);
  return 1;
}
