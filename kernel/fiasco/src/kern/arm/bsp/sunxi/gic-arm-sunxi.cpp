IMPLEMENTATION [arm && pic_gic && pf_sunxi_bpim3]:

IMPLEMENT_OVERRIDE inline
Unsigned32 Gic::pcpu_to_sgi(Cpu_phys_id cpu)
{
  unsigned id = cxx::int_value<Cpu_phys_id>(cpu);
  // Conceptually: ((id >> 8) & 0x1)*4 + ((id >> 0) & 0xF)
  // But that is unecessarily complex since values are limited to actually appearing ones.
  return (id >> 6) | (id & 3);
}
