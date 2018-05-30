/*
 * Copyright (C) 2017 Kernkonzept GmbH.
 * Author(s): Sarah Hoffmann <sarah.hoffmann@kernkonzept.com>
 *
 * This file is distributed under the terms of the GNU General Public
 * License, version 2.  Please see the COPYING-GPL-2 file for details.
 */
#pragma once

#include <cassert>

#include <l4/l4virtio/server/virtio>
#include <l4/l4virtio/server/l4virtio>
#include <l4/l4virtio/l4virtio>
#include <l4/re/util/unique_cap>

#include "debug.h"
#include "trusty_proto.h"

namespace Trusty {

class Driver_server
: public L4virtio::Svr::Device,
  public L4::Epiface_t<Driver_server, L4virtio::Device>
{
public:
  enum { Vq_max = 32, Num_queues = 2 };

  Driver_server()
  : L4virtio::Svr::Device(&_dev_config),
    _dev_config(0x44, L4VIRTIO_ID_L4TRUSTY_IPC, 0, Num_queues)
  {
    _dev_config.reset_hdr();

    reset_queue_config(0, Vq_max);
    reset_queue_config(1, Vq_max);

    init_mem_info(4);

    _kick_guest_irq = L4Re::chkcap(L4Re::Util::make_unique_cap<L4::Irq>());
  }

  void set_host_irq(L4::Cap<L4::Irq> const &irq)
  { _host_irq = irq; }

  void register_single_driver_irq() override
  { _kick_guest_irq.get().move(server_iface()->rcv_cap<L4::Irq>(0)); }

  Server_iface *server_iface() const override
  { return L4::Epiface::server_iface(); }

  L4::Cap<L4::Irq> device_notify_irq() const override
  { return _host_irq; }

  L4::Cap<L4::Irq> driver_notify_irq() const
  { return _kick_guest_irq.get(); }

  long op_set_status(L4virtio::Device::Rights, unsigned _status)
  {
    change_status(_status);

    return L4_EOK;
  }

  void reset() override
  {
    for (auto &q: _queues)
      q.disable();
  }

  int reconfig_queue(unsigned index) override
  {
    if (index >= Num_queues)
      return -L4_ERANGE;

    Dbg trace(Dbg::Trace, "Driver");

    trace.printf("Queue config for queue %d requested\n", index);

    if (setup_queue(_queues + index, index, Vq_max))
      {
        if (queues_ready())
          {
            trace.printf("Queues have become ready. Kick buffer.\n");
            _host_irq->trigger();
          }

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
          Err().printf("failed to start queues\n");
          return false;
        }

    return true;
  }

  void notify_queue(L4virtio::Svr::Virtqueue *queue)
  {
    if (queue->no_notify_guest())
      return;

    _kick_guest_irq->trigger();
  }

  bool queues_ready() const
  {
    for (auto &q: _queues)
      if (!q.ready())
        return false;

    return true;
  }

  bool driver_ready()
  {
    return _dev_config.status().raw == 0xf;
  }

  L4virtio::Svr::Virtqueue const *queue(unsigned idx) const
  {
    assert(idx < Num_queues);
    return _queues + idx;
  }

  L4virtio::Svr::Virtqueue *queue(unsigned idx)
  {
    assert(idx < Num_queues);
    return _queues + idx;
  }

  void set_failed()
  { _dev_config.set_failed(); }

  bool handle_mem_cmd_write()
  {
    l4_uint32_t cmd = _dev_config.get_cmd();
    if (L4_LIKELY(!(cmd & L4VIRTIO_CMD_MASK)))
      return false;

    switch (cmd & L4VIRTIO_CMD_MASK)
      {
      case L4VIRTIO_CMD_SET_STATUS:
        change_status(cmd & ~L4VIRTIO_CMD_MASK);
        break;

      case L4VIRTIO_CMD_CFG_QUEUE:
        reconfig_queue(cmd & ~L4VIRTIO_CMD_MASK);
        break;

      default:
        // unknown command
        break;
      }

    _dev_config.reset_cmd();
    _kick_guest_irq->trigger();

    return true;
  }


private:

  void change_status(unsigned _status)
  {
    Dbg(Dbg::Trace, "Driver").printf("status change:  0x%x -> 0x%x\n",
                                     _dev_config.status().raw, _status);

    L4virtio::Svr::Dev_config::Status status(_status);

    if (_status == 0)
      {
        Dbg(Dbg::Warn, "Driver").printf("Resetting device\n");
        reset();
      }

    // do nothing if 'failed' is set
    if (status.failed())
      return;

    if (status.running() && !check_queues())
      status.failed() = 1;

    bool kickoff = _status == 0xf && _dev_config.status().raw != 0xf;

    _dev_config.set_status(status);

    if (kickoff)
      _host_irq->trigger();
  }


  L4virtio::Svr::Dev_config _dev_config;
  L4virtio::Svr::Virtqueue _queues[Num_queues];
  L4Re::Util::Unique_cap<L4::Irq> _kick_guest_irq;
  L4::Cap<L4::Irq> _host_irq;
};

}
