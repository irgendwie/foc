/*
 * Copyright (C) 2017 Kernkonzept GmbH.
 * Author(s): Sarah Hoffmann <sarah.hoffmann@kernkonzept.com>
 *
 * This file is distributed under the terms of the GNU General Public
 * License, version 2.  Please see the COPYING-GPL-2 file for details.
 */

#include <l4/re/env>
#include <l4/re/util/br_manager>
#include <l4/re/util/object_registry>

#include <cstdlib>
#include <getopt.h>

#include "debug.h"
#include "device_side.h"
#include "driver_side.h"
#include "buffer_copy.h"

static struct option options[] =
{
  {"verbose", 0, 0, 'v'},
  {"mem", 1, 0, 'm'},  // size of buffer memory in kB
  {0, 0, 0, 0}
};

Dbg warn(Dbg::Warn);
Dbg info(Dbg::Info);
Dbg trace(Dbg::Trace);

static L4Re::Util::Registry_server<L4Re::Util::Br_manager_hooks> server;

static int run(int argc, char *argv[])
{
  unsigned debuglevel = 1;
  L4Re::Util::Dbg::set_level(debuglevel);

  unsigned mem_size = 1024;
  int opt, index;

  while ((opt = getopt_long(argc, argv, "m:v", options, &index)) != -1)
    {
      switch (opt)
        {
        case 'm':
          mem_size = atoi(optarg);
          info.printf("Memory buffer for device: %u kB\n", mem_size);
          break;
        case 'v':
          debuglevel <<= 1;
          debuglevel |= 1;
          break;
        default:
          warn.printf("Unknown option '%c', ignoring.\n", opt);
          break;
        }
    }

  L4Re::Util::Dbg::set_level(debuglevel);

  auto driver = cxx::make_unique<Trusty::Driver_server>();
  server.registry()->register_obj(driver.get(), "drv");

  auto device = cxx::make_unique<Trusty::Device_server>("dev", mem_size);

  auto buffer_cp = cxx::make_unique<Trusty::Buffer_copy>();
  auto irq = server.registry()->register_irq_obj(buffer_cp.get());

  device->contact_device(irq);
  driver->set_host_irq(irq);

  buffer_cp->connect(cxx::move(driver), cxx::move(device));

  printf("Going into server loop\n");
  server.loop();
  return 0;
}

int main(int argc, char *argv[])
{
  try
    {
      return run(argc, argv);
    }
  catch (L4::Runtime_error &e)
    {
      Err().printf("%s: %s\n", e.str(), e.extra_str());
    }
  return -1;
}
