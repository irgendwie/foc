/*
 * Copyright (C) 2017 Kernkonzept GmbH.
 * Author(s): Sarah Hoffmann <sarah.hoffmann@kernkonzept.com>
 *
 * This file is distributed under the terms of the GNU General Public
 * License, version 2.  Please see the COPYING-GPL-2 file for details.
 */
#pragma once

#include <l4/atkins/fixtures/epiface_provider>
#include <l4/atkins/factory>

#include <l4/cxx/list_alloc>

#include "mock_device.h"

/**
 * Simple Virtqueue implementation for the driver that adds
 * functions for managing free slots.
 */
class TestVirtqueue : public L4virtio::Virtqueue
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

  TestVirtqueue() : _next_free(Eoq) {}

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



/**
 * Provides a framework for testing the virtio proxy.
 *
 * The harness sets up a mock device that is connected to the device
 * endpoint of the proxy and provides functions that wrap various
 * common functions of the driver.
 */
class TestTrustyProxyHarness
: public Atkins::Fixture::Server_thread
{
public:
  /**
   * Constructs the base test setup.
   *
   * Initialises and connects the device side and allocates the resources
   * necessary for the test.
   */
  TestTrustyProxyHarness()
  {
    using Atkins::Factory::cap;

    auto *env = L4Re::Env::env();
    driver = L4Re::chkcap(env->get_cap<L4virtio::Device>("drvcap"),
                          "Getting capability for config page");
    drvirq = cap<L4::Irq>("Allocate capability for driver side");
    L4Re::chksys(env->factory()->create(drvirq.get()));

    devirq = cap<L4::Irq>("Allocate capability for device side");

    cfg_ds = cap<L4Re::Dataspace>("Allocate cap for config dataspace");

    device.register_service(server.registry(), "devcap");

    data = Atkins::Factory::ds_region(3 * L4_PAGESIZE);
    _shared_alloc.free(data->get<void *>(), 3 * L4_PAGESIZE, true);

    for (auto &q : _queues)
      q.set_device_mem_base(data->get<l4_addr_t>());

    start_loop();
  }

  ~TestTrustyProxyHarness()
  {
    server.registry()->unregister_obj(&device);
  }

protected:

  /**
   * Run the intial L4Virtio handshake.
   *
   * Registers the notification IRQs and shared dataspaces of
   * the driver side with the proxy.
   */
  void connect_driver()
  {
    L4Re::chksys(driver->register_iface(L4::Ipc::make_cap_rw(drvirq.get()),
                                        devirq.get(), cfg_ds.get()));
    L4Re::chksys(L4Re::Env::env()->rm()->attach(&config, L4_PAGESIZE,
                                                L4Re::Rm::Search_addr,
                                                L4::Ipc::make_cap_rw(cfg_ds.get()),
                                                0, L4_PAGESHIFT));
    L4Re::chksys(driver->register_ds(L4::Ipc::make_cap_rw(data->dataspace.get()), 0x4000, 0,
                                     data->dataspace->size()));
  }

  void setup_queue(unsigned idx, unsigned num)
  {
    auto q = _queues + idx;
    auto qsz = q->total_size(num);

    void *baseaddr = _shared_alloc.alloc(qsz, q->desc_align());

    if (!baseaddr)
      L4Re::chksys(-L4_ENOMEM, "Allocate memory for queues");

    // setup internal structure
    q->init_queue(num, baseaddr);

    l4_addr_t dev_base = virt_to_dev((l4_addr_t) baseaddr);

    // setup config
    auto *cfg = config->queues() + idx;
    cfg->num = num;
    cfg->desc_addr = dev_base;
    cfg->avail_addr = dev_base + q->avail_offset();
    cfg->used_addr = dev_base + q->used_offset();
    cfg->ready = 1;

    L4Re::chksys(driver->config_queue(idx),
                 "Sending queue configuration\n");
  }

  l4_addr_t virt_to_dev(l4_addr_t virt) const
  { return virt - data->get<l4_addr_t>() + 0x4000; }

  Mock_trusty_device device;

  L4::Cap<L4virtio::Device> driver;
  L4Re::Util::Unique_cap<L4::Irq> devirq;
  L4Re::Util::Unique_cap<L4::Irq> drvirq;
  L4Re::Util::Unique_cap<L4Re::Dataspace> cfg_ds;
  L4Re::Rm::Unique_region<L4virtio::Device::Config_hdr *> config;
  cxx::Ref_ptr<Atkins::Factory::Test_region> data;
  cxx::List_alloc _shared_alloc;
  TestVirtqueue _queues[Mock_trusty_device::Num_queues];

};


