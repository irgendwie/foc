INTERFACE [arm]:
/*
 * Controls CoreLink™ CCI-400 CacheCoherent Interconnect CCI controls multiple
 * ACE (or AXI4) masters (memories) and slaves (processors). Briefly, ACE is a
 * protocol on a snoop bus to guarantee cache control, and memory atomicity
 * between independent accessor of one addressable memory such as clusters of a
 * potentially asymmetric multi processor system.
*/

#include "mmio_register_block.h"

class Cci : public Mmio_register_block
{
public:
  explicit Cci(Address base) : Mmio_register_block(base)
    { }

  enum
  {
    // Offset of registers from the base location.
    REGISTER_OFFSET = 0x90000,
    // Length of the register mmio block.
    REGISTER_SIZE   = 0x10000,
    // Overall status register for cci.
    STATUS_REG      = 0xc,
    // cci400 supports two full ACE slaves, three ACE-lite slaves. Not
    // interested in ACE-lite here. The ACE master ones are S3 and S4. Maybe
    // the Mali GPU on S2 is interesting at some point.
    SLAVE_3_Base    = 0x94000,
    SLAVE_4_Base    = 0x95000,
  };

  enum Cci_Slave_interface
  {
    // Main control register.
    CONTOL_REG    = 0x000,
    // Just for fun, we likely don't need to use these.
    READ_QOS_REG  = 0x100,
    WRITE_QOS_REG = 0x104,
    QOS_CTRL_REG  = 0x10C,
    REGULATOR_REG = 0x130,
    QOS_SCALE_REG = 0x134,
    QOS_RANGE_REG = 0x138,
  };

  // Register masks for 32-bit STATUS_REG.
  enum Status_Val {
    // Whether changes to snoop or DVM are pending, e.g. power down needs to
    // hold while pending.
    PENDING_MASK = 0x1,
  };

  // Values for 32-bit SNOOP_REG.
  enum Slave_Control_Val {
    // Issue snoop requests.
    ENABLE_SNOOP_MASK   = 0x1,
    // Distributed Virtual Memory (DVM), required for MMU support.
    ENABLE_DVI_MASK     = 0x2,
    // Read masks for support flags.
    SUPPORTS_DVM_MASK   = (0x1 << 31),
    SUPPORTS_SNOOP_MASK = (0x1 << 30),
  };
};

IMPLEMENTATION [arm]:

// Enable cache control on a slave/processor port.
PUBLIC
void
Cci::enable_slave_port(int port) {
  // FIXME: not yet implemented.
  return;
}
