#include <kern/pmap.h>
#include <kern/pci.h>
#include <kern/e1000.h>

// LAB 6: Your driver code here
struct tx_desc
{
  uint64_t addr;
  uint16_t length;
  uint8_t cso;
  uint8_t cmd;
  uint8_t status;
  uint8_t css;
  uint16_t special;
} __attribute__((packed));

#define TX_ARRAY_CAP (64)
#define TX_ARRAY_SIZE ((TX_ARRAY_CAP) * sizeof(struct tx_desc))

struct tx_desc tx_desc_array[TX_ARRAY_CAP] __attribute__((aligned(TX_ARRAY_SIZE)));

static volatile uint32_t *
access_32(volatile void * base, uint32_t offset)
{
  return (volatile uint32_t *)(base + offset);
}

static volatile uint16_t *
access_16(volatile void * base, uint32_t offset)
{
  return (volatile uint16_t *)(base + offset);
}

// exec 5
static void
pci_e1000_init(volatile void * base)
{
  // manual chapter 14.5
  (*access_32(base, E1000_TDBAL)) = (uint32_t)tx_desc_array;
  (*access_32(base, E1000_TDBAH)) = 0;
  (*access_32(base, E1000_TDLEN)) = TX_ARRAY_SIZE;
  (*access_32(base, E1000_TDH)) = 0;
  (*access_32(base, E1000_TDT)) = 0;

  (*access_32(base, E1000_TIPG)) = 10 | (8 << 10) | (6 << 20);
  (*access_32(base, E1000_TCTL)) = E1000_TCTL_EN | E1000_TCTL_PSP | 0x100 | 0x40000;
}

// Exec 3
int
pci_e1000_attach(struct pci_func *pcif)
{
  pci_func_enable(pcif);
  const uint32_t bar0_base = pcif->reg_base[0];
  const uint32_t bar0_size = pcif->reg_size[0];
  boot_map_region(kern_pgdir, E1000_BAR0_BASE, bar0_size, bar0_base, PTE_W);
  volatile void *pregs = (void *)E1000_BAR0_BASE;
  pci_e1000_init(pregs);
  return 1;
}
