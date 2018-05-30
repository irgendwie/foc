/*
 * Copyright (C) 2017 Kernkonzept GmbH.
 * Author(s): Sarah Hoffmann <sarah.hoffmann@kernkonzept.com>
 *
 * This file is distributed under the terms of the GNU General Public
 * License, version 2.  Please see the COPYING-GPL-2 file for details.
 */
#pragma once

#include <l4/cxx/list_alloc>

#include <l4/re/dataspace>
#include <l4/re/env>
#include <l4/re/rm>
#include <l4/re/util/cap_alloc>
#include <l4/re/util/unique_cap>
#include <l4/re/util/env_ns>

#include <l4/sys/cxx/ipc_types>
#include <l4/sys/irq>

#include <l4/l4virtio/server/virtio>
#include <l4/l4virtio/server/l4virtio>
#include <l4/l4virtio/l4virtio>

#include <cassert>
#include <pthread.h>

#include "debug.h"

namespace Trusty {

static void *wait_for_device_thread(void *obj);

class Virtqueue : public L4virtio::Virtqueue
{
private:
  /// Index of next free entry in the descriptor table.
  l4_uint16_t _next_free;

public:
  enum End_of_queue
  {
    // Indicates the end of the queue.
    Eoq = 0xFFFF
  };

  Virtqueue() : _next_free(Eoq) {}

 /**
   * Initialize the descriptor table and the index structures
   * of this queue.
   *
   * \param num    The number of entries in the descriptor table, the
   *               available ring, and the used ring (must be a power of 2).
   *
   * \pre The queue must be set up correctly with setup() or setup_simple().
   */
  void initialize_rings(unsigned num)
  {
    _used->idx = 0;
    _avail->idx = 0;

    // setup the freelist
    for (l4_uint16_t d = 0; d < num - 1; ++d)
      _desc[d].next = d + 1;
    _desc[num - 1].next = Eoq;
    _next_free = 0;
  }

  /**
   * Initialize this virtqueue.
   *
   * \param num    The number of entries in the descriptor table, the
   *               available ring, and the used ring (must be a power of 2).
   * \param base   The base address for the queue data structure.
   *
   * This function sets up the memory and initializes the freelist.
   */
  void init_queue(unsigned num, void *base)
  {
    setup_simple(num, base);
    initialize_rings(num);
  }


  /**
   * Allocate and return an unused descriptor from the descriptor table.
   *
   * The descriptor will be removed from the free list, the content
   * should be considered undefined. After use, it needs to be freed
   * using free_descriptor().
   *
   * \return The index of the reserved descriptor or Virtqueue::Eoq if
   *         no free descriptor is available.
   *
   * Note: the implementation uses (2^16 - 1) as the end of queue marker.
   *       That means that the final entry in the queue can not be allocated
   *       iff the queue size is 2^16.
   */
  l4_uint16_t alloc_descriptor()
  {
    // XXX needs to be locked
    l4_uint16_t idx = _next_free;
    if (idx == Eoq)
      return Eoq;

    _next_free = _desc[idx].next;

    return idx;
  }

  /**
   * Enqueue a descriptor in the available ring.
   *
   * \param descno Index of the head descriptor to enqueue.
   */
  void enqueue_descriptor(l4_uint16_t descno)
  {
    if (descno > _idx_mask)
      throw L4::Bounds_error();

    // TODO lock required
    _avail->ring[_avail->idx++ & _idx_mask] = descno; // _avail->idx expected to wrap
  }

  Desc desc(l4_uint16_t descno)
  {
    assert(descno <= _idx_mask);

    return _desc[descno];
  }

  void set_desc(l4_uint16_t descno, void *ptr, l4_uint32_t len, bool write)
  {
    if (descno > _idx_mask)
      throw L4::Bounds_error();

    auto &d = _desc[descno];

    Desc::Flags flags(0);
    flags.write() = write ? 1 : 0;

    d.addr = virt_to_dev(ptr);
    d.len = len;
    d.flags = flags;
  }

  void chain_desc(l4_uint16_t head, l4_uint16_t next)
  {
    assert(head <= _idx_mask);
    assert(next <= _idx_mask);

    auto &h = _desc[head];

    h.flags.next() = 1;
    h.next = next;
  }

  /**
   * Return the next finished block.
   *
   * \return Index of the head or Virtqueue::Eoq
   *         if no used element is currently available.
   */
  Used_elem find_next_used()
  {
    // TODO lock required
    if (_current_avail == _used->idx)
      return Used_elem(Eoq, 0);

    L4virtio::rmb();

    return _used->ring[_current_avail++ & _idx_mask];
  }

  bool used_avail() const
  { return _current_avail != _used->idx;  }


  l4_uint16_t free_descriptor(l4_uint16_t idx)
  {
    assert (idx <= _idx_mask);

    auto &d = _desc[idx];

    l4_uint16_t next = d.flags.next() ? d.next : (l4_uint16_t) Eoq;

    // TODO lock required
    d.next = _next_free;
    _next_free = idx;

    return next;
  }

  void set_device_mem_base(l4_addr_t addr)
  { _device_mem_base = addr; }

private:
  L4virtio::Ptr<void> virt_to_dev(void *virt) const
  { return L4virtio::Ptr<void>((l4_addr_t) virt - _device_mem_base); }

  l4_addr_t _device_mem_base;
};

class Device_server
{
public:
  enum { Num_queues = 2 };

  Device_server(char const *device, unsigned buffer_size)
  {
    buffer_size *= 1024;
    _shared_ds = L4Re::chkcap(L4Re::Util::make_unique_cap<L4Re::Dataspace>());

    auto *e = L4Re::Env::env();
    L4Re::chksys(e->mem_alloc()->alloc(buffer_size, _shared_ds.get()));
    L4Re::chksys(e->rm()->attach(&_shared, buffer_size,
                                 L4Re::Rm::Search_addr,
                                 L4::Ipc::make_cap_rw(_shared_ds.get())));
    _shared_alloc.free((void *) _shared.get(), buffer_size, true);

    _device =
      L4Re::Util::Unique_cap<L4virtio::Device>(
          L4Re::chkcap(L4Re::Util::Env_ns().query<L4virtio::Device>(device)));

    for (unsigned i = 0; i < Num_queues; ++i)
      _queues[i].set_device_mem_base(_shared.get());
  }

  pthread_t contact_device(L4::Cap<L4::Irq> notify_irq)
  {
    _irq_from_device = notify_irq;

    pthread_t thr;

    L4Re::chksys(pthread_create(&thr, nullptr, &wait_for_device_thread, this),
                 "Create device register thread");

    return thr;
  }

  L4::Cap<L4::Irq> notification_irq() const
  { return _irq_to_device.get(); }

  void *register_device()
  {
    auto config_ds = L4Re::chkcap(L4Re::Util::make_unique_cap<L4Re::Dataspace>(),
                                  "Allocating capability for config space");
    _irq_to_device = L4Re::chkcap(L4Re::Util::make_unique_cap<L4::Irq>(),
                                  "Allocating capability for notification irq");

    L4Re::chksys(_device->register_iface(_irq_from_device, _irq_to_device.get(),
                                         config_ds.get()),
                 "Registering with virtio device");

    auto *e = L4Re::Env::env();
    L4Re::chksys(e->rm()->attach(&_config, L4_PAGESIZE, L4Re::Rm::Search_addr | L4Re::Rm::Eager_map,
                                 L4::Ipc::make_cap_rw(config_ds.get()), 0,
                                 L4_PAGESHIFT),
                 "Attaching config dataspace");

    if (memcmp(&_config->magic, "virt", 4) != 0)
      L4Re::chksys(-L4_ENODEV, "Device config has wrong magic value");

    if (_config->version != 2)
      L4Re::chksys(-L4_ENODEV, "Invalid virtio version, must be 2");

    if (!l4virtio_get_feature(_config->dev_features_map,
                              L4VIRTIO_FEATURE_CMD_CONFIG))
      L4Re::chksys(-L4_ENODEV, "Device does not support config via cmd.");

    // register the block buffer
    L4Re::chksys(_device->register_ds(L4::Ipc::make_cap_rw(_shared_ds.get()),
                                      0, 0, _shared_ds->size()));

    // setting config_ds markes device configured
    _config_ds = cxx::move(config_ds);

    // kick processor to start the initialization process
    _irq_from_device->trigger();

    return nullptr;
  }

  int initialize()
  {
    if (_config->status & L4VIRTIO_STATUS_FAILED)
      return -L4_EIO;

    if (_config->status & L4VIRTIO_STATUS_ACKNOWLEDGE)
      return L4_EOK;

    // reset
    set_status(0);

    // device initialisation
    int status = L4VIRTIO_STATUS_ACKNOWLEDGE;
    set_status(status);

    status |= L4VIRTIO_STATUS_DRIVER;
    set_status(status);

    config_features();

    if (_config->status & L4VIRTIO_STATUS_FAILED)
      L4Re::chksys(-L4_EIO, "Device failure during initialisation.");

    return (_config->status & L4VIRTIO_STATUS_FAILED) ? -L4_EIO : L4_EOK;
  }

  bool is_available() const
  { return _config_ds.is_valid(); }

  Virtqueue *queue(unsigned idx)
  { return _queues + idx; }

  int mirror_queue(unsigned idx, L4virtio::Svr::Virtqueue const *other)
  {
    if (idx >= Num_queues)
      return -L4_EINVAL;

    Dbg dbg(Dbg::Trace, "Device");

    dbg.printf("Mirroring queue %d\n", idx);

    auto *q = _queues + idx;
    auto *cfg = _config->queues() + idx;

    // compute maximum queue length
    unsigned num = other->num();

    if (num > cfg->num_max)
      num = cfg->num_max;

    dbg.printf("Queue size %d (other: %d maximum: %d)\n",
               num, other->num(), cfg->num_max);

    auto qsz = q->total_size(num);

    void *baseaddr = _shared_alloc.alloc(qsz, q->desc_align());

    if (!baseaddr)
      {
        Dbg(Dbg::Warn, "Device")
          .printf("Cannot allocate memory for queues (0x%lx bytes)\n",
                   qsz);
        return -L4_ENOMEM;
      }

    // setup internal structure
    q->init_queue(num, baseaddr);

    // setup config
    l4_addr_t dev_base = virt_to_dev((l4_addr_t) baseaddr);
    cfg->num = num;
    cfg->desc_addr = dev_base;
    cfg->avail_addr = dev_base + q->avail_offset();
    cfg->used_addr = dev_base + q->used_offset();
    cfg->ready = 1;

    _config->config_queue(idx, _irq_to_device.get(), _irq_from_device);

    dbg.printf("Initialised queue %d for device: d:0x%llx a:0x%llx u:0x%llx\n",
           idx, cfg->desc_addr, cfg->avail_addr, cfg->used_addr);

    return L4_EOK;
  }

  int finalize_setup()
  {
    set_status(_config->status | L4VIRTIO_STATUS_DRIVER_OK);

    return has_failed() ? -L4_EIO : L4_EOK;
  }

  bool has_failed() const
  { return _config->status & L4VIRTIO_STATUS_FAILED; }

  void set_failed()
  { set_status(_config->status | L4VIRTIO_STATUS_FAILED); }

  cxx::List_alloc *allocator()
  { return &_shared_alloc; }

private:
  void set_status(unsigned status) const
  { _config->set_status(status, _irq_to_device.get(), _irq_from_device); }

  // XXX check properly
  l4_addr_t virt_to_dev(l4_addr_t virt) const
  { return virt - _shared.get(); }

  void config_features()
  {
    if (!l4virtio_get_feature(_config->dev_features_map,
                              L4VIRTIO_FEATURE_CMD_CONFIG))
      L4Re::chksys(-L4_ENODEV, "Require configuration via cmd register.");

    // otherwise accept the feature offered
    _config->driver_features_map[0] = _config->dev_features_map[0];
    _config->driver_features_map[1] = _config->dev_features_map[1];

    set_status(_config->status | L4VIRTIO_STATUS_FEATURES_OK);

    if (!(_config->status & L4VIRTIO_STATUS_FEATURES_OK))
      L4Re::chksys(-L4_EINVAL, "Negotiation of device features.");
  }

  L4Re::Util::Unique_cap<L4virtio::Device> _device;
  L4Re::Util::Unique_cap<L4Re::Dataspace> _config_ds;
  L4Re::Util::Unique_cap<L4Re::Dataspace> _shared_ds;
  L4Re::Rm::Unique_region<L4virtio::Device::Config_hdr *> _config;
  L4Re::Rm::Unique_region<l4_addr_t> _shared;

  L4::Cap<L4::Irq> _irq_from_device;
  L4Re::Util::Unique_cap<L4::Irq> _irq_to_device;

  cxx::List_alloc _shared_alloc;
  Virtqueue _queues[Num_queues];
};

static void *wait_for_device_thread(void *obj)
{
  try
    {
      return ((Device_server *) obj)->register_device();
    }
  catch (L4::Runtime_error &e)
    {
      Err().printf("%s: %s\n", e.str(), e.extra_str());
    }

  return nullptr;
}

}
