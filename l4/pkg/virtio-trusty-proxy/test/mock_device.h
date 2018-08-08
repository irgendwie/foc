/*
 * Copyright (C) 2017 Kernkonzept GmbH.
 * Author(s): Sarah Hoffmann <sarah.hoffmann@kernkonzept.com>
 *
 * This file is distributed under the terms of the GNU General Public
 * License, version 2.  Please see the COPYING-GPL-2 file for details.
 */
#pragma once

#include <l4/l4virtio/server/l4virtio>

#include <l4/re/env>
#include <l4/re/error_helper>
#include <l4/re/util/cap_alloc>
#include <l4/re/util/unique_cap>
#include <l4/sys/ipc.h>
#include <l4/vbus/vbus>
#include <l4/util/util.h>

#include <l4/atkins/factory>
#include <l4/atkins/debug>

#include <gtest/gtest.h>

#include "trusty_proto.h"

struct Mock_trusty_iface
: public L4::Kobject_t<Mock_trusty_iface, L4virtio::Device, 23>
{
  L4_INLINE_RPC(long, wait_for_cmd, (l4_uint32_t *));

  typedef L4::Typeid::Rpcs<wait_for_cmd_t> Rpcs;
};

/**
 * Simple virtio mock device.
 *
 * The device by itself only implements basic L4virtio setup.
 * Any further actions or reactions need to be steered by the test
 * itself.
 */
class Mock_trusty_device
: public L4virtio::Svr::Device,
  public L4::Epiface_t<Mock_trusty_device, Mock_trusty_iface>
{

  struct Host_irq : public L4::Irqep_t<Host_irq>
  {
    explicit Host_irq(Mock_trusty_device *s) : s(s) {}
    Mock_trusty_device *s;
    void handle_irq()
    { s->kick(); }
  };

public:
  enum { Vq_max = 32, Num_queues = 2 };

  Mock_trusty_device()
  : L4virtio::Svr::Device(&_dev_config),
    _dev_config(0x44, L4VIRTIO_ID_L4TRUSTY_IPC, 0, Num_queues),
    _host_irq(this)
  {
    _dev_config.set_host_feature(L4VIRTIO_FEATURE_CMD_CONFIG);

    _dev_config.reset_hdr();

    reset_queue_config(0, Vq_max);
    reset_queue_config(1, Vq_max);

    init_mem_info(4);

    _kick_guest_irq = L4Re::chkcap(L4Re::Util::make_unique_cap<L4::Irq>());
  }

  void register_service(L4::Registry_iface *registry, char const *name)
  {
    L4Re::chkcap(registry->register_obj(this, name));
    L4Re::chkcap(registry->register_irq_obj(&_host_irq));
  }

  void register_single_driver_irq() override
  { _kick_guest_irq.get().move(server_iface()->rcv_cap<L4::Irq>(0)); }

  Server_iface *server_iface() const override
  { return L4::Epiface::server_iface(); }

  L4::Cap<L4::Irq> device_notify_irq() const override
  { return _host_irq.obj_cap(); }

  L4::Cap<L4::Irq> driver_notify_irq() const
  { return _kick_guest_irq.get(); }

  void reset() override
  {
    for (auto &q: _queues)
      q.disable();
  }

  int reconfig_queue(unsigned index) override
  {
    if (index >= Num_queues)
      return -L4_ERANGE;

    Atkins::Dbg().printf("Queue config for queue %d requested\n", index);

    if (setup_queue(_queues + index, index, Vq_max))
      {
        return 0;
      }

    return -L4_EINVAL;
  }

  bool check_queues() override
  {
    for (auto &q: _queues)
      if (!q.ready())
        {
          reset();
          Atkins::Dbg().printf("failed to start queues\n");
          return false;
        }

    return true;
  }

  void kick()
  {
  }

  /**
   * Wait for an mmio-based command to change the status.
   *
   * Raises an exception if the new status is not the expected one
   * or the next command is not related to the status. Waits
   * forever if no command is sent.
   */
  void expect_new_status(unsigned new_status)
  {
    Atkins::Dbg().printf("Waiting for status %d\n", new_status);
    l4_uint32_t cmd;
    L4Re::chksys(obj_cap()->wait_for_cmd(&cmd));

    Atkins::Dbg().printf("Received command: 0x%x payload: %d\n",
                         cmd & L4VIRTIO_CMD_MASK, cmd & ~L4VIRTIO_CMD_MASK);
    if ((cmd & L4VIRTIO_CMD_MASK) != L4VIRTIO_CMD_SET_STATUS)
      L4Re::chksys(-L4_EINVAL, "Unexpected command");

    if ((cmd & ~L4VIRTIO_CMD_MASK) != new_status)
      L4Re::chksys(-L4_EINVAL, "Unexpected status set");

    L4Re::chksys(op_set_status(0, new_status));

    _dev_config.reset_cmd();
    _kick_guest_irq->trigger();
  }

  void expect_new_queue()
  {
    Atkins::Dbg().printf("Waiting for queue configuration\n");
    l4_uint32_t cmd;
    L4Re::chksys(obj_cap()->wait_for_cmd(&cmd));

    Atkins::Dbg().printf("Received command: 0x%x payload: %d\n",
                         cmd & L4VIRTIO_CMD_MASK, cmd & ~L4VIRTIO_CMD_MASK);
    if ((cmd & L4VIRTIO_CMD_MASK) != L4VIRTIO_CMD_CFG_QUEUE)
      L4Re::chksys(-L4_EINVAL, "Unexpected command");

    L4Re::chksys(reconfig_queue(cmd & ~L4VIRTIO_CMD_MASK),
                 "Configuration of device queue.");

    _dev_config.reset_cmd();
    _kick_guest_irq->trigger();
  }

  /**
   * Busily wait for the command field to be changed in the device config space.
   */
  long op_wait_for_cmd(Mock_trusty_iface::Rights, l4_uint32_t &cmd)
  {
    auto utcb = l4_utcb();
    cmd = cxx::access_once(&_dev_config.hdr()->cmd);

    while (cmd == 0)
      {
        l4_ipc_receive(_host_irq.obj_cap().cap(), utcb, L4_IPC_NEVER);
        cmd = cxx::access_once(&_dev_config.hdr()->cmd);
        l4_sleep(1);
      }
    while(cmd == 0);

    return L4_EOK;
  }

private:
  bool queues_ready() const
  {
    for (auto &q: _queues)
      if (!q.ready())
        return false;

    return true;
  }

  L4virtio::Svr::Dev_config _dev_config;
  L4virtio::Svr::Virtqueue _queues[Num_queues];
  L4Re::Util::Unique_cap<L4::Irq> _kick_guest_irq;
  Host_irq _host_irq;
};
