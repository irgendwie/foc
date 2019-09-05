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
    explicit Pmu(Address virt_cpu_cfg, Address virt_prcm, Address virt_rcpu)
      : _cpu_cfg(virt_cpu_cfg),
        _prcm_cfg(virt_prcm),
	      _rcpu_cfg(virt_rcpu)
    { }

  private:
    void cluster_on(int cluster) const;
    void cpu_on(int cluster, int cpu) const;
    void power_switch_on(int cluster, int cpu) const;
    void set_secondary_entry(Address) const;

    Mmio_register_block _cpu_cfg;
    Mmio_register_block _prcm_cfg;
    Mmio_register_block _rcpu_cfg;

    friend class Platform_control;
  };

  // System Control registers
  enum Cpu_Cfg_Registers : Address {
    CPUx_base       = 0x40,
    CPUx_offset     = 0x40,
    CTRL_REG0_base  = 0x00,
    CTRL_REG1_base  = 0x04,
    C0CTRL_REG0     = 0x00,
    C0CTRL_REG1     = 0x04,
    C1CTRL_REG0     = 0x10,
    C1CTRL_REG1     = 0x14,
    GENER_CTRL_REG0 = 0x28,
    GENER_CTRL_REG1 = 0x2c,
    CPU_STATUS_base = 0x30,
    C0_CPU_STATUS   = 0x30,
    C1_CPU_STATUS   = 0x34,
    IRQ_FIQ_STATUS  = 0x3c,
    IRQ_FIQ_MASK    = 0x40,
    RST_CTRL_base   = 0x80,
    C0_RST_CTRL     = 0x80,
    C1_RST_CTRL     = 0x84,
    CPUx_RST_CTRL   = 0,
    GENER_CTRL_REG  = 0x184,
    PRIVATE_REG0    = 0x1a4,
    PRIVATE_REG1    = 0x1a8,
    BOOT_HOTPLUG_REG = 0x1ac,
  };

  // Power control management registers
  enum Prcm_Cfg_Registers: Address {
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

  // Bit flags for parts of the RST_CTRL registers.
  // Exact function of each flag is not known exactly.
  enum Cpu_Rst_Ctrl_Val : Mword {
    SOC_DBG_RESET_MASK = 0x1 << 24,
    // A part of the debugging components.
    ETM_RESET_MASK     = 0xF << 20,
    DEBUG_RESET_MASK   = 0xF << 16,
    HRESET_MASK        = 0x1 << 12,
    L2CACHE_RESET_MASK = 0x1 <<  8,
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

  printf("BPI M3: boot_ap_cpus\n");

  Address cpucfg_addr = Kmem::mmio_remap(Mem_layout::Cpu_cfg_phys_base);
  Address prcm_addr = Kmem::mmio_remap(Mem_layout::R_prcm_phys_base);
  Address r_cpucfg_addr = Kmem::mmio_remap(Mem_layout::R_cpu_cfg_phys_base);
  Address cci_addr = Kmem::mmio_remap(Mem_layout::Cci_400_phys_base);
  Kmem::mmio_remap(Mem_layout::Cci_400_phys_base + Cci::SLAVE_3_Base);
  Kmem::mmio_remap(Mem_layout::Cci_400_phys_base + Cci::SLAVE_4_Base);

  pmu.construct(cpucfg_addr, prcm_addr, r_cpucfg_addr);
  cci.construct(cci_addr);

  printf("BPI M3: powering secondary cluster\n");
  pmu->cluster_on(1);

  printf("BPI M3: starting cci ports\n");
  cci_init();

  printf("BPI M3: booting up other cores\n");
  // Ipi::bcast(Ipi::Global_request, Cpu_number::boot_cpu());

  const int phys_ids [8] = {
    0x000, 0x001, 0x002, 0x003,
    0x100, 0x101, 0x102, 0x103,
  };

  /* Start all other cores, assuming this is cluster0-core0 */
  for(int i = 1; i < 8; i++) {
    printf(" enabling %i (mpidr %i)\n", i, phys_ids[i]);
    cpuboot(Cpu_phys_id(phys_ids[i]), phys_tramp_mp_addr);
    // Ipi::send(Ipi::Global_request, Cpu_number::boot_cpu(), Cpu_number(i));
  }

  printf("BPI M3: cores running\n");
}

IMPLEMENT_OVERRIDE static
void
Platform_control::init(Cpu_number cpu_id)
{
  Cpu& cpu = Cpu::cpus.cpu(cpu_id);
  printf("BPI M3: cpu ready %i(%i)\n",
    cxx::int_value<Cpu_number>(cpu_id),
    cxx::int_value<Cpu_phys_id>(cpu.phys_id()));
}

// Has an interface as if pcsi. But neither boot loader (stage0, uboot)
// supports that call for all cpus and clusters currently (2019).
PRIVATE static
int
Platform_control::cpuboot(Cpu_phys_id target, Address phys_tramp_mp_addr)
{
  auto id = cxx::int_value<Cpu_phys_id>(target);
  pmu->set_secondary_entry(phys_tramp_mp_addr);
  pmu->cpu_on(id >> 8, id);
  return 0;
}

PRIVATE static
void
Platform_control::cci_init()
{
  printf(" enabling slave port 3\n");
  cci->enable_slave_port(3);
  // printf(" enabling slave port 4\n");
  // cci->enable_slave_port(4);
}

IMPLEMENT
void
Platform_control::Pmu::set_secondary_entry(Address entry_addr) const {
  _rcpu_cfg
    .r<Mword>(Platform_control::PRIVATE_REG0)
    .write(entry_addr);
}

IMPLEMENT
void
Platform_control::Pmu::cluster_on(int idx) const {
  // We only have two clusters.
  idx = idx & 1;
  // The following is initialization code based on the Allwinner Linux kernel
  // TODO: the Linux code has some waits. Evaluate which are needed and why?

  // Grab the registers for the cluster we want to control.
  auto rst_ctrl = _cpu_cfg.r<Mword>(Platform_control::RST_CTRL_base + 0x4*idx);
  auto pwron_reset = _rcpu_cfg.r<Mword>(Platform_control::CPU_STATUS_base + 0x4*idx);
  auto ctrl_reg0 = _cpu_cfg.r<Mword>(Platform_control::CTRL_REG0_base + 0x10*idx);
  auto ctrl_reg1 = _cpu_cfg.r<Mword>(Platform_control::CTRL_REG1_base + 0x10*idx);
  auto pwroff_gating = _rcpu_cfg.r<Mword>(Platform_control::CPUx_PWROFF_GATING_base + 0x4*idx);

  // Assert reset for all cores.
  rst_ctrl.clear(0xF);
  // Assert power-on reset for all cores.
  pwron_reset.clear(0xF);

  // Assert resets for connected components. Includes the L2 cache while the
  // cores will do their own L1 caches on startup.
  Mword CLUSTER_CORE_RESETS =
    Platform_control::SOC_DBG_RESET_MASK
    | Platform_control::ETM_RESET_MASK
    | Platform_control::DEBUG_RESET_MASK
    | Platform_control::HRESET_MASK
    | Platform_control::L2CACHE_RESET_MASK;
  rst_ctrl.clear(CLUSTER_CORE_RESETS);

  // Set L2 reset disable to low.
  ctrl_reg0.clear(0x1 << 4);

  // active ACINACTM, Part of the reset of the ACE interface
  ctrl_reg1.set(0x1);
  // Clear pwroff gating
  pwroff_gating.clear((0x1 << 4) | 0x1);
  // de-activate ACINACTM,
  ctrl_reg1.clear(0x1);

  // Finally de-assert all resets we asserted earlier.
  rst_ctrl.set(CLUSTER_CORE_RESETS);

  // Core resets and power-on resets stay on!
}

IMPLEMENT
void
Platform_control::Pmu::cpu_on(int cluster, int cpu) const {
  // We have two clusters.
  cluster &= 1;
  // And four cpus per cluster.
  cpu &= 3;

  auto rst_ctrl = _cpu_cfg.r<Mword>(Platform_control::RST_CTRL_base + 0x4*cluster);
  auto pwron_reset = _rcpu_cfg.r<Mword>(Platform_control::CPU_STATUS_base + 0x4*cluster);
  auto ctrl_reg0 = _cpu_cfg.r<Mword>(Platform_control::CTRL_REG0_base + 0x10*cluster);
  auto pwroff_gating = _rcpu_cfg.r<Mword>(Platform_control::CPUx_PWROFF_GATING_base + 0x4*cluster);

  // Assert core reset control of core.
  rst_ctrl.clear(1 << cpu);
  // Assert power-on reset of core.
  pwron_reset.clear(1 << cpu);

  // Set L1RSTDISABLE of core to low.
  ctrl_reg0.clear(1 << cpu);

  // Release power switch.
  power_switch_on(cluster, cpu);

  // Clear poweroff gating
  pwroff_gating.clear(1 << cpu);

  // De-assert power-on reset.
  pwron_reset.set(1 << cpu);
  // De-assert core reset control.
  rst_ctrl.set(1 << cpu);
}

IMPLEMENT
void
Platform_control::Pmu::power_switch_on(int cluster, int cpu) const {
  auto pwr_clamp = _prcm_cfg.r<Mword>(Platform_control::CPU_PWR_base + 0x10*cluster + 0x4*cpu);
  // TODO: this is a copy from Linux. Has several magic values and it is
  // unclear what exactly they do. Seem to gradually enable something (note
  // that bits get cleared from the right, one or multiple at a time). The
  // documentation only specifies
  // * 0x00 – Power On
  // * 0xFF – Power Off
  // But it does not give a sequence for this.

  if(pwr_clamp.read() == 0x00) {
    printf("Power-switch for cluster:%i cpu:%i already enabled\n", cluster, cpu);
    return;
  }

  pwr_clamp.write(0xFE);
  Platform_control::udelay(20);

  pwr_clamp.write(0xF8);
  Platform_control::udelay(10);

  pwr_clamp.write(0xE0);
  Platform_control::udelay(10);

  /// TODO: this write appears only for SUN8IW6, but WHY?
  pwr_clamp.write(0xc0);
  Platform_control::udelay(10);

  pwr_clamp.write(0x80);
  Platform_control::udelay(10);

  pwr_clamp.write(0x00);
  Platform_control::udelay(20);

  while(pwr_clamp.read() != 0x00)
    ;
}

PRIVATE static
void
Platform_control::udelay(int us)
{
  // Running at ~1GHz
  for (int delay = 1000*us; delay; --delay)
    Mem::barrier();
}
