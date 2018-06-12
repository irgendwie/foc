#include "support.h"
#include "mmio_16550.h"

namespace {
class Platform_arm_tegrak1 : public Platform_single_region_ram
{
  bool probe() { return true; }

  void init()
  {
    switch (PLATFORM_UART_NR)
      {
      case 1:
        kuart.base_address = 0x70006000;
        kuart.irqno        = 68;
        break;
      case 2:
        kuart.base_address = 0x70006040;
        kuart.irqno        = 69;
      case 3:
        kuart.base_address = 0x70006200;
        kuart.irqno        = 78;
      default:
      case 4:
        kuart.base_address = 0x70006300;
        kuart.irqno        = 122;
        break;
      };
    kuart.reg_shift    = 2;
    kuart.base_baud    = 25459200;
    kuart.baud         = 115200;

    static L4::Uart_16550 _uart(kuart.base_baud, 0, 0, 0, 0);
    setup_16550_mmio_uart(&_uart);
  }
};
}

REGISTER_PLATFORM(Platform_arm_tegrak1);
