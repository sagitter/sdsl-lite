// Copyright (c) 2016, the SDSL Project Authors.  All rights reserved.
// Please see the AUTHORS file for details.  Use of this source code is governed
// by a BSD license that can be found in the LICENSE file.
/*!\file csa_sampling_strategy.hpp
 * \brief csa_sampling_strategy.hpp includes different strategy classes for suffix array sampling in the CSAs.
 * \author Simon Gog
 */

#ifndef INCLUDED_CSA_SAMPLING_STRATEGY
#define INCLUDED_CSA_SAMPLING_STRATEGY

/*
 *       Text = ABCDEFABCDEF$
 *              0123456789012
 *       sa_sample_dens = 2
 *    *1 SA *2
 *     * 12 *   $
 *       06 *   ABCDEF$
 *     * 00 *   ABCDEFABCDEF$
 *       07     BCDEF$
 *     * 01     BCDEFABCDEF$
 *       08 *   CDEF$
 *     * 02 *   CDEFABCDEF$
 *       09     DEF$
 *     * 03     DEFABCDEF$
 *       10 *   EF$
 *     * 04 *   EFABCDEF$
 *       11     F$
 *     * 05     FABCDEF$
 *
 *    The first sampling (*1) is called suffix order sampling. It has the advantage, that
 *    we don't need to store a bitvector, which marks the sampled suffixes, since a suffix
 *    at index \(i\) in the suffix array is marked if \( 0 \equiv i \mod sa_sample_dens \).
 *
 *   The second sampling (*2) is called text order sampling. It is also called regular in [1].
 *
 * [1] P.Ferragina, J. Siren, R. Venturini: Distribution-Aware Compressed Full-Text Indexes, ESA 2011
 */

#include <iosfwd>
#include <set>
#include <stdint.h>
#include <string>
#include <tuple>
#include <type_traits>
#include <utility>

#include <sdsl/bits.hpp>
#include <sdsl/cereal.hpp>
#include <sdsl/config.hpp>
#include <sdsl/construct.hpp>
#include <sdsl/construct_isa.hpp>
#include <sdsl/csa_alphabet_strategy.hpp>
#include <sdsl/int_vector.hpp>
#include <sdsl/int_vector_buffer.hpp>
#include <sdsl/inv_perm_support.hpp>
#include <sdsl/io.hpp>
#include <sdsl/memory_tracking.hpp>
#include <sdsl/ram_fs.hpp>
#include <sdsl/rank_support_v.hpp>
#include <sdsl/rrr_vector.hpp>
#include <sdsl/sd_vector.hpp>
#include <sdsl/sdsl_concepts.hpp>
#include <sdsl/structure_tree.hpp>
#include <sdsl/util.hpp>
#include <sdsl/wt_int.hpp>

namespace sdsl
{

template <class t_csa, uint8_t t_width = 0>
class _sa_order_sampling : public int_vector<t_width>
{
public:
    typedef int_vector<t_width> base_type;
    typedef typename base_type::size_type size_type;   // make typedefs of base_type visible
    typedef typename base_type::value_type value_type; //
    enum
    {
        sample_dens = t_csa::sa_sample_dens
    };
    enum
    {
        text_order = false
    };
    typedef sa_sampling_tag sampling_category;

    //! Default constructor
    _sa_order_sampling()
    {}

    //! Constructor
    /*
     * \param cconfig Cache configuration (SA is expected to be cached.).
     * \param csa     Pointer to the corresponding CSA. Not used in this class.
     * \par Time complexity
     *      Linear in the size of the suffix array.
     */
    _sa_order_sampling(cache_config const & cconfig, SDSL_UNUSED t_csa const * csa = nullptr)
    {
        int_vector_buffer<> sa_buf(cache_file_name(conf::KEY_SA, cconfig));
        size_type n = sa_buf.size();
        this->width(bits::hi(n) + 1);
        this->resize((n + sample_dens - 1) / sample_dens);

        for (size_type i = 0, cnt_mod = sample_dens, cnt_sum = 0; i < n; ++i, ++cnt_mod)
        {
            size_type sa = sa_buf[i];
            if (sample_dens == cnt_mod)
            {
                cnt_mod = 0;
                base_type::operator[](cnt_sum++) = sa;
            }
        }
    }

    //! Determine if index i is sampled or not
    inline bool is_sampled(size_type i) const
    {
        return 0 == (i % sample_dens);
    }

    //! Return the suffix array value for the sampled index i
    inline value_type operator[](size_type i) const
    {
        return base_type::operator[](i / sample_dens);
    }
};

template <uint8_t t_width = 0>
struct sa_order_sa_sampling
{
    template <class t_csa>
    using type = _sa_order_sampling<t_csa, t_width>;
    using sampling_category = sa_sampling_tag;
};

template <class t_csa, class t_bv = bit_vector, class t_rank = typename t_bv::rank_1_type, uint8_t t_width = 0>
class _text_order_sampling : public int_vector<t_width>
{
private:
    t_bv m_marked;
    t_rank m_rank_marked;

public:
    typedef int_vector<t_width> base_type;
    typedef typename base_type::size_type size_type;   // make typedefs of base_type visible
    typedef typename base_type::value_type value_type; //
    typedef t_bv bv_type;
    enum
    {
        sample_dens = t_csa::sa_sample_dens
    };
    enum
    {
        text_order = true
    };
    typedef sa_sampling_tag sampling_category;

    bv_type const & marked = m_marked;
    t_rank const & rank_marked = m_rank_marked;

    //! Default constructor
    _text_order_sampling()
    {}

    //! Constructor
    /*
     * \param cconfig Cache configuration (SA is expected to be cached.).
     * \param csa    Pointer to the corresponding CSA. Not used in this class.
     * \par Time complexity
     *      Linear in the size of the suffix array.
     */
    _text_order_sampling(cache_config const & cconfig, SDSL_UNUSED t_csa const * csa = nullptr)
    {
        int_vector_buffer<> sa_buf(cache_file_name(conf::KEY_SA, cconfig));
        size_type n = sa_buf.size();
        bit_vector marked(n, 0); // temporary bitvector for the marked text positions
        this->width(bits::hi(n / sample_dens) + 1);
        this->resize((n + sample_dens - 1) / sample_dens);

        for (size_type i = 0, sa_cnt = 0; i < n; ++i)
        {
            size_type sa = sa_buf[i];
            if (0 == (sa % sample_dens))
            {
                marked[i] = 1;
                base_type::operator[](sa_cnt++) = sa / sample_dens;
            }
        }
        m_marked = std::move(t_bv(marked));
        util::init_support(m_rank_marked, &m_marked);
    }

    //! Copy constructor
    _text_order_sampling(_text_order_sampling const & st) : base_type(st)
    {
        m_marked = st.m_marked;
        m_rank_marked = st.m_rank_marked;
        m_rank_marked.set_vector(&m_marked);
    }

    //! Determine if index i is sampled or not
    inline bool is_sampled(size_type i) const
    {
        return m_marked[i];
    }

    //! Return the suffix array value for the sampled index i
    inline value_type operator[](size_type i) const
    {
        return base_type::operator[](m_rank_marked(i)) * sample_dens;
    }

    value_type condensed_sa(size_type i) const
    {
        return base_type::operator[](i);
    }

    //! Assignment operation
    _text_order_sampling & operator=(_text_order_sampling const & st)
    {
        if (this != &st)
        {
            base_type::operator=(st);
            m_marked = st.m_marked;
            m_rank_marked = st.m_rank_marked;
            m_rank_marked.set_vector(&m_marked);
        }
        return *this;
    }

    //! Swap operation
    void swap(_text_order_sampling & st)
    {
        base_type::swap(st);
        m_marked.swap(st.m_marked);
        util::swap_support(m_rank_marked, st.m_rank_marked, &m_marked, &(st.m_marked));
    }

    size_type serialize(std::ostream & out, structure_tree_node * v = nullptr, std::string name = "") const
    {
        structure_tree_node * child = structure_tree::add_child(v, name, util::class_name(*this));
        size_type written_bytes = 0;
        written_bytes += base_type::serialize(out, child, "samples");
        written_bytes += m_marked.serialize(out, child, "marked");
        written_bytes += m_rank_marked.serialize(out, child, "rank_marked");
        structure_tree::add_size(child, written_bytes);
        return written_bytes;
    }

    void load(std::istream & in)
    {
        base_type::load(in);
        m_marked.load(in);
        m_rank_marked.load(in);
        m_rank_marked.set_vector(&m_marked);
    }

    template <typename archive_t>
    void CEREAL_SAVE_FUNCTION_NAME(archive_t & ar) const
    {
        base_type::CEREAL_SAVE_FUNCTION_NAME(ar);
        ar(CEREAL_NVP(m_marked));
        ar(CEREAL_NVP(m_rank_marked));
    }

    template <typename archive_t>
    void CEREAL_LOAD_FUNCTION_NAME(archive_t & ar)
    {
        base_type::CEREAL_LOAD_FUNCTION_NAME(ar);
        ar(CEREAL_NVP(m_marked));
        ar(CEREAL_NVP(m_rank_marked));
        m_rank_marked.set_vector(&m_marked);
    }
};

template <class t_bit_vec = sd_vector<>, class t_rank_sup = typename t_bit_vec::rank_1_type, uint8_t t_width = 0>
struct text_order_sa_sampling
{
    template <class t_csa>
    using type = _text_order_sampling<t_csa, t_bit_vec, t_rank_sup, t_width>;
    using sampling_category = sa_sampling_tag;
};

template <class t_csa,
          class t_bv_sa = sd_vector<>,
          class t_bv_isa = sd_vector<>,
          class t_rank_sa = typename t_bv_sa::rank_1_type,
          class t_select_isa = typename t_bv_isa::select_1_type>
class _fuzzy_sa_sampling
{
private:
    t_bv_sa m_marked_sa;
    t_rank_sa m_rank_marked_sa;
    t_bv_isa m_marked_isa;
    t_select_isa m_select_marked_isa;
    wt_int<rrr_vector<63>> m_inv_perm;

public:
    typedef typename bit_vector::size_type size_type;   // make typedefs of base_type visible
    typedef typename bit_vector::value_type value_type; //
    typedef t_bv_sa bv_sa_type;
    enum
    {
        sample_dens = t_csa::sa_sample_dens
    };
    enum
    {
        text_order = true
    };
    typedef sa_sampling_tag sampling_category;

    t_bv_sa const & marked_sa = m_marked_sa;
    t_rank_sa const & rank_marked_sa = m_rank_marked_sa;
    t_bv_isa const & marked_isa = m_marked_isa;
    t_select_isa const & select_marked_isa = m_select_marked_isa;

    //! Default constructor
    _fuzzy_sa_sampling()
    {}

    //! Constructor
    /*
     * \param cconfig Cache configuration (SA is expected to be cached.).
     * \param csa    Pointer to the corresponding CSA. Not used in this class.
     * \par Time complexity
     *      Linear in the size of the suffix array.
     */
    _fuzzy_sa_sampling(cache_config & cconfig, SDSL_UNUSED t_csa const * csa = nullptr)
    {
        {
            // (2) check, if the suffix array is cached
            if (!cache_file_exists(conf::KEY_ISA, cconfig))
            {
                auto event = memory_monitor::event("ISA");
                construct_isa(cconfig);
            }
            register_cache_file(conf::KEY_SA, cconfig);
        }
        {
            int_vector_buffer<> isa_buf(cache_file_name(conf::KEY_ISA, cconfig));
            size_type n = isa_buf.size();
            bit_vector marked_isa(n, 0); // temporary bitvector for marked ISA positions
            bit_vector marked_sa(n, 0);  // temporary bitvector for marked SA positions
            int_vector<> inv_perm((n + sample_dens - 1) / sample_dens, 0, bits::hi(n) + 1);
            size_type cnt = 0;
            size_type runs = 1;

            uint64_t min_prev_val = 0;
            for (size_type i = 0; i < n; i += sample_dens)
            {
                size_type pos_min = i;
                size_type pos_cnd = isa_buf[i] >= min_prev_val ? i : n;
                for (size_type j = i + 1; j < i + sample_dens and j < n; ++j)
                {
                    if (isa_buf[j] < isa_buf[pos_min])
                        pos_min = j;
                    if (isa_buf[j] >= min_prev_val)
                    {
                        if (pos_cnd == n)
                        {
                            pos_cnd = j;
                        }
                        else if (isa_buf[j] < isa_buf[pos_cnd])
                        {
                            pos_cnd = j;
                        }
                    }
                }
                if (pos_cnd == n)
                { // increasing sequence can not be extended
                    pos_cnd = pos_min;
                    ++runs;
                }
                min_prev_val = isa_buf[pos_cnd];
                marked_isa[pos_cnd] = 1;
                inv_perm[cnt++] = min_prev_val;
                marked_sa[min_prev_val] = 1;
            }
            m_marked_isa = std::move(t_bv_isa(marked_isa));
            util::init_support(m_select_marked_isa, &m_marked_isa);
            {
                rank_support_v<> rank_marked_sa(&marked_sa);
                for (size_type i = 0; i < inv_perm.size(); ++i)
                {
                    inv_perm[i] = rank_marked_sa(inv_perm[i]);
                }
            }
            util::bit_compress(inv_perm);

            m_marked_sa = std::move(t_bv_sa(marked_sa));
            util::init_support(m_rank_marked_sa, &m_marked_sa);

            std::string tmp_key =
                "fuzzy_isa_samples_" + util::to_string(util::pid()) + "_" + util::to_string(util::id());
            std::string tmp_file_name = cache_file_name(tmp_key, cconfig);
            store_to_file(inv_perm, tmp_file_name);
            construct(m_inv_perm, tmp_file_name, 0);
            sdsl::remove(tmp_file_name);
        }
    }

    //! Copy constructor
    _fuzzy_sa_sampling(_fuzzy_sa_sampling const & st) :
        m_marked_sa(st.m_marked_sa),
        m_rank_marked_sa(st.m_rank_marked_sa),
        m_marked_isa(st.m_marked_isa),
        m_select_marked_isa(st.m_select_marked_isa),
        m_inv_perm(st.m_inv_perm)
    {
        m_rank_marked_sa.set_vector(&m_marked_sa);
        m_select_marked_isa.set_vector(&m_marked_isa);
    }

    //! Move constructor
    _fuzzy_sa_sampling(_fuzzy_sa_sampling && st) :
        m_marked_sa(std::move(st.m_marked_sa)),
        m_rank_marked_sa(std::move(st.m_rank_marked_sa)),
        m_marked_isa(std::move(st.m_marked_isa)),
        m_select_marked_isa(std::move(st.m_select_marked_isa)),
        m_inv_perm(std::move(st.m_inv_perm))
    {
        m_rank_marked_sa.set_vector(&m_marked_sa);
        m_select_marked_isa.set_vector(&m_marked_isa);
    }

    //! Determine if index i is sampled or not
    inline bool is_sampled(size_type i) const
    {
        return m_marked_sa[i];
    }

    //! Return the suffix array value for the sampled index i
    inline value_type operator[](size_type i) const
    {
        return m_select_marked_isa(m_inv_perm.select(1, m_rank_marked_sa(i)) + 1);
    }

    //! Return the inv permutation at position i (already condensed!!!)
    inline value_type inv(size_type i) const
    {
        return m_inv_perm[i];
    }

    size_type size() const
    {
        return m_inv_perm.size();
    }

    //! Assignment operation
    _fuzzy_sa_sampling & operator=(_fuzzy_sa_sampling const & st)
    {
        if (this != &st)
        {
            _fuzzy_sa_sampling tmp(st);
            *this = std::move(tmp);
        }
        return *this;
    }

    //! Move assignment operation
    _fuzzy_sa_sampling & operator=(_fuzzy_sa_sampling && st)
    {
        m_marked_sa = std::move(st.m_marked_sa);
        m_rank_marked_sa = std::move(st.m_rank_marked_sa);
        m_marked_isa = std::move(st.m_marked_isa);
        m_select_marked_isa = std::move(st.m_select_marked_isa);
        m_inv_perm = std::move(st.m_inv_perm);
        m_rank_marked_sa.set_vector(&m_marked_sa);
        m_select_marked_isa.set_vector(&m_marked_isa);
        return *this;
    }

    size_type serialize(std::ostream & out, structure_tree_node * v = nullptr, std::string name = "") const
    {
        structure_tree_node * child = structure_tree::add_child(v, name, util::class_name(*this));
        size_type written_bytes = 0;
        written_bytes += m_marked_sa.serialize(out, child, "marked_sa");
        written_bytes += m_rank_marked_sa.serialize(out, child, "rank_marked_sa");
        written_bytes += m_marked_isa.serialize(out, child, "marked_isa");
        written_bytes += m_select_marked_isa.serialize(out, child, "select_marked_isa");
        written_bytes += m_inv_perm.serialize(out, child, "inv_perm");
        structure_tree::add_size(child, written_bytes);
        return written_bytes;
    }

    void load(std::istream & in)
    {
        m_marked_sa.load(in);
        m_rank_marked_sa.load(in);
        m_rank_marked_sa.set_vector(&m_marked_sa);
        m_marked_isa.load(in);
        m_select_marked_isa.load(in);
        m_select_marked_isa.set_vector(&m_marked_isa);
        m_inv_perm.load(in);
    }

    template <typename archive_t>
    void CEREAL_SAVE_FUNCTION_NAME(archive_t & ar) const
    {
        ar(CEREAL_NVP(m_marked_sa));
        ar(CEREAL_NVP(m_rank_marked_sa));
        ar(CEREAL_NVP(m_marked_isa));
        ar(CEREAL_NVP(m_select_marked_isa));
        ar(CEREAL_NVP(m_inv_perm));
    }

    template <typename archive_t>
    void CEREAL_LOAD_FUNCTION_NAME(archive_t & ar)
    {
        ar(CEREAL_NVP(m_marked_sa));
        ar(CEREAL_NVP(m_rank_marked_sa));
        m_rank_marked_sa.set_vector(&m_marked_sa);
        ar(CEREAL_NVP(m_marked_isa));
        ar(CEREAL_NVP(m_select_marked_isa));
        m_select_marked_isa.set_vector(&m_marked_isa);
        ar(CEREAL_NVP(m_inv_perm));
    }

    //! Equality operator.
    bool operator==(_fuzzy_sa_sampling const & other) const noexcept
    {
        return (m_marked_sa == other.m_marked_sa) && (m_rank_marked_sa == other.m_rank_marked_sa)
            && (m_marked_isa == other.m_marked_isa) && (m_select_marked_isa == other.m_select_marked_isa)
            && (m_inv_perm == other.m_inv_perm);
    }

    //! Inequality operator.
    bool operator!=(_fuzzy_sa_sampling const & other) const noexcept
    {
        return !(*this == other);
    }
};
template <class t_bv_sa = sd_vector<>,
          class t_bv_isa = sd_vector<>,
          class t_rank_sa = typename t_bv_sa::rank_1_type,
          class t_select_isa = typename t_bv_isa::select_1_type>
struct fuzzy_sa_sampling
{
    template <class t_csa>
    using type = _fuzzy_sa_sampling<t_csa, t_bv_sa, t_bv_isa, t_rank_sa, t_select_isa>;
    using sampling_category = sa_sampling_tag;
};

/*
 *       Text = ABCDEFABCDEF$
 *              0123456789012
 *       sa_sample_dens = 4
 *       sa_sample_chars = {B,E}
 *     SA BWT (1)
 *     12  F   * $
 *     06  F     ABCDEF$
 *     00  $   * ABCDEFABCDEF$
 *     07  A     BCDEF$
 *     01  A     BCDEFABCDEF$
 *     08  B   * CDEF$
 *     02  B   * CDEFABCDEF$
 *     09  C     DEF$
 *     03  C     DEFABCDEF$
 *     10  D     EF$
 *     04  D   * EFABCDEF$
 *     11  E   * F$
 *     05  E   * FABCDEF$
 *
 *    In this sampling a suffix x=SA[i] is marked if x \( 0 \equiv x \mod sa_sample_dens \) or
 *    BWT[i] is contained in sa_sample_chars.
 */

template <class t_csa, class t_bv = bit_vector, class t_rank = typename t_bv::rank_1_type, uint8_t t_width = 0>
class _bwt_sampling : public int_vector<t_width>
{
private:
    t_bv m_marked;
    t_rank m_rank_marked;

public:
    typedef int_vector<t_width> base_type;
    typedef typename base_type::size_type size_type;   // make typedefs of base_type visible
    typedef typename base_type::value_type value_type; //
    enum
    {
        sample_dens = t_csa::sa_sample_dens
    };
    enum
    {
        text_order = false
    };
    typedef sa_sampling_tag sampling_category;

    //! Default constructor
    _bwt_sampling()
    {}

    //! Constructor
    /*
     * \param cconfig Cache configuration (BWT,SA, and SAMPLE_CHARS are expected to be cached.).
     * \param csa    Pointer to the corresponding CSA. Not used in this class.
     * \par Time complexity
     *      Linear in the size of the suffix array.
     */
    _bwt_sampling(cache_config const & cconfig, SDSL_UNUSED t_csa const * csa = nullptr)
    {
        int_vector_buffer<> sa_buf(cache_file_name(conf::KEY_SA, cconfig));
        int_vector_buffer<t_csa::alphabet_type::int_width> bwt_buf(
            cache_file_name(key_bwt<t_csa::alphabet_type::int_width>(), cconfig));
        size_type n = sa_buf.size();
        bit_vector marked(n, 0); // temporary bitvector for the marked text positions
        this->width(bits::hi(n) + 1);
        int_vector<> sample_char;
        typedef typename t_csa::char_type char_type;
        std::set<char_type> char_map;
        if (load_from_cache(sample_char, conf::KEY_SAMPLE_CHAR, cconfig))
        {
            for (uint64_t i = 0; i < sample_char.size(); ++i)
            {
                char_map.insert((char_type)sample_char[i]);
            }
        }
        size_type sa_cnt = 0;
        for (size_type i = 0; i < n; ++i)
        {
            size_type sa = sa_buf[i];
            char_type bwt = bwt_buf[i];
            if (0 == (sa % sample_dens))
            {
                marked[i] = 1;
                ++sa_cnt;
            }
            else if (char_map.find(bwt) != char_map.end())
            {
                marked[i] = 1;
                ++sa_cnt;
            }
        }
        this->resize(sa_cnt);
        sa_cnt = 0;
        for (size_type i = 0; i < n; ++i)
        {
            size_type sa = sa_buf[i];
            if (marked[i])
            {
                base_type::operator[](sa_cnt++) = sa;
            }
        }
        m_marked = std::move(marked);
        util::init_support(m_rank_marked, &m_marked);
    }

    //! Copy constructor
    _bwt_sampling(_bwt_sampling const & st) : base_type(st)
    {
        m_marked = st.m_marked;
        m_rank_marked = st.m_rank_marked;
        m_rank_marked.set_vector(&m_marked);
    }

    //! Determine if index i is sampled or not
    inline bool is_sampled(size_type i) const
    {
        return m_marked[i];
    }

    //! Return the suffix array value for the sampled index i
    inline value_type operator[](size_type i) const
    {
        return base_type::operator[](m_rank_marked(i)) * sample_dens;
    }

    //! Assignment operation
    _bwt_sampling & operator=(_bwt_sampling const & st)
    {
        if (this != &st)
        {
            base_type::operator=(st);
            m_marked = st.m_marked;
            m_rank_marked = st.m_rank_marked;
            m_rank_marked.set_vector(&m_marked);
        }
        return *this;
    }

    //! Swap operation
    void swap(_bwt_sampling & st)
    {
        base_type::swap(st);
        m_marked.swap(st.m_marked);
        util::swap_support(m_rank_marked, st.m_rank_marked, &m_marked, &(st.m_marked));
    }

    size_type serialize(std::ostream & out, structure_tree_node * v = nullptr, std::string name = "") const
    {
        structure_tree_node * child = structure_tree::add_child(v, name, util::class_name(*this));
        size_type written_bytes = 0;
        written_bytes += base_type::serialize(out, child, "samples");
        written_bytes += m_marked.serialize(out, child, "marked");
        written_bytes += m_rank_marked.serialize(out, child, "rank_marked");
        structure_tree::add_size(child, written_bytes);
        return written_bytes;
    }

    void load(std::istream & in)
    {
        base_type::load(in);
        m_marked.load(in);
        m_rank_marked.load(in);
        m_rank_marked.set_vector(&m_marked);
    }

    template <typename archive_t>
    void CEREAL_SAVE_FUNCTION_NAME(archive_t & ar) const
    {
        base_type::CEREAL_SAVE_FUNCTION_NAME(ar);
        ar(CEREAL_NVP(m_marked));
        ar(CEREAL_NVP(m_rank_marked));
    }

    template <typename archive_t>
    void CEREAL_LOAD_FUNCTION_NAME(archive_t & ar)
    {
        base_type::CEREAL_LOAD_FUNCTION_NAME(ar);
        ar(CEREAL_NVP(m_marked));
        ar(CEREAL_NVP(m_rank_marked));
        m_rank_marked.set_vector(&m_marked);
    }
};

template <class t_bit_vec = bit_vector, class t_rank_sup = typename t_bit_vec::rank_1_type, uint8_t t_width = 0>
struct sa_bwt_sampling
{
    template <class t_csa>
    using type = _bwt_sampling<t_csa, t_bit_vec, t_rank_sup, t_width>;
    using sampling_category = sa_sampling_tag;
};

template <class t_csa, uint8_t t_width = 0>
class _isa_sampling : public int_vector<t_width>
{
public:
    typedef int_vector<t_width> base_type;
    typedef typename base_type::size_type size_type;   // make typedefs of base_type visible
    typedef typename base_type::value_type value_type; //
    typedef typename t_csa::sa_sample_type sa_type;    // sa sample type
    enum
    {
        sample_dens = t_csa::isa_sample_dens
    };
    typedef isa_sampling_tag sampling_category;

    //! Default constructor
    _isa_sampling()
    {}

    //! Constructor
    /*
     * \param cconfig   Cache configuration (SA is expected to be cached.).
     * \param sa_sample Pointer to the corresponding SA sampling. Not used in this class.
     * \par Time complexity
     *      Linear in the size of the suffix array.
     */
    _isa_sampling(cache_config const & cconfig, SDSL_UNUSED sa_type const * sa_sample = nullptr)
    {
        int_vector_buffer<> sa_buf(cache_file_name(conf::KEY_SA, cconfig));
        size_type n = sa_buf.size();
        if (n >= 1)
        { // so n+t_csa::isa_sample_dens >= 2
            this->width(bits::hi(n) + 1);
            this->resize((n - 1) / sample_dens + 1);
        }
        for (size_type i = 0; i < this->size(); ++i)
            base_type::operator[](i) = 0;

        for (size_type i = 0; i < n; ++i)
        {
            size_type sa = sa_buf[i];
            if ((sa % sample_dens) == 0)
            {
                base_type::operator[](sa / sample_dens) = i;
            }
        }
    }

    //! Returns the ISA value at position j, where
    inline value_type operator[](size_type i) const
    {
        return base_type::operator[](i / sample_dens);
    }

    //! Returns the rightmost ISA sample <= i and its position
    inline std::tuple<value_type, size_type> sample_leq(size_type i) const
    {
        size_type ci = i / sample_dens;
        return std::make_tuple(base_type::operator[](ci), ci * sample_dens);
    }

    //! Returns the leftmost ISA sample >= i and its position
    inline std::tuple<value_type, size_type> sample_qeq(size_type i) const
    {
        size_type ci = (i / sample_dens + 1) % this->size();
        return std::make_tuple(base_type::operator[](ci), ci * sample_dens);
    }

    //! Load sampling from disk
    void load(std::istream & in, SDSL_UNUSED sa_type const * sa_sample = nullptr)
    {
        base_type::load(in);
    }

    template <typename archive_t>
    void CEREAL_SAVE_FUNCTION_NAME(archive_t & ar) const
    {
        base_type::CEREAL_SAVE_FUNCTION_NAME(ar);
    }

    template <typename archive_t>
    void CEREAL_LOAD_FUNCTION_NAME(archive_t & ar)
    {
        base_type::CEREAL_LOAD_FUNCTION_NAME(ar);
    }

    void set_vector(SDSL_UNUSED sa_type const *)
    {}
};

template <uint8_t t_width = 0>
struct isa_sampling
{
    template <class t_csa>
    using type = _isa_sampling<t_csa, t_width>;
    using sampling_category = isa_sampling_tag;
};

template <class t_csa, class t_inv_perm, class t_sel>
class _text_order_isa_sampling_support
{
    static_assert(t_csa::sa_sample_dens == t_csa::isa_sample_dens,
                  "ISA sampling requires: sa_sample_dens == isa_sample_dens");

public:
    typedef typename bit_vector::size_type size_type;
    typedef typename bit_vector::value_type value_type;
    typedef typename t_csa::sa_sample_type sa_type; // sa sample type
    typedef typename sa_type::bv_type bv_type;      // bitvector type used to mark SA samples
    enum
    {
        sample_dens = t_csa::isa_sample_dens
    };
    typedef isa_sampling_tag sampling_category;

private:
    t_sel m_select_marked;
    t_inv_perm m_inv_perm;

public:
    t_sel const & select_marked = m_select_marked;

    //! Default constructor
    _text_order_isa_sampling_support()
    {}

    //! Constructor
    /*
     * \param cconfig   Cache configuration. (Not used in this class)
     * \param sa_sample Pointer to the corresponding SA sampling..
     * \par Time complexity
     *      Linear in the size of the suffix array.
     */
    _text_order_isa_sampling_support(SDSL_UNUSED cache_config const & cconfig,
                                     const typename std::enable_if<sa_type::text_order, sa_type *>::type sa_sample)
    {
        // and initialize the select support on bitvector marked
        m_select_marked = t_sel(&(sa_sample->marked));
        int_vector<> const * perm = (int_vector<> const *)sa_sample;
        m_inv_perm = t_inv_perm(perm);
        m_inv_perm.set_vector(perm);
    }

    //! Copy constructor
    _text_order_isa_sampling_support(_text_order_isa_sampling_support const & st)
    {
        m_inv_perm = st.m_inv_perm;
        m_select_marked = st.m_select_marked;
    }

    //! Return the inverse suffix array value for the sampled index i
    inline value_type operator[](size_type i) const
    {
        return m_select_marked(m_inv_perm[i / sample_dens] + 1);
    }

    //! Returns the rightmost ISA sample <= i and its position
    inline std::tuple<value_type, size_type> sample_leq(size_type i) const
    {
        size_type ci = i / sample_dens;
        return std::make_tuple(m_select_marked(m_inv_perm[ci] + 1), ci * sample_dens);
    }

    //! Returns the leftmost ISA sample >= i and its position
    inline std::tuple<value_type, size_type> sample_qeq(size_type i) const
    {
        size_type ci = (i / sample_dens + 1) % m_inv_perm.size();
        return std::make_tuple(m_select_marked(m_inv_perm[ci] + 1), ci * sample_dens);
    }

    //! Assignment operation
    _text_order_isa_sampling_support & operator=(_text_order_isa_sampling_support const & st)
    {
        if (this != &st)
        {
            m_inv_perm = st.m_inv_perm;
            m_select_marked = st.m_select_marked;
        }
        return *this;
    }

    //! Swap operation
    void swap(_text_order_isa_sampling_support & st)
    {
        if (this != &st)
        {
            m_inv_perm.swap(st.m_inv_perm);
            m_select_marked.swap(st.m_select_marked);
        }
    }

    size_type serialize(std::ostream & out, structure_tree_node * v = nullptr, std::string name = "") const
    {
        structure_tree_node * child = structure_tree::add_child(v, name, util::class_name(*this));
        size_type written_bytes = 0;
        written_bytes += m_inv_perm.serialize(out, child, "inv_perm");
        written_bytes += m_select_marked.serialize(out, child, "select_marked");
        structure_tree::add_size(child, written_bytes);
        return written_bytes;
    }

    //! Load sampling from disk
    void load(std::istream & in, sa_type const * sa_sample = nullptr)
    {
        m_inv_perm.load(in);
        m_select_marked.load(in);
        set_vector(sa_sample);
    }

    template <typename archive_t>
    void CEREAL_SAVE_FUNCTION_NAME(archive_t & ar) const
    {
        ar(CEREAL_NVP(m_inv_perm));
        ar(CEREAL_NVP(m_select_marked));
    }

    template <typename archive_t>
    void CEREAL_LOAD_FUNCTION_NAME(archive_t & ar, sa_type const * sa_sample = nullptr)
    {
        ar(CEREAL_NVP(m_inv_perm));
        ar(CEREAL_NVP(m_select_marked));
        set_vector(sa_sample);
    }

    //! Equality operator.
    bool operator==(_text_order_isa_sampling_support const & other) const noexcept
    {
        return (m_inv_perm == other.m_inv_perm) && (m_select_marked == other.m_select_marked);
    }

    //! Inequality operator.
    bool operator!=(_text_order_isa_sampling_support const & other) const noexcept
    {
        return !(*this == other);
    }

    void set_vector(sa_type const * sa_sample = nullptr)
    {
        if (sa_sample == nullptr)
        {
            m_select_marked.set_vector(nullptr);
            m_inv_perm.set_vector(nullptr);
        }
        else
        {
            m_select_marked.set_vector(&(sa_sample->marked));
            m_inv_perm.set_vector((int_vector<> const *)sa_sample);
        }
    }
};

template <class t_inv_perm = inv_perm_support<8>, class t_sel = void>
struct text_order_isa_sampling_support
{
    template <class t_csa>
    using type = _text_order_isa_sampling_support<
        t_csa,
        t_inv_perm,
        typename std::conditional<std::is_void<t_sel>::value,
                                  typename t_csa::sa_sample_type::bv_type::select_1_type,
                                  t_sel>::type>;
    using sampling_category = isa_sampling_tag;
};

template <class t_csa, class t_select_sa>
class _fuzzy_isa_sampling_support
{
    static_assert(t_csa::sa_sample_dens == t_csa::isa_sample_dens,
                  "ISA sampling requires: sa_sample_dens==isa_sample_dens");

public:
    typedef typename bit_vector::size_type size_type;
    typedef typename bit_vector::value_type value_type;
    typedef typename t_csa::sa_sample_type sa_type; // sa sample type
    enum
    {
        sample_dens = t_csa::isa_sample_dens
    };
    typedef isa_sampling_tag sampling_category;

private:
    sa_type const * m_sa_p = nullptr; // pointer to sa_sample_strategy
    t_select_sa m_select_marked_sa;

public:
    //! Default constructor
    _fuzzy_isa_sampling_support()
    {}

    //! Constructor
    /*
     * \param cconfig   Cache configuration. (Not used in this class)
     * \param sa_sample Pointer to the corresponding SA sampling..
     * \par Time complexity
     *      Linear in the size of the suffix array.
     */
    _fuzzy_isa_sampling_support(SDSL_UNUSED cache_config const & cconfig, sa_type const * sa_sample) : m_sa_p(sa_sample)
    {
        util::init_support(m_select_marked_sa, &(sa_sample->marked_sa));
    }

    //! Copy constructor
    _fuzzy_isa_sampling_support(_fuzzy_isa_sampling_support const & st) : m_select_marked_sa(st.m_select_marked_sa)
    {
        set_vector(st.m_sa_p);
    }

    //! Return the inverse suffix array value for the sampled index i
    inline value_type operator[](size_type i) const
    {
        return m_sa_p->inv(i);
    }

    //! Returns the rightmost ISA sample <= i and its position
    inline std::tuple<value_type, size_type> sample_leq(size_type i) const
    {
        size_type ci = i / sample_dens;
        size_type j = m_sa_p->select_marked_isa(ci + 1);
        if (j > i)
        {
            if (ci > 0)
            {
                ci = ci - 1;
            }
            else
            {
                ci = m_sa_p->size() - 1;
            }
            j = m_sa_p->select_marked_isa(ci + 1);
        }
        return std::make_tuple(m_select_marked_sa(m_sa_p->inv(ci) + 1), j);
    }

    //! Returns the leftmost ISA sample >= i and its position
    inline std::tuple<value_type, size_type> sample_qeq(size_type i) const
    {
        size_type ci = i / sample_dens;
        size_type j = m_sa_p->select_marked_isa(ci + 1);
        if (j < i)
        {
            if (ci < m_sa_p->size() - 1)
            {
                ci = ci + 1;
            }
            else
            {
                ci = 0;
            }
            j = m_sa_p->select_marked_isa(ci + 1);
        }
        return std::make_tuple(m_select_marked_sa(m_sa_p->inv(ci) + 1), j);
    }

    //! Assignment operation
    _fuzzy_isa_sampling_support & operator=(_fuzzy_isa_sampling_support const & st)
    {
        if (this != &st)
        {
            m_select_marked_sa = st.m_select_marked_sa;
            set_vector(st.m_sa_p);
        }
        return *this;
    }

    //! Swap operation
    void swap(_fuzzy_isa_sampling_support & st)
    {
        m_select_marked_sa.swap(st.m_select_marked_sa);
    }

    size_type serialize(std::ostream & out, structure_tree_node * v = nullptr, std::string name = "") const
    {
        structure_tree_node * child = structure_tree::add_child(v, name, util::class_name(*this));
        size_type written_bytes = 0;
        written_bytes += m_select_marked_sa.serialize(out, child, "select_marked_sa");
        structure_tree::add_size(child, written_bytes);
        return written_bytes;
    }

    //! Load sampling from disk
    void load(std::istream & in, sa_type const * sa_sample = nullptr)
    {
        m_select_marked_sa.load(in);
        set_vector(sa_sample);
    }

    template <typename archive_t>
    void CEREAL_SAVE_FUNCTION_NAME(archive_t & ar) const
    {
        ar(CEREAL_NVP(m_select_marked_sa));
    }

    template <typename archive_t>
    void CEREAL_LOAD_FUNCTION_NAME(archive_t & ar, sa_type const * sa_sample = nullptr)
    {
        ar(CEREAL_NVP(m_select_marked_sa));
        set_vector(sa_sample);
    }

    //! Equality operator.
    bool operator==(_fuzzy_isa_sampling_support const & other) const noexcept
    {
        return (m_select_marked_sa == other.m_select_marked_sa);
    }

    //! Inequality operator.
    bool operator!=(_fuzzy_isa_sampling_support const & other) const noexcept
    {
        return !(*this == other);
    }

    void set_vector(sa_type const * sa_sample = nullptr)
    {
        m_sa_p = sa_sample;
        if (nullptr != m_sa_p)
        {
            m_select_marked_sa.set_vector(&(sa_sample->marked_sa));
        }
    }
};

template <class t_select_sa = void>
struct fuzzy_isa_sampling_support
{
    template <class t_csa>
    using type =
        _fuzzy_isa_sampling_support<t_csa,
                                    typename std::conditional<std::is_void<t_select_sa>::value,
                                                              typename t_csa::sa_sample_type::bv_sa_type::select_1_type,
                                                              t_select_sa>::type>;
    using sampling_category = isa_sampling_tag;
};

} // namespace sdsl

#endif
