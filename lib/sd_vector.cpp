#include "sdsl/sd_vector.hpp"

//! Namespace for the succinct data structure library
namespace sdsl
{

sd_vector_builder::sd_vector_builder() :
    m_size(0), m_capacity(0),
    m_wl(0),
    m_tail(0), m_items(0),
    m_last_high(0), m_highpos(0)
{
}

sd_vector_builder::sd_vector_builder(size_type n, size_type m) :
    m_size(n), m_capacity(m),
    m_wl(0),
    m_tail(0), m_items(0),
    m_last_high(0), m_highpos(0)
{
    if(m_capacity > m_size)
    {
        throw std::runtime_error("sd_vector_builder: requested capacity is larger than vector size.");
    }

    size_type logm = bits::hi(m_capacity) + 1, logn = bits::hi(m_size) + 1;
    if(logm == logn)
    {
        logm--; // to ensure logn-logm > 0
    }
    m_wl = logn - logm;
    m_low = int_vector<>(m_capacity, 0, m_wl);
    m_high = bit_vector(m_capacity + (1ULL << logm), 0);
}


template<>
sd_vector<>::sd_vector(sd_vector_builder& builder)
{
    if(builder.items() != builder.capacity())
    {
      throw std::runtime_error("sd_vector: the builder is not full.");
    }

    m_size = builder.m_size;
    m_wl = builder.m_wl;
    m_low = std::move(builder.m_low);
    m_high = std::move(builder.m_high);
    util::init_support(m_high_1_select, &m_high);
    util::init_support(m_high_0_select, &m_high);

    builder = sd_vector_builder();
}

} // end namespace
