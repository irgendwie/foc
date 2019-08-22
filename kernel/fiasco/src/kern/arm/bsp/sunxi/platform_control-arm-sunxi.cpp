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
  class Pmu
  {
  public:
    explicit Pmu(Address virt_cpu_cfg, Address virt_rcpu)
      : _cpu_cfg(virt_cpu_cfg),
	_rcpu_cfg(virt_rcpu)
    { }

  private:
    void cluster_power(int, bool) const;
    void cpu_power(int, bool) const;
    void set_secondary_entry(Address phys) const;

    Mmio_register_block _cpu_cfg;
    Mmio_register_block _rcpu_cfg;

    friend class Platform_control;
  };

  static Static_object<Pmu> pmu;
  static Static_object<Cci> cci;
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

Static_object<Platform_control::Pmu> Platform_control::pmu;
Static_object<Cci> Platform_control::cci;

// Boot all multi-processor clusters and cpus.
// The sequence is as follows:
//
// 1. Initialize the cci ports
// 2. Start cpus in sequence
// 2.1. (?) Enable cluster power if first cpu on cluster.
// 2.2. Power on the cpu
// 2.3. Reset the cpu to seconary entry address.
// 2.4. Wait for it to come online.
//
// Note that 2. would sometimes be done my the psci in full but the bootloader
// does not seem to support it. Some bits of the `mpidr` would then be reserved
// for the cluster address, usually (?) the second-least significant byte.
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

  printf("BPI M3: boot_ap_cpus");

  Address cpucfg_addr = Kmem::mmio_remap(Mem_layout::Cpu_cfg_phys_base);
  Address r_cpucfg_addr = Kmem::mmio_remap(Mem_layout::R_cpu_cfg_phys_base);
  Address cci_addr = Kmem::mmio_remap(Mem_layout::Cci_400_phys_base);

  pmu.construct(cpucfg_addr, r_cpucfg_addr);
  cci.construct(cci_addr);

  printf("BPI M3: starting cci ports");

  for(int i = 0; i < 2; i++) {
    cci_init(i);
  }

  printf("BPI M3: booting up other cores");

  Ipi::bcast(Ipi::Global_request, Cpu_number::boot_cpu());

  const int phys_ids [8] = {
    0x000, 0x001, 0x002, 0x003,
    0x100, 0x101, 0x102, 0x103,
  };

  for(int i = 0; i < 8; i++) {
    cpuboot(Cpu_phys_id(phys_ids[i]), phys_tramp_mp_addr);

    Ipi::send(Ipi::Global_request, Cpu_number::boot_cpu(), Cpu_number(i));
  }

  printf("BPI M3: cores running");
}

// Has an interface as if pcsi. But neither boot loader (stage0, uboot)
// supports that call for all cpus and clusters currently (2019).
PRIVATE static
int
Platform_control::cpuboot(Cpu_phys_id target, Address phys_tramp_mp_addr)
{
  pmu->set_secondary_entry(phys_tramp_mp_addr);
  return 0;
}

PRIVATE static
int
Platform_control::cci_init(int cluster)
{
  // cci->enable_slave_port(3);
  // cci->enable_slave_port(4);
}

/*
static inline void sunxi_set_secondary_entry(void *entry)
{
	sunxi_smc_writel((u32)entry, (void *)(SUNXI_R_CPUCFG_VBASE + PRIVATE_REG0));
}
*/
