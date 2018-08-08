/*
 * Copyright (C) 2017 Kernkonzept GmbH.
 * Author(s): Sarah Hoffmann <sarah.hoffmann@kernkonzept.com>
 *
 * This file is distributed under the terms of the GNU General Public
 * License, version 2.  Please see the COPYING-GPL-2 file for details.
 */
#pragma once

#include <l4/re/util/debug>

struct Err : L4Re::Util::Err
{
  Err(Level l = Fatal)
    : L4Re::Util::Err(l, "trusty-proxy") {}
};

struct Dbg : L4Re::Util::Dbg
{
  enum
  {
    Warn  = 1,
    Info  = 2,
    Trace = 4,
  };

  explicit Dbg() : L4Re::Util::Dbg(Dbg::Info, "trusty-proxy", "") {}

  Dbg(unsigned long l = Info, char const *subsys = "")
    : L4Re::Util::Dbg(l, "trusty-proxy", subsys) {}
};
