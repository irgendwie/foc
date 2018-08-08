/*
 * Copyright (C) 2017 Kernkonzept GmbH.
 * Author(s): Sarah Hoffmann <sarah.hoffmann@kernkonzept.com>
 *
 * This file is distributed under the terms of the GNU General Public
 * License, version 2.  Please see the COPYING-GPL-2 file for details.
 */

#include <cstring>

#include <l4/cxx/exceptions>
#include <l4/atkins/tap/main>
#include <l4/atkins/debug>
#include <l4/atkins/l4_assert>

#include "test_driver.h"

typedef TestTrustyProxyHarness TrustyProxyBuffers;

/**
 * Check that the initial handshake works as expected.
 *
 * This test is a showcase on how to use the TrustyProxyHarness to
 * implement a specific test scenario.
 *
 * \note virtio-trusty-proxy currently does not support clients being
 *       disconnected and reconnected. This means that only exactly
 *       one scenario per test program can be run.
 */
TEST_F(TrustyProxyBuffers, BasicSetup)
{
  Atkins::Dbg dbg;

  dbg.printf("Initial handshake of device.\n");
  ASSERT_NO_THROW(device.expect_new_status(0));
  ASSERT_NO_THROW(device.expect_new_status(1));
  ASSERT_NO_THROW(device.expect_new_status(3));
  ASSERT_NO_THROW(device.expect_new_status(11));

  dbg.printf("Initial handshake of driver\n");
  ASSERT_NO_THROW(connect_driver());
  ASSERT_EQ(0, memcmp(&config->magic, "virt", 4));
  ASSERT_EQ(2U, config->version);
  ASSERT_L4OK(driver->set_status(1));
  ASSERT_L4OK(driver->set_status(3));

  dbg.printf("Setup of virtqueues\n");
  ASSERT_NO_THROW(setup_queue(0, 32));
  ASSERT_NO_THROW(setup_queue(1, 32));
  ASSERT_NO_THROW(device.expect_new_queue());
  ASSERT_NO_THROW(device.expect_new_queue());

  dbg.printf("Finalise setup of device\n");
  ASSERT_NO_THROW(device.expect_new_status(15));
  dbg.printf("Finalise setup of driver\n");
  ASSERT_L4OK(driver->set_status(11));
  ASSERT_L4OK(driver->set_status(15));
  dbg.printf("Test done\n");
}
