#ifndef VEXCL_CLOGS_HPP
#define VEXCL_CLOGS_HPP

/*
The MIT License

Copyright (c) 2014 Bruce Merry <bmerry@users.sourceforge.net>

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
*/

/**
 * \file   external/clogs.hpp
 * \author Bruce Merry <bmerry@users.sourceforge.net>
 * \brief  Enables use of CLOGS (http://clogs.sourceforge.net) primitives.
 */

#include <type_traits>
#include <vector>
#include <vexcl/vector.hpp>
#include <vexcl/types.hpp>
#include <clogs/scan.h>

#ifdef VEXCL_BACKEND_CUDA
#  error "clogs interoperation is not supported for the CUDA backend!"
#endif

namespace vex {

template<typename T, typename Enable = void>
struct clogs_type {};

template<>
struct clogs_type<cl_char, void>
{
    static inline clogs::Type type() { return clogs::TYPE_CHAR; }
};

template<>
struct clogs_type<cl_short, void>
{
    static inline clogs::Type type() { return clogs::TYPE_SHORT; }
};

template<>
struct clogs_type<cl_int, void>
{
    static inline clogs::Type type() { return clogs::TYPE_INT; }
};

template<>
struct clogs_type<cl_long, void>
{
    static inline clogs::Type type() { return clogs::TYPE_LONG; }
};


template<>
struct clogs_type<cl_uchar, void>
{
    static inline clogs::Type type() { return clogs::TYPE_UCHAR; }
};

template<>
struct clogs_type<cl_ushort, void>
{
    static inline clogs::Type type() { return clogs::TYPE_USHORT; }
};

template<>
struct clogs_type<cl_uint, void>
{
    static inline clogs::Type type() { return clogs::TYPE_UINT; }
};

template<>
struct clogs_type<cl_ulong, void>
{
    static inline clogs::Type type() { return clogs::TYPE_ULONG; }
};


template<>
struct clogs_type<cl_float, void>
{
    static inline clogs::Type type() { return clogs::TYPE_FLOAT; }
};

template<>
struct clogs_type<cl_double, void>
{
    static inline clogs::Type type() { return clogs::TYPE_DOUBLE; }
};


template<typename T>
struct clogs_type<T, typename std::enable_if<
        vex::is_cl_native<T>::value && (vex::cl_vector_length<T>::value > 1)>::type>
{
    static inline clogs::Type type() {
        return clogs::Type(
            clogs_type<typename vex::cl_scalar_of<T>::type>::type().getBaseType(),
            vex::cl_vector_length<T>::value);
    }
};

template<typename T, typename Enable = void>
struct clogs_is_scannable : public std::false_type {};

template<typename T>
struct clogs_is_scannable<T, typename std::enable_if<
        sizeof(clogs_type<T>::type())
        && std::is_integral<typename vex::cl_scalar_of<T>::type>::value>::type>
    : public std::true_type {};

template<typename T>
void exclusive_scan(
        const vex::vector<T> &src,
        typename std::enable_if<clogs_is_scannable<T>::value, vex::vector<T> >::type &dst) {
    const std::vector<backend::command_queue> &queue = src.queue_list();

    std::vector<T> tail;
    /* If there is more than one partition, we need to take a copy the last
     * element in each partition (except the last) as otherwise information
     * about it is lost.
     *
     * This must be captured here rather than later, in case the input and
     * output alias.
     */
    if (queue.size() > 1) {
        tail.resize(queue.size() - 1);
        for (unsigned d = 0; d < tail.size(); ++d) {
            if (src.part_size(d))
                tail[d] = src[src.part_start(d + 1) - 1];
        }
    }

    for (unsigned d = 0; d < queue.size(); ++d) {
        if (src.part_size(d)) {
            clogs::Scan scanner(
                    queue[d].getInfo<CL_QUEUE_CONTEXT>(),
                    queue[d].getInfo<CL_QUEUE_DEVICE>(),
                    clogs_type<T>::type());
            scanner.enqueue(
                    queue[d],
                    src(d).raw_buffer(), dst(d).raw_buffer(), src.part_size(d));
        }
    }

    /* If there is more than one partition, update all of them except for the
     * first. This is not very efficient: it would be better to have deeper
     * hooks into clogs so that the block sums could be prefix summed across
     * partitions.
     */
    if (queue.size() > 1) {
        T sum{};
        for (unsigned d = 0; d < tail.size(); ++d) {
            if (src.part_size(d)) {
                sum += tail[d];
                sum += dst[src.part_start(d + 1) - 1];
                // Wrap partition into vector for ease of use:
                vex::vector<T> part(queue[d + 1], dst(d + 1));
                part = sum + part;
            }
        }
    }
}

} // namespace vex

#endif
