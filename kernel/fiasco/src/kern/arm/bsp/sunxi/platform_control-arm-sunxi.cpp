INTERFACE [arm && mp && pf_sunxi]:

#include "types.h"

//------------------------------------------------A83T components
INTERFACE [arm && mp && pf_sunxi && pf_sunxi_bpim3]:

#include "io.h"
#include "mem_layout.h"
#include "mmio_register_block.h"
#include "cci-400.h"

EXTENSION class Platform_control {
public:
  class Pmu : public Mmio_register_block
  {
  };
};

//------------------------------------------------Generic control
IMPLEMENTATION [arm && mp && pf_sunxi && !(pf_sunxi_bpim3)]:

#include "ipi.h"
#include "mem.h"
#include "mmio_register_block.h"
#include "kmem.h"

PUBLIC static
void
Platform_control::boot_ap_cpus(Address phys_tramp_mp_addr)
{
  enum {
    CPUx_base      = 0x40,
    CPUx_offset    = 0x40,
    CPUx_RST_CTRL  = 0,
    GENER_CTRL_REG = 0x184,
    PRIVATE_REG    = 0x1a4,
  };
  Mmio_register_block c(Kmem::mmio_remap(0x01c25c00));
  c.write<Mword>(phys_tramp_mp_addr, 0x1a4);

  unsigned cpu = 1;
  c.write<Mword>(0, CPUx_base + CPUx_offset * cpu + CPUx_RST_CTRL);
  c.clear<Mword>(1 << cpu, GENER_CTRL_REG);
  c.write<Mword>(3, CPUx_base + CPUx_offset * cpu + CPUx_RST_CTRL);

  Ipi::bcast(Ipi::Global_request, Cpu_number::boot_cpu());
}

//------------------------------------------------A83T control
IMPLEMENTATION [arm && mp && pf_sunxi && pf_sunxi_bpim3]:

#include "ipi.h"
#include "mem.h"
#include "mmio_register_block.h"
#include "kmem.h"

PUBLIC static
void
Platform_control::boot_ap_cpus(Address phys_tramp_mp_addr)
{
  // System Control registers
  enum {
    CPUx_base       = 0x40,
    CPUx_offset     = 0x40,
    C0CTRL_REG0     = 0x00,
    C0CTRL_REG1     = 0x04,
    C1CTRL_REG0     = 0x10,
    C1CTRL_REG1     = 0x14,
    GENER_CTRL_REG0 = 0x28,
    GENER_CTRL_REG1 = 0x2c,
    C0_CPU_STATUS   = 0x30,
    C1_CPU_STATUS   = 0x34,
    IRQ_FIQ_STATUS  = 0x3c,
    IRQ_FIQ_MASK    = 0x40,
    C0_RST_CTRL     = 0x80,
    C1_RST_CTRL     = 0x84,
    CPUx_RST_CTRL   = 0,
    GENER_CTRL_REG  = 0x184,
    PRIVATE_REG0    = 0x1a4,
    PRIVATE_REG1    = 0x1a8,
    BOOT_HOTPLUG_REG = 0x1ac,
  };

  // Power control management registers
  enum {
    CPUS_CLK_REG             = 0x00,
    CPUx_PWROFF_GATING_base  = 0x100,
    C0CPUX_PWROFF_GATING_REG = 0x100,
    C1CPUX_PWROFF_GATING_REG = 0x104,
    GPU_PWROFF_GATING_REG    = 0x118,
    CPU_PWR_base             = 0x140,
    C0_CPU0_PWR_SWITCH_CTRL  = 0x140,
    C0_CPU1_PWR_SWITCH_CTRL  = 0x144,
    C0_CPU2_PWR_SWITCH_CTRL  = 0x148,
    C0_CPU3_PWR_SWITCH_CTRL  = 0x14c,
    C1_CPU0_PWR_SWITCH_CTRL  = 0x150,
    C1_CPU1_PWR_SWITCH_CTRL  = 0x154,
    C1_CPU2_PWR_SWITCH_CTRL  = 0x158,
    C1_CPU3_PWR_SWITCH_CTRL  = 0x15c,
  };

  Mmio_register_block cpucfg(Kmem::mmio_remap(Mem_layout::Cpu_cfg_phys_base));
  Mmio_register_block r_prcm(Kmem::mmio_remap(Mem_layout::R_cpu_cfg_phys_base));

  printf("BPI M3: booting up other cores");

  Ipi::bcast(Ipi::Global_request, Cpu_number::boot_cpu());
}

/*
static inline void sunxi_set_secondary_entry(void *entry)
{
	sunxi_smc_writel((u32)entry, (void *)(SUNXI_R_CPUCFG_VBASE + PRIVATE_REG0));
}
*/
