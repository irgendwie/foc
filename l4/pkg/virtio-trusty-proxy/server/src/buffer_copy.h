/*
 * Copyright (C) 2017 Kernkonzept GmbH.
 * Author(s): Sarah Hoffmann <sarah.hoffmann@kernkonzept.com>
 *
 * This file is distributed under the terms of the GNU General Public
 * License, version 2.  Please see the COPYING-GPL-2 file for details.
 */
#pragma once

#include <l4/sys/cxx/ipc_epiface>

#include "debug.h"
#include "driver_side.h"
#include "device_side.h"

namespace Trusty {

class Queue_handler : public L4virtio::Svr::Request_processor
{
  struct Buffer
  {
    Buffer() = default;
    Buffer(L4virtio::Svr::Driver_mem_region const *r,
           Virtqueue::Desc const &d,
           L4virtio::Svr::Request_processor const *)
    {
      addr = static_cast<char *>(r->local(d.addr));
      len = d.len;
      flags = d.flags;
    }

    char *addr;
    l4_uint32_t len;
    L4virtio::Virtqueue::Desc::Flags flags;
  };

  struct Shadow_buffer
  {
    L4virtio::Svr::Virtqueue::Head_desc head;
    Buffer drv_buffer;
    void *devaddr;
  };

public:
  void set_driver_meminfo(Trusty::Driver_server::Mem_list const *drv_meminfo)
  { _drv_meminfo = drv_meminfo; }

  void set_dev_alloc(cxx::List_alloc *dev_alloc)
  { _dev_alloc = dev_alloc; }

  void set_endpoints(Virtqueue *device, L4::Cap<L4::Irq> device_irq,
                     L4virtio::Svr::Virtqueue *driver,
                     L4::Cap<L4::Irq> driver_irq)
  {
    _qdev = device;
    _qdrv = driver;
    _device_irq = device_irq;
    _driver_irq = driver_irq;

    _outgoing_prev_idx = Trusty::Virtqueue::Eoq;
  }

  void config_queue()
  {
    _shadow.reset(new Shadow_buffer[_qdev->num()]);
  }

  void disable_notify()
  {
    _qdrv->disable_notify();
    _qdev->no_notify_host(true);
  }

  void enable_notify()
  {
    _qdrv->enable_notify();
    _qdev->no_notify_host(false);
  }

  void copy()
  {
    Dbg dbg(Dbg::Trace, _dbg_name);

    bool needs_notify = false;

    // incoming packets
    while (_incoming_head || start_incoming())
      {
        // get an outgoing descriptor
        auto dev_desc_id = _qdev->alloc_descriptor();
        dbg.printf("incoming buffer %llx (%x) goes to %d\n",
                    _incoming_head.desc()->addr.get(),
                    _incoming_head.desc()->len, dev_desc_id);

        if (dev_desc_id == Trusty::Virtqueue::Eoq)
          {
            dbg.printf("WARNING: device queue is full\n");
            break;
          }

        auto *shadow = &_shadow[dev_desc_id];

        if (_outgoing_head_idx == Trusty::Virtqueue::Eoq)
          _outgoing_head_idx = dev_desc_id;

        shadow->head = _incoming_head;
        shadow->drv_buffer = _incoming_buffer;

        // allocate an equal buffer for the other side
        shadow->devaddr = _dev_alloc->alloc(_incoming_buffer.len, 8);

        dbg.printf("outgoing buffer allocated at %p\n", shadow->devaddr);

        if (shadow->devaddr == 0)
          {
            // XXX need t have a descriptor in-flight for this case
            dbg.printf("Could not allocate memory for buffer. TODO\n");
            break;
          }

        // copy data over if necessary (assume a block is either read or write)
        if (!_incoming_buffer.flags.write())
          {
            dbg.printf("Copy content in (0x%x bytes) \n", _incoming_buffer.len);
            memcpy(shadow->devaddr, _incoming_buffer.addr, _incoming_buffer.len);
          }
        _qdev->set_desc(dev_desc_id, shadow->devaddr, _incoming_buffer.len,
                        _incoming_buffer.flags.write());
        if (_outgoing_prev_idx != Trusty::Virtqueue::Eoq)
          _qdev->chain_desc(_outgoing_prev_idx, dev_desc_id);

        if (!next(_drv_meminfo, &_incoming_buffer))
          {
            dbg.printf("Send away buffer %d\n", _outgoing_head_idx);
            _qdev->enqueue_descriptor(_outgoing_head_idx);
            _outgoing_head_idx = Trusty::Virtqueue::Eoq;
            _outgoing_prev_idx = Trusty::Virtqueue::Eoq;
            _incoming_head = L4virtio::Svr::Virtqueue::Head_desc();
            needs_notify = true;
          }
        else
          _outgoing_prev_idx = dev_desc_id;
      }

    if (needs_notify)
      {
        dbg.printf("Kick device side\n");
        _device_irq->trigger(); // XXX no notify?
        needs_notify = false;
      }

    // returned packages
    auto used = _qdev->find_next_used();
    while (used.id != Trusty::Virtqueue::Eoq)
      {
        dbg.printf("Used buffer received (%d, len 0x%x)\n", used.id, used.len);
        l4_uint16_t desc_id = used.id;
        do
          {
            // get corresponding block from driver side
            auto *shadow = &_shadow[desc_id];

            // copy data back if necessary
            // XXX can we restrict that to used.len?
            if (shadow->drv_buffer.flags.write()) {
              dbg.printf("Write back 0x%x bytes %p -> %p\n",
                         shadow->drv_buffer.len,
                         shadow->devaddr, shadow->drv_buffer.addr);
              memcpy(shadow->drv_buffer.addr, shadow->devaddr,
                     shadow->drv_buffer.len);
            }

            dbg.printf("Freeing used buffer %d\n", desc_id);
            desc_id = _qdev->free_descriptor(desc_id);
            _dev_alloc->free(shadow->devaddr, shadow->drv_buffer.len);
          }
        while (desc_id != Trusty::Virtqueue::Eoq);

        // mark finished on driver side
        dbg.printf("Marking as consumed for driver: %llx\n",
                   _shadow[used.id].head.desc()->addr.get());
        _qdrv->consumed(_shadow[used.id].head, used.len);
        needs_notify = true;

        used = _qdev->find_next_used();
      }

    if (needs_notify)
      {
        dbg.printf("Kick driver side\n");
        _driver_irq->trigger();
      }
  }

  bool work_pending()
  {
    return ready() && (_qdrv->desc_avail() || _qdev->used_avail());
  }

  void set_debug(char const *name)
  { _dbg_name = name; }

private:
  bool ready() const
  { return _qdev->ready() && _qdrv->ready() ; }

  bool start_incoming()
  {
    auto r = _qdrv->next_avail();

    if (L4_UNLIKELY(!r))
        return false;

    _incoming_head = start(_drv_meminfo, r, &_incoming_buffer);

    return true;
  }

  Virtqueue *_qdev;
  L4::Cap<L4::Irq> _device_irq;

  L4virtio::Svr::Virtqueue *_qdrv;
  L4::Cap<L4::Irq> _driver_irq;

  L4virtio::Svr::Virtqueue::Head_desc _incoming_head;
  l4_uint16_t _outgoing_head_idx;
  l4_uint16_t _outgoing_prev_idx;
  Buffer _incoming_buffer;
  Trusty::Driver_server::Mem_list const *_drv_meminfo;
  cxx::List_alloc *_dev_alloc;
  cxx::unique_ptr<Shadow_buffer[]> _shadow;
  char const * _dbg_name;
};

class Buffer_copy
: public L4::Irqep_t<Buffer_copy>
{
  enum { Num_queues = 2 };

public:
  void connect(cxx::unique_ptr<Trusty::Driver_server> &&drv,
              cxx::unique_ptr<Trusty::Device_server> &&dev)
  {
    _drv = cxx::move(drv);
    _dev = cxx::move(dev);

    for (unsigned i = 0; i < Num_queues; ++i)
      {
        _qhandler[i].set_driver_meminfo(_drv->mem_info());
        _qhandler[i].set_dev_alloc(_dev->allocator());
        _qhandler[i].set_endpoints(_dev->queue(i), _dev->notification_irq(),
                                   _drv->queue(i), _drv->driver_notify_irq());
      }

    _qhandler[0].set_debug("Q1");
    _qhandler[1].set_debug("Q2");
  }

  void handle_irq()
  {
    _drv->handle_mem_cmd_write();

    if (!_device_available)
      {
        if (_dev->is_available())
          {
            if (mirror_queues() != L4_EOK)
              return;
            _device_available = true;
          }
        else
          return;
      }

    if (_drv->driver_ready())
      handle_queues();
  }

private:
  int mirror_queues()
  {
    Dbg warn(Dbg::Warn, "Buffers");

    if (_dev->initialize() != L4_EOK)
      return -L4_EIO;

    if (!_drv->queues_ready())
      {
        Dbg(Dbg::Trace, "Buffers").printf("Driver queues not yet ready.\n");
        return -L4_EIO;
      }

    for (unsigned i = 0; i < Trusty::Driver_server::Num_queues; ++i)
      {
        if (_dev->mirror_queue(i, _drv->queue(i)) != L4_EOK)
          {
            _drv->set_failed();
            _dev->set_failed();
            warn.printf("Mirroring queue failed\n");
            return -L4_EIO;
          }

        _qhandler[i].config_queue();
      }

    if (_dev->finalize_setup() != L4_EOK) {
      _drv->set_failed();
      warn.printf("Finalizing device failed\n");
      return -L4_EIO;
    }

    return L4_EOK;
  }

  void handle_queues()
  {
    for (;;)
      {
        for (auto &q: _qhandler)
          {
            if (q.work_pending())
              {
                q.disable_notify();
                q.copy();
                q.enable_notify();
              }
          }

        L4virtio::wmb();
        L4virtio::rmb();

        bool work = false;
        for (auto &q: _qhandler)
          if (L4_UNLIKELY((work |= q.work_pending())))
              break;

        if (L4_LIKELY(!work))
          break;
      }
  }

  cxx::unique_ptr<Trusty::Driver_server> _drv;
  cxx::unique_ptr<Trusty::Device_server> _dev;
  Queue_handler _qhandler[Num_queues];

  bool _device_available = false;
};

}
