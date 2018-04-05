/*
 * (c) 2016 Adam Lackorzynski <adam@l4re.org>
 *
 * This file is part of L4Re and distributed under the terms of the
 * GNU General Public License 2.
 * Please see the COPYING-GPL-2 file for details.
 */
#ifndef L4_CXX_UART_SH_H__
#define L4_CXX_UART_SH_H__

#include "uart_base.h"

namespace L4
{
  class Uart_sh : public Uart
  {
  public:
    explicit Uart_sh() {}
    bool startup(Io_register_block const *);
    void shutdown();
    bool enable_rx_irq(bool enable = true);
    bool change_mode(Transfer_mode m, Baud_rate r);
    int get_char(bool blocking = true) const;
    int char_avail() const;
    inline void out_char(char c) const;
    int write(char const *s, unsigned long count) const;
  };
};

#endif
