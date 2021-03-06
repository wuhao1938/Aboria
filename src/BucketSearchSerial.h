/*

Copyright (c) 2005-2016, University of Oxford.
All rights reserved.

University of Oxford means the Chancellor, Masters and Scholars of the
University of Oxford, having an administrative office at Wellington
Square, Oxford OX1 2JD, UK.

This file is part of Aboria.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:
 * Redistributions of source code must retain the above copyright notice,
   this list of conditions and the following disclaimer.
 * Redistributions in binary form must reproduce the above copyright notice,
   this list of conditions and the following disclaimer in the documentation
   and/or other materials provided with the distribution.
 * Neither the name of the University of Oxford nor the names of its
   contributors may be used to endorse or promote products derived from this
   software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

*/


#ifndef BUCKETSEARCH_SERIAL_H_
#define BUCKETSEARCH_SERIAL_H_

#include <boost/iterator/iterator_facade.hpp>
#include "Traits.h"
#include "Vector.h"
#include "Log.h"
#include "Get.h"
#include "NeighbourSearchBase.h"
#include "detail/SpatialUtil.h"
#include <vector>
#include <iostream>
#include <set>


namespace Aboria {

template <typename Traits>
struct bucket_search_serial_params {
    typedef typename Traits::double_d double_d;
    bucket_search_serial_params(): 
        side_length(detail::get_max<double>()) {}
    bucket_search_serial_params(const double_d& side_length):
        side_length(side_length) {}
    double_d side_length;
};

template <typename Traits>
class bucket_search_serial_query; 

/// \brief Implements neighbourhood searching using a bucket search algorithm, dividing
/// the domain into constant size "buckets".
///
/// This class implements neighbourhood searching using a bucket search algorithm. The 
/// domain is first divided up into a regular grid of constant size "buckets", either by
/// using the class constructor to initialise the domain extents, bucket size etc., or by 
/// using the reset() member function to reset these parameters.
///
/// After the buckets are created, a set of 3D points can be assigned to their respective buckets
/// using the embed_points() member function. After this, neighbourhood queries around
/// a given point can be performed using find_broadphase_neighbours(), which returns a const 
/// iterator to all the points in the same bucket or surrounding buckets of the given point.
///
template <typename Traits>
class bucket_search_serial: 
    public neighbour_search_base<bucket_search_serial<Traits>,
                                 Traits,
                                 bucket_search_serial_params<Traits>,
                                 linked_list_iterator<Traits>,
                                 bucket_search_serial_query<Traits>> {

    typedef typename Traits::double_d double_d;
    typedef typename Traits::position position;
    typedef typename Traits::vector_int vector_int;
    typedef typename Traits::iterator iterator;
    typedef typename Traits::unsigned_int_d unsigned_int_d;

    typedef bucket_search_serial_params<Traits> params_type;

    friend neighbour_search_base<bucket_search_serial<Traits>,
                                 Traits,
                                 bucket_search_serial_params<Traits>,
                                 linked_list_iterator<Traits>,
                                 bucket_search_serial_query<Traits>>;


public:
    static constexpr bool unordered() {
        return true;
    }

private:
    void set_domain_impl() {
        m_size = 
            floor((this->m_bounds.bmax-this->m_bounds.bmin)/this->m_bucket_side_length)
            .template cast<unsigned int>();
        for (int i=0; i<Traits::dimension; ++i) {
            if (m_size[i] == 0) {
                m_size[i] = this->m_bounds.bmax[i]-this->m_bounds.bmin[i];
            }
        }
        this->m_bucket_side_length = (this->m_bounds.bmax-this->m_bounds.bmin)/m_size;
        m_point_to_bucket_index = 
            detail::point_to_bucket_index<Traits::dimension>(m_size,this->m_bucket_side_length,this->m_bounds);
 
	    LOG(2,"\tnumber of buckets = "<<m_size<<" (total="<<m_size.prod()<<")");

        m_buckets.assign(m_size.prod(), detail::get_empty_id());
        //TODO: should always be true?
        m_use_dirty_cells = true;

        this->m_query.m_buckets_begin = iterator_to_raw_pointer(m_buckets.begin());

        this->m_query.m_bucket_side_length = this->m_bucket_side_length;
        this->m_query.m_bounds.bmin = this->m_bounds.bmin;
        this->m_query.m_bounds.bmax = this->m_bounds.bmax;
        this->m_query.m_periodic = this->m_periodic;
        this->m_query.m_end_bucket = m_size-1;
        this->m_query.m_point_to_bucket_index = m_point_to_bucket_index;
    }

    void check_data_structure() {
        int num_particles = 0;
        for (int i=0; i<m_buckets.size(); i++) {
            int j = m_buckets[i];
            int old_j = detail::get_empty_id();
            while (j != detail::get_empty_id()) {
                ASSERT(m_linked_list_reverse[j] == old_j, "m_linked_list_reverse not right: m_linked_list_reverse[j] = "<<m_linked_list_reverse[j]<<", j = "<<j<<", old_j = "<<old_j); 
                ASSERT(m_dirty_buckets[j] == i, "m_dirty_buckets not right: m_dirty_buckets[j] = "<<m_dirty_buckets[j]<<", i = "<<i<<", j = "<<j); 
                ASSERT(j < m_linked_list.size(), "j index too large");
                ASSERT(j >= 0, "j index less than zero");
                num_particles++;
                old_j = j;
                j = m_linked_list[old_j];
            }
        }
        ASSERT(num_particles == m_linked_list.size(), "m_linked_list size inconsistent");
        ASSERT(num_particles == m_linked_list_reverse.size(), "m_linked_list_reverse size inconsistent");
        ASSERT(num_particles == m_dirty_buckets.size(), "m_dirty_buckets size inconsistent");
        for (int i=0; i<m_linked_list.size(); ++i) {
            ASSERT(i < m_linked_list.size(), "i index too large");
            ASSERT(i >= 0, "i index less than zero");
            ASSERT(m_dirty_buckets[i] < m_buckets.size(), "m_dirty_buckets not right");
            ASSERT(m_dirty_buckets[i] >= 0, "m_dirty_buckets not right");
            ASSERT(m_linked_list[i] < m_linked_list.size() || m_linked_list[i] == detail::get_empty_id(), "m_linked_list not right: m_linked_list[i] = "<<m_linked_list[i]<<", m_linked_list.size() = "<<m_linked_list.size());
            ASSERT(m_linked_list[i] >= 0 || m_linked_list[i] == detail::get_empty_id(), "m_linked_list not right");
            ASSERT(m_linked_list_reverse[i] < m_linked_list_reverse.size() || m_linked_list_reverse[i] == detail::get_empty_id(), "m_linked_list_reverse not right");
            ASSERT(m_linked_list_reverse[i] >= 0 || m_linked_list_reverse[i] == detail::get_empty_id(), "m_linked_list_reverse not right");
        }
    }



    void update_iterator_impl() {
        this->m_query.m_particles_begin = iterator_to_raw_pointer(this->m_particles_begin);
        //check_data_structure();
    }


    void embed_points_impl() {
        const size_t n = this->m_particles_end - this->m_particles_begin;

        /*
         * clear head of linked lists (m_buckets)
         */
        if (m_use_dirty_cells) {
            //TODO: wont ever be true??
            if (m_dirty_buckets.size()<m_buckets.size()) {
                for (int i: m_dirty_buckets) {
                    m_buckets[i] = detail::get_empty_id();
                }
            } else {
                m_buckets.assign(m_buckets.size(), detail::get_empty_id());
            }
        }
        m_use_dirty_cells = true;

        m_linked_list.assign(n, detail::get_empty_id());
        m_linked_list_reverse.assign(n, detail::get_empty_id());
        m_dirty_buckets.assign(n,detail::get_empty_id());
        for (size_t i=0; i<n; ++i) {
            const double_d& r = get<position>(this->m_particles_begin)[i];
            const unsigned int bucketi = m_point_to_bucket_index.find_bucket_index(r);
            ASSERT(bucketi < m_buckets.size() && bucketi >= 0, "bucket index out of range");
            const int bucket_entry = m_buckets[bucketi];

            // Insert into own bucket
            m_buckets[bucketi] = i;
            m_dirty_buckets[i] = bucketi;
            m_linked_list[i] = bucket_entry;
            m_linked_list_reverse[i] = detail::get_empty_id();
            if (bucket_entry != detail::get_empty_id()) m_linked_list_reverse[bucket_entry] = i;
        }

#ifndef __CUDA_ARCH__
        if (4 <= ABORIA_LOG_LEVEL) { 
            LOG(4,"\tbuckets:");
            for (int i = 0; i<m_buckets.size(); ++i) {
                if (m_buckets[i] != detail::get_empty_id()) {
                    LOG(4,"\ti = "<<i<<" bucket contents = "<<m_buckets[i]);
                }
            }
            LOG(4,"\tend buckets");
            LOG(4,"\tlinked list:");
            for (int i = 0; i<m_linked_list.size(); ++i) {
                LOG(4,"\ti = "<<i<<" p = "<<get<position>(*(this->m_particles_begin+i))<<" contents = "<<m_linked_list[i]<<". reverse = "<<m_linked_list_reverse[i]);
            }
            LOG(4,"\tend linked list:");
        }
#endif

        //check_data_structure();

        this->m_query.m_particles_begin = iterator_to_raw_pointer(this->m_particles_begin);
        this->m_query.m_linked_list_begin = iterator_to_raw_pointer(this->m_linked_list.begin());
    }


    void add_points_at_end_impl(const size_t dist) {
        const size_t n = this->m_particles_end - this->m_particles_begin;
        const size_t start_adding = n-dist;
        ASSERT(m_linked_list.size() == start_adding, "m_linked_list not consistent with dist");
        ASSERT(m_linked_list_reverse.size() == start_adding, "m_linked_list_reverse not consistent with dist");
        ASSERT(m_dirty_buckets.size() == start_adding, "m_dirty_buckets not consistent with dist");
        m_linked_list.resize(n,detail::get_empty_id());
        m_linked_list_reverse.resize(n,detail::get_empty_id());
        m_dirty_buckets.resize(n,detail::get_empty_id());

        for (size_t i = start_adding; i<n; ++i) {
            const double_d& r = get<position>(this->m_particles_begin)[i];
            const unsigned int bucketi = m_point_to_bucket_index.find_bucket_index(r);
            ASSERT(bucketi < m_buckets.size() && bucketi >= 0, "bucket index out of range");
            const int bucket_entry = m_buckets[bucketi];

            // Insert into own cell
            m_buckets[bucketi] = i;
            m_dirty_buckets[i] = bucketi;
            m_linked_list[i] = bucket_entry;
            m_linked_list_reverse[i] = detail::get_empty_id();
            if (bucket_entry != detail::get_empty_id()) m_linked_list_reverse[bucket_entry] = i;
        }

#ifndef __CUDA_ARCH__
        if (4 <= ABORIA_LOG_LEVEL) { 
            LOG(4,"\tbuckets:");
            for (int i = 0; i<m_buckets.size(); ++i) {
                if (m_buckets[i] != detail::get_empty_id()) {
                    LOG(4,"\ti = "<<i<<" bucket contents = "<<m_buckets[i]);
                }
            }
            LOG(4,"\tend buckets");
            for (int i = 0; i<m_linked_list.size(); ++i) {
                LOG(4,"\ti = "<<i<<" p = "<<get<position>(*(this->m_particles_begin+i))<<" contents = "<<m_linked_list[i]<<". reverse = "<<m_linked_list_reverse[i]);
            }
        }
#endif

        //check_data_structure();

        this->m_query.m_particles_begin = iterator_to_raw_pointer(this->m_particles_begin);
        this->m_query.m_linked_list_begin = iterator_to_raw_pointer(this->m_linked_list.begin());
    }

    void delete_points_at_end_impl(const size_t dist) {
        const size_t n = this->m_particles_end - this->m_particles_begin;
        ASSERT(m_linked_list.size()-n == dist, "m_linked_list not consistent with dist");
        ASSERT(m_linked_list_reverse.size()-n == dist, "m_linked_list_reverse not consistent with dist");
        ASSERT(m_dirty_buckets.size()-n == dist, "m_dirty_buckets not consistent with dist");
        const size_t oldn = m_linked_list.size();
        for (size_t i = n; i<oldn; ++i) {
            if (m_dirty_buckets[i] == detail::get_empty_id()) continue;
            const int celli = m_dirty_buckets[i];

            //get first backwards index < n
            int backwardsi = i;
            while (backwardsi >= n) {
                backwardsi = m_linked_list_reverse[backwardsi];
                if (backwardsi == detail::get_empty_id()) break;
            }

            //get first forward index < n
            int forwardi = i;
            while (forwardi >= n) {
                forwardi = m_linked_list[forwardi];
                if (forwardi == detail::get_empty_id()) break;
            }

            if (forwardi != detail::get_empty_id()) {
                m_linked_list_reverse[forwardi] = backwardsi;
            }
            if (backwardsi != detail::get_empty_id()) {
                m_linked_list[backwardsi] = forwardi;
            } else if (forwardi != detail::get_empty_id()) {
                m_buckets[celli] = forwardi;
            } else {
                m_buckets[celli] = detail::get_empty_id();
            }
        }
        m_linked_list.resize(n);
        m_linked_list_reverse.resize(n);
        m_dirty_buckets.resize(n);


        //check_data_structure();

        this->m_query.m_particles_begin = iterator_to_raw_pointer(this->m_particles_begin);
        this->m_query.m_linked_list_begin = iterator_to_raw_pointer(this->m_linked_list.begin());
    }

    void update_point(iterator update_iterator) {
        const size_t i = std::distance(this->m_particles_begin,update_iterator);
        const bool particle_based = true;

        const int forwardi = m_linked_list[i];
        const int backwardsi = m_linked_list_reverse[i];

        if (forwardi != detail::get_empty_id()) {
            m_linked_list_reverse[forwardi] = backwardsi;
        }
        if (backwardsi != detail::get_empty_id()) {
            m_linked_list[backwardsi] = forwardi;
        } else {
            const int celli = m_dirty_buckets[i];
            ASSERT(m_buckets[celli]==i,"inconsistant m_buckets data structures!");
            m_buckets[celli] = forwardi;
        }

        const int celli = find_cell_index(return_vect3d(*update_iterator));
        const int cell_entry = m_buckets[celli];

        // Insert into own cell
        m_buckets[celli] = i;
        m_dirty_buckets[i] = celli;
        m_linked_list[i] = cell_entry;
        m_linked_list_reverse[i] = detail::get_empty_id();
        if (cell_entry != detail::get_empty_id()) m_linked_list_reverse[cell_entry] = i;

    }

    void untrack_point(const size_t i) {
        ASSERT((i>=0) && (i<m_linked_list.size()),"invalid untrack index");

        const int forwardi = m_linked_list[i];
        const int backwardsi = m_linked_list_reverse[i];

        ASSERT(forwardi == detail::get_empty_id() ||
                ((forwardi>=0) && (forwardi<m_linked_list_reverse.size())),"invalid untrack index (forwards)");
        ASSERT(backwardsi == detail::get_empty_id() ||
                ((backwardsi>=0) && (backwardsi<m_linked_list.size())),"invalid untrack index (backwards)");

        if (forwardi != detail::get_empty_id()) {
            m_linked_list_reverse[forwardi] = backwardsi;
        }
        if (backwardsi != detail::get_empty_id()) {
            m_linked_list[backwardsi] = forwardi;
        } else {
            const int celli = m_dirty_buckets[i];
            ASSERT(m_buckets[celli]==i,"inconsistant m_buckets data structures!");
            m_buckets[celli] = forwardi;
        }
    }


    void copy_points_impl(iterator copy_from_iterator, iterator copy_to_iterator) {
        const size_t toi = std::distance(this->m_particles_begin,copy_to_iterator);
        const size_t fromi = std::distance(this->m_particles_begin,copy_from_iterator);
        ASSERT(toi != fromi,"toi and fromi are the same");

        // unlink old toi pointers
        const int toi_back = m_linked_list_reverse[toi];
        const int toi_forward = m_linked_list[toi];
        ASSERT(toi_back == detail::get_empty_id() ||
            (toi_back < m_linked_list_reverse.size() && toi_back >= 0),
            "invalid index of "<<toi_back <<". Note linked list reverse size is "
            << m_linked_list_reverse.size());
        ASSERT(toi_forward == detail::get_empty_id() ||
            (toi_forward < m_linked_list_reverse.size() && toi_forward >= 0),
            "invalid index of "<<toi_back <<". Note linked list reverse size is "
            << m_linked_list_reverse.size());
        if (toi_back != detail::get_empty_id()) {
            m_linked_list[toi_back] = toi_forward;
        } else {
            m_buckets[m_dirty_buckets[toi]] = toi_forward;
        }
        if (toi_forward != detail::get_empty_id()) {
            m_linked_list_reverse[toi_forward] = toi_back;
        }

        // setup fromi <-> toi 
        const int forwardi = m_linked_list[fromi];
        const int bucketi = m_dirty_buckets[fromi];
        ASSERT(toi < m_linked_list.size() && toi >= 0,"invalid index");
        ASSERT(bucketi < m_buckets.size() && bucketi >= 0,"invalid index");
        ASSERT(fromi < m_linked_list_reverse.size() && fromi >= 0,"invalid index");
        ASSERT(forwardi == detail::get_empty_id() ||
            (forwardi < m_linked_list_reverse.size() && forwardi>= 0),
            "invalid index of "<<forwardi<<". Note linked list reverse size is "
            << m_linked_list_reverse.size());

        m_linked_list[toi] = forwardi;
        m_linked_list_reverse[toi] = fromi;
        m_linked_list[fromi] = toi;
        if (forwardi != detail::get_empty_id()) { //check this
            m_linked_list_reverse[forwardi] = toi; 
        }
        m_dirty_buckets[toi] = bucketi;

        //check_data_structure();
    }

    const bucket_search_serial_query<Traits>& get_query_impl() const {
        return m_query;
    }

    vector_int m_buckets;
    vector_int m_linked_list;
    vector_int m_linked_list_reverse;
    vector_int m_dirty_buckets;
    bucket_search_serial_query<Traits> m_query;
    bool m_use_dirty_cells;

    unsigned_int_d m_size;
    detail::point_to_bucket_index<Traits::dimension> m_point_to_bucket_index;

};

// assume that query functions, are only called from device code
template <typename Traits>
struct bucket_search_serial_query {

    typedef Traits traits_type;
    typedef typename Traits::raw_pointer raw_pointer;
    typedef typename Traits::double_d double_d;
    typedef typename Traits::bool_d bool_d;
    typedef typename Traits::int_d int_d;
    typedef typename Traits::unsigned_int_d unsigned_int_d;
    typedef typename Traits::reference reference;
    typedef typename Traits::position position;
    const static unsigned int dimension = Traits::dimension;
    typedef lattice_iterator<dimension> bucket_iterator;
    typedef typename bucket_iterator::reference bucket_reference;
    typedef typename bucket_iterator::value_type bucket_value_type;
    typedef linked_list_iterator<Traits> particle_iterator;

    bool_d m_periodic;
    double_d m_bucket_side_length; 
    int_d m_end_bucket;
    detail::bbox<dimension> m_bounds;
    detail::point_to_bucket_index<dimension> m_point_to_bucket_index;

    raw_pointer m_particles_begin;
    int *m_buckets_begin;
    int *m_linked_list_begin;

    inline
    CUDA_HOST_DEVICE
    bucket_search_serial_query():
        m_periodic(),
        m_particles_begin(),
        m_buckets_begin()
    {}

    const double_d& get_min_bucket_size() const { return m_bucket_side_length; }

    CUDA_HOST_DEVICE
    iterator_range_with_transpose<particle_iterator> get_bucket_particles(const bucket_reference &bucket) const {
        // handle end cases
        unsigned_int_d my_bucket(bucket);
        double_d transpose(0);
        bool outside = false;
        for (int i=0; i<Traits::dimension; i++) {
            if (bucket[i] < 0) {
                if (m_periodic[i]) {
                    my_bucket[i] = m_end_bucket[i];
                    transpose[i] = -(m_bounds.bmax-m_bounds.bmin)[i];
                } else {
                    outside = true;
                    break;
                }
            }
            if (bucket[i] > m_end_bucket[i]) {
                if (m_periodic[i]) {
                    my_bucket[i] = 0;
                    transpose[i] = (m_bounds.bmax-m_bounds.bmin)[i];
                } else {
                    outside = true;
                    break;
                }
            }
        }
        if (!outside) {
                const unsigned int bucket_index = m_point_to_bucket_index.collapse_index_vector(my_bucket);

#ifndef __CUDA_ARCH__
                LOG(4,"\tget_bucket_particles: looking in bucket "<<bucket<<" = "<<bucket_index);
#endif
                return iterator_range_with_transpose<particle_iterator>(
                        particle_iterator(m_buckets_begin[bucket_index],
                                m_particles_begin,
                                m_linked_list_begin),
                        particle_iterator(),
                        transpose);
        } else {
                return iterator_range_with_transpose<particle_iterator>(
                        particle_iterator(),
                        particle_iterator());
        }
        
    }

    CUDA_HOST_DEVICE
    detail::bbox<dimension>& get_bucket_bbox(const bucket_reference &bucket) const {
        return detail::bbox<dimension>((*bucket)*m_bucket_side_length + m_bounds.bmin);
    }

    CUDA_HOST_DEVICE
    bucket_value_type get_bucket(const double_d &position) const {
        return m_point_to_bucket_index.find_bucket_index_vector(position);
    }

    CUDA_HOST_DEVICE
    iterator_range<bucket_iterator> get_near_buckets(const bucket_reference &bucket) const {
        int_d start,end;
        for (int i=0; i<Traits::dimension; i++) {
            if (m_periodic[i]) {
                start[i] = bucket[i]-1;
                end[i] = bucket[i]+1;
            } else {
                if (bucket[i] > 0) {
                    start[i] = bucket[i]-1;
                } else {
                    start[i] = bucket[i];
                }
                if (bucket[i] < m_end_bucket[i]) {
                    end[i] = bucket[i]+1;
                } else {
                    end[i] = bucket[i];
                }
            }
        }
#ifndef __CUDA_ARCH__
        LOG(4,"\tget_near_buckets: looking in bucket "<<bucket<<". start = "<<start<<" end = "<<end);
#endif
 
        return iterator_range<bucket_iterator>(
                bucket_iterator(start,end,start)
                ,++bucket_iterator(start,end,end)
                );
    }

    CUDA_HOST_DEVICE
    bucket_iterator begin() const {
        return bucket_iterator(int_d(0),m_end_bucket,int_d(0));
    }
    CUDA_HOST_DEVICE
    bucket_iterator end() const {
        return ++bucket_iterator(int_d(0),m_end_bucket,m_end_bucket);
    }

};

}

#endif /* BUCKETSEARCH_H_ */
