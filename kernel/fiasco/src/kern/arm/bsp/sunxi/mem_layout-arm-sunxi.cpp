INTERFACE [arm && pf_sunxi]: //--------------------------------------------

EXTENSION class Mem_layout
{
public:
  enum Phys_layout_sunxi : Address {
    Mp_scu_phys_base     = 0xf8f00000,
    Gic_dist_phys_base   = 0x01c81000,
    Gic_cpu_phys_base    = 0x01c82000,
    Gic_h_phys_base      = 0x01c84000,
    Gic_v_phys_base      = 0x01c86000,
    Timer_phys_base      = 0x01c20c00,
  };
};

//-----------------------------
INTERFACE [arm && pf_sunxi && pf_sunxi_bpim3]:

EXTENSION class Mem_layout
{
public:
  enum Phys_layout_a83t : Address {
    /* CPUCFG internally, CPUXCFG under Linux */
    Cpu_cfg_phys_base    = 0x01700000,
    /* CoreLink cache-coherent interconnect (cci) control */
    Cci_400_phys_base    = 0x01790000,
    /* Cpu power/reset/clock management? Just a guess at the acronym. */
    R_prcm_phys_base     = 0x01f01400,
    /* Not documented in the Manual, just looking at the kernel fork here. */
    R_cpu_cfg_phys_base  = 0x01f01c00,
    /* Ccu (clock generation) configuration */
    Ccu_phys_base        = 0x01c20000,
    /* System revision version information */
    Sctrl_phys_base      = 0x01c00000,
    /* chip_id information */
    Sid_phys_base        = 0x01c14000,
  };
};
