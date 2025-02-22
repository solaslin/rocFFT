// Copyright (C) 2020 - 2022 Advanced Micro Devices, Inc. All rights reserved.
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.

#ifndef FFT_PARAMS_H
#define FFT_PARAMS_H

#include <algorithm>
#include <complex>
#include <hip/hip_runtime_api.h>
#include <iostream>
#include <mutex>
#include <numeric>
#include <omp.h>
#include <random>
#include <tuple>
#include <vector>

#include "../shared/printbuffer.h"
#include "../shared/ptrdiff.h"

enum fft_status
{
    fft_status_success,
    fft_status_failure,
    fft_status_invalid_arg_value,
    fft_status_invalid_dimensions,
    fft_status_invalid_array_type,
    fft_status_invalid_strides,
    fft_status_invalid_distance,
    fft_status_invalid_offset,
    fft_status_invalid_work_buffer,
};

enum fft_transform_type
{
    fft_transform_type_complex_forward,
    fft_transform_type_complex_inverse,
    fft_transform_type_real_forward,
    fft_transform_type_real_inverse,
};

enum fft_precision
{
    fft_precision_single,
    fft_precision_double,
};

enum fft_array_type
{
    fft_array_type_complex_interleaved,
    fft_array_type_complex_planar,
    fft_array_type_real,
    fft_array_type_hermitian_interleaved,
    fft_array_type_hermitian_planar,
    fft_array_type_unset,
};

enum fft_result_placement
{
    fft_placement_inplace,
    fft_placement_notinplace,
};

// Determine the size of the data type given the precision and type.
template <typename Tsize>
inline Tsize var_size(const fft_precision precision, const fft_array_type type)
{
    size_t var_size = 0;
    switch(precision)
    {
    case fft_precision_single:
        var_size = sizeof(float);
        break;
    case fft_precision_double:
        var_size = sizeof(double);
        break;
    }
    switch(type)
    {
    case fft_array_type_complex_interleaved:
    case fft_array_type_hermitian_interleaved:
        var_size *= 2;
        break;
    default:
        break;
    }
    return var_size;
}

// Container class for test parameters.
class fft_params
{
public:
    // All parameters are row-major.
    std::vector<size_t>  length;
    std::vector<size_t>  istride;
    std::vector<size_t>  ostride;
    size_t               nbatch         = 1;
    fft_precision        precision      = fft_precision_double;
    fft_transform_type   transform_type = fft_transform_type_complex_forward;
    fft_result_placement placement      = fft_placement_inplace;
    size_t               idist          = 0;
    size_t               odist          = 0;
    fft_array_type       itype          = fft_array_type_unset;
    fft_array_type       otype          = fft_array_type_unset;
    std::vector<size_t>  ioffset        = {0, 0};
    std::vector<size_t>  ooffset        = {0, 0};

    std::vector<size_t> isize;
    std::vector<size_t> osize;

    size_t workbuffersize = 0;

    // run testing load/store callbacks
    bool                    run_callbacks   = false;
    static constexpr double load_cb_scalar  = 0.457813941;
    static constexpr double store_cb_scalar = 0.391504938;

    // Check that data outside of output strides is not overwritten.
    // This is only set explicitly on some tests where there's space
    // between dimensions, but the dimensions are still in-order.
    // We're not trying to generically find holes in arbitrary data
    // layouts.
    //
    // NOTE: this flag is not included in tokens, since it doesn't
    // affect how the FFT library behaves.
    bool check_output_strides = false;

    // scaling factor - we do a pointwise multiplication of outputs by
    // this factor
    double scale_factor = 1.0;

    fft_params(){};
    virtual ~fft_params(){};

    // Given an array type, return the name as a string.
    static std::string array_type_name(const fft_array_type type, bool verbose = true)
    {
        switch(type)
        {
        case fft_array_type_complex_interleaved:
            return verbose ? "fft_array_type_complex_interleaved" : "CI";
        case fft_array_type_complex_planar:
            return verbose ? "fft_array_type_complex_planar" : "CP";
        case fft_array_type_real:
            return verbose ? "fft_array_type_real" : "R";
        case fft_array_type_hermitian_interleaved:
            return verbose ? "fft_array_type_hermitian_interleaved" : "HI";
        case fft_array_type_hermitian_planar:
            return verbose ? "fft_array_type_hermitian_planar" : "HP";
        case fft_array_type_unset:
            return verbose ? "fft_array_type_unset" : "UN";
        }
        return "";
    }

    std::string transform_type_name() const
    {
        switch(transform_type)
        {
        case fft_transform_type_complex_forward:
            return "fft_transform_type_complex_forward";
        case fft_transform_type_complex_inverse:
            return "fft_transform_type_complex_inverse";
        case fft_transform_type_real_forward:
            return "fft_transform_type_real_forward";
        case fft_transform_type_real_inverse:
            return "fft_transform_type_real_inverse";
        default:
            throw std::runtime_error("Invalid transform type");
        }
    }

    // Convert to string for output.
    std::string str(const std::string& separator = ", ") const
    {
        std::stringstream ss;
        ss << "length:";
        for(auto i : length)
            ss << " " << i;
        ss << separator;
        ss << "istride:";
        for(auto i : istride)
            ss << " " << i;
        ss << separator;
        ss << "idist: " << idist << separator;

        ss << "ostride:";
        for(auto i : ostride)
            ss << " " << i;
        ss << separator;
        ss << "odist: " << odist << separator;

        ss << "batch: " << nbatch << separator;
        ss << "isize:";
        for(auto i : isize)
            ss << " " << i;
        ss << separator;
        ss << "osize:";
        for(auto i : osize)
            ss << " " << i;
        ss << separator;

        ss << "ioffset:";
        for(auto i : ioffset)
            ss << " " << i;
        ss << separator;
        ss << "ooffset:";
        for(auto i : ooffset)
            ss << " " << i;
        ss << separator;

        if(placement == fft_placement_inplace)
            ss << "in-place";
        else
            ss << "out-of-place";
        ss << separator;
        ss << "transform_type: " << transform_type_name() << separator;
        ss << array_type_name(itype) << " -> " << array_type_name(otype) << separator;
        if(precision == fft_precision_single)
            ss << "single-precision";
        else
            ss << "double-precision";
        ss << separator;

        ss << "ilength:";
        for(const auto i : ilength())
            ss << " " << i;
        ss << separator;
        ss << "olength:";
        for(const auto i : olength())
            ss << " " << i;
        ss << separator;

        ss << "ibuffer_size:";
        for(const auto i : ibuffer_sizes())
            ss << " " << i;
        ss << separator;

        ss << "obuffer_size:";
        for(const auto i : obuffer_sizes())
            ss << " " << i;
        ss << separator;

        if(scale_factor != 1.0)
            ss << "scale factor: " << scale_factor << separator;

        return ss.str();
    }

    // Produce a stringified token of the test fft params.
    std::string token() const
    {
        std::string ret;

        switch(transform_type)
        {
        case fft_transform_type_complex_forward:
            ret += "complex_forward_";
            break;
        case fft_transform_type_complex_inverse:
            ret += "complex_inverse_";
            break;
        case fft_transform_type_real_forward:
            ret += "real_forward_";
            break;
        case fft_transform_type_real_inverse:
            ret += "real_inverse_";
            break;
        }

        ret += "len_";

        for(auto n : length)
        {
            ret += std::to_string(n);
            ret += "_";
        }
        switch(precision)
        {
        case fft_precision_single:
            ret += "single_";
            break;
        case fft_precision_double:
            ret += "double_";
            break;
        }

        switch(placement)
        {
        case fft_placement_inplace:
            ret += "ip_";
            break;
        case fft_placement_notinplace:
            ret += "op_";
            break;
        }

        ret += "batch_";
        ret += std::to_string(nbatch);

        auto append_array_info = [&ret](const std::vector<size_t>& stride, fft_array_type type) {
            for(auto s : stride)
            {
                ret += std::to_string(s);
                ret += "_";
            }

            switch(type)
            {
            case fft_array_type_complex_interleaved:
                ret += "CI";
                break;
            case fft_array_type_complex_planar:
                ret += "CP";
                break;
            case fft_array_type_real:
                ret += "R";
                break;
            case fft_array_type_hermitian_interleaved:
                ret += "HI";
                break;
            case fft_array_type_hermitian_planar:
                ret += "HP";
                break;
            default:
                ret += "UN";
                break;
            }
        };

        ret += "_istride_";
        append_array_info(istride, itype);

        ret += "_ostride_";
        append_array_info(ostride, otype);

        ret += "_idist_";
        ret += std::to_string(idist);
        ret += "_odist_";
        ret += std::to_string(odist);

        ret += "_ioffset";
        for(auto n : ioffset)
        {
            ret += "_";
            ret += std::to_string(n);
        }

        ret += "_ooffset";
        for(auto n : ooffset)
        {
            ret += "_";
            ret += std::to_string(n);
        }

        if(run_callbacks)
            ret += "_CB";

        if(scale_factor != 1.0)
            ret += "_scale";

        return ret;
    }

    // Set all params from a stringified token.
    void from_token(std::string token)
    {
        std::vector<std::string> vals;

        std::string delimiter = "_";
        {
            size_t pos = 0;
            while((pos = token.find(delimiter)) != std::string::npos)
            {
                auto val = token.substr(0, pos);
                vals.push_back(val);
                token.erase(0, pos + delimiter.length());
            }
            vals.push_back(token);
        }

        auto vector_parser
            = [](const std::vector<std::string>& vals, const std::string token, size_t& pos) {
                  if(vals[pos++] != token)
                      throw std::runtime_error("Unable to parse token");
                  std::vector<size_t> vec;

                  while(pos < vals.size())
                  {
                      if(std::all_of(vals[pos].begin(), vals[pos].end(), ::isdigit))
                      {
                          vec.push_back(std::stoull(vals[pos++]));
                      }
                      else
                      {
                          break;
                      }
                  }
                  return vec;
              };

        auto type_parser = [](const std::string& val) {
            if(val == "CI")
                return fft_array_type_complex_interleaved;
            else if(val == "CP")
                return fft_array_type_complex_planar;
            else if(val == "R")
                return fft_array_type_real;
            else if(val == "HI")
                return fft_array_type_hermitian_interleaved;
            else if(val == "HP")
                return fft_array_type_hermitian_planar;
            return fft_array_type_unset;
        };

        size_t pos = 0;

        bool complex = vals[pos++] == "complex";
        bool forward = vals[pos++] == "forward";

        if(complex && forward)
            transform_type = fft_transform_type_complex_forward;
        if(complex && !forward)
            transform_type = fft_transform_type_complex_inverse;
        if(!complex && forward)
            transform_type = fft_transform_type_real_forward;
        if(!complex && !forward)
            transform_type = fft_transform_type_real_inverse;

        length = vector_parser(vals, "len", pos);

        if(vals[pos] == "single")
            precision = fft_precision_single;
        else if(vals[pos] == "double")
            precision = fft_precision_double;
        pos++;

        placement = (vals[pos++] == "ip") ? fft_placement_inplace : fft_placement_notinplace;

        if(vals[pos++] != "batch")
            throw std::runtime_error("Unable to parse token");
        nbatch = std::stoull(vals[pos++]);

        istride = vector_parser(vals, "istride", pos);

        itype = type_parser(vals[pos]);
        pos++;

        ostride = vector_parser(vals, "ostride", pos);

        otype = type_parser(vals[pos]);
        pos++;

        if(vals[pos++] != "idist")
            throw std::runtime_error("Unable to parse token");
        idist = std::stoull(vals[pos++]);

        if(vals[pos++] != "odist")
            throw std::runtime_error("Unable to parse token");
        odist = std::stoull(vals[pos++]);

        ioffset = vector_parser(vals, "ioffset", pos);

        ooffset = vector_parser(vals, "ooffset", pos);

        if(pos < vals.size() && vals[pos] == "CB")
        {
            run_callbacks = true;
            ++pos;
        }

        if(pos < vals.size() && vals[pos] == "scale")
        {
            // just pick some factor that's not zero or one
            scale_factor = 0.1239;
            ++pos;
        }
    }

    // Stream output operator (for gtest, etc).
    friend std::ostream& operator<<(std::ostream& stream, const fft_params& params)
    {
        stream << params.str();
        return stream;
    }

    // Dimension of the transform.
    size_t dim() const
    {
        return length.size();
    }

    virtual std::vector<size_t> ilength() const
    {
        auto ilength = length;
        if(transform_type == fft_transform_type_real_inverse)
            ilength[dim() - 1] = ilength[dim() - 1] / 2 + 1;
        return ilength;
    }

    virtual std::vector<size_t> olength() const
    {
        auto olength = length;
        if(transform_type == fft_transform_type_real_forward)
            olength[dim() - 1] = olength[dim() - 1] / 2 + 1;
        return olength;
    }

    static size_t nbuffer(const fft_array_type type)
    {
        switch(type)
        {
        case fft_array_type_real:
        case fft_array_type_complex_interleaved:
        case fft_array_type_hermitian_interleaved:
            return 1;
        case fft_array_type_complex_planar:
        case fft_array_type_hermitian_planar:
            return 2;
        case fft_array_type_unset:
            return 0;
        }
        return 0;
    }

    // Number of input buffers
    size_t nibuffer() const
    {
        return nbuffer(itype);
    }

    // Number of output buffers
    size_t nobuffer() const
    {
        return nbuffer(otype);
    }

    void set_iotypes()
    {
        if(itype == fft_array_type_unset)
        {
            switch(transform_type)
            {
            case fft_transform_type_complex_forward:
            case fft_transform_type_complex_inverse:
                itype = fft_array_type_complex_interleaved;
                break;
            case fft_transform_type_real_forward:
                itype = fft_array_type_real;
                break;
            case fft_transform_type_real_inverse:
                itype = fft_array_type_hermitian_interleaved;
                break;
            default:
                throw std::runtime_error("Invalid transform type");
            }
        }
        if(otype == fft_array_type_unset)
        {
            switch(transform_type)
            {
            case fft_transform_type_complex_forward:
            case fft_transform_type_complex_inverse:
                otype = fft_array_type_complex_interleaved;
                break;
            case fft_transform_type_real_forward:
                otype = fft_array_type_hermitian_interleaved;
                break;
            case fft_transform_type_real_inverse:
                otype = fft_array_type_real;
                break;
            default:
                throw std::runtime_error("Invalid transform type");
            }
        }
    }

    // Check that the input and output types are consistent.
    bool check_iotypes() const
    {
        switch(itype)
        {
        case fft_array_type_complex_interleaved:
        case fft_array_type_complex_planar:
        case fft_array_type_hermitian_interleaved:
        case fft_array_type_hermitian_planar:
        case fft_array_type_real:
            break;
        default:
            throw std::runtime_error("Invalid Input array type format");
        }

        switch(otype)
        {
        case fft_array_type_complex_interleaved:
        case fft_array_type_complex_planar:
        case fft_array_type_hermitian_interleaved:
        case fft_array_type_hermitian_planar:
        case fft_array_type_real:
            break;
        default:
            throw std::runtime_error("Invalid Input array type format");
        }

        // Check that format choices are supported
        if(transform_type != fft_transform_type_real_forward
           && transform_type != fft_transform_type_real_inverse)
        {
            if(placement == fft_placement_inplace && itype != otype)
            {
                throw std::runtime_error(
                    "In-place transforms must have identical input and output types");
            }
        }

        bool okformat = true;
        switch(itype)
        {
        case fft_array_type_complex_interleaved:
        case fft_array_type_complex_planar:
            okformat = (otype == fft_array_type_complex_interleaved
                        || otype == fft_array_type_complex_planar);
            break;
        case fft_array_type_hermitian_interleaved:
        case fft_array_type_hermitian_planar:
            okformat = otype == fft_array_type_real;
            break;
        case fft_array_type_real:
            okformat = (otype == fft_array_type_hermitian_interleaved
                        || otype == fft_array_type_hermitian_planar);
            break;
        default:
            throw std::runtime_error("Invalid Input array type format");
        }

        return okformat;
    }

    // Given a length vector, set the rest of the strides.
    // The optional argument stride0 sets the stride for the contiguous dimension.
    // The optional rcpadding argument sets the stride correctly for in-place
    // multi-dimensional real/complex transforms.
    // Format is row-major.
    template <typename T1>
    std::vector<T1> compute_stride(const std::vector<T1>&     length,
                                   const std::vector<size_t>& stride0   = std::vector<size_t>(),
                                   const bool                 rcpadding = false) const
    {
        std::vector<T1> stride(dim());

        size_t dimoffset = 0;

        if(stride0.size() == 0)
        {
            // Set the contiguous stride:
            stride[dim() - 1] = 1;
            dimoffset         = 1;
        }
        else
        {
            // Copy the input values to the end of the stride array:
            for(size_t i = 0; i < stride0.size(); ++i)
            {
                stride[dim() - stride0.size() + i] = stride0[i];
            }
        }

        if(stride0.size() < dim())
        {
            // Compute any remaining values via recursion.
            for(size_t i = dim() - dimoffset - stride0.size(); i-- > 0;)
            {
                auto lengthip1 = length[i + 1];
                if(rcpadding && i == dim() - 2)
                {
                    lengthip1 = 2 * (lengthip1 / 2 + 1);
                }
                stride[i] = stride[i + 1] * lengthip1;
            }
        }

        return stride;
    }

    void compute_istride()
    {
        istride = compute_stride(ilength(),
                                 istride,
                                 placement == fft_placement_inplace
                                     && transform_type == fft_transform_type_real_forward);
    }

    void compute_ostride()
    {
        ostride = compute_stride(olength(),
                                 ostride,
                                 placement == fft_placement_inplace
                                     && transform_type == fft_transform_type_real_inverse);
    }

    virtual void compute_isize()
    {
        auto   il  = ilength();
        size_t val = compute_ptrdiff(il, istride, nbatch, idist);
        isize.resize(nibuffer());
        for(unsigned int i = 0; i < isize.size(); ++i)
        {
            isize[i] = val + ioffset[i];
        }
    }

    virtual void compute_osize()
    {
        auto   ol  = olength();
        size_t val = compute_ptrdiff(ol, ostride, nbatch, odist);
        osize.resize(nobuffer());
        for(unsigned int i = 0; i < osize.size(); ++i)
        {
            osize[i] = val + ooffset[i];
        }
    }

    std::vector<size_t> ibuffer_sizes() const
    {
        std::vector<size_t> ibuffer_sizes;

        // In-place real-to-complex transforms need to have enough space in the input buffer to
        // accomadate the output, which is slightly larger.
        if(placement == fft_placement_inplace && transform_type == fft_transform_type_real_forward)
        {
            return obuffer_sizes();
        }

        if(isize.empty())
            return ibuffer_sizes;

        switch(itype)
        {
        case fft_array_type_complex_planar:
        case fft_array_type_hermitian_planar:
            ibuffer_sizes.resize(2);
            break;
        default:
            ibuffer_sizes.resize(1);
        }
        for(unsigned i = 0; i < ibuffer_sizes.size(); i++)
        {
            ibuffer_sizes[i] = isize[i] * var_size<size_t>(precision, itype);
        }
        return ibuffer_sizes;
    }

    virtual std::vector<size_t> obuffer_sizes() const
    {
        std::vector<size_t> obuffer_sizes;

        if(osize.empty())
            return obuffer_sizes;

        switch(otype)
        {
        case fft_array_type_complex_planar:
        case fft_array_type_hermitian_planar:
            obuffer_sizes.resize(2);
            break;
        default:
            obuffer_sizes.resize(1);
        }
        for(unsigned i = 0; i < obuffer_sizes.size(); i++)
        {
            obuffer_sizes[i] = osize[i] * var_size<size_t>(precision, otype);
        }
        return obuffer_sizes;
    }

    // Compute the idist for a given transform based on the placeness, transform type, and data
    // layout.
    void set_idist()
    {
        if(idist != 0)
            return;

        // In-place 1D transforms need extra dist.
        if(transform_type == fft_transform_type_real_forward && dim() == 1
           && placement == fft_placement_inplace)
        {
            idist = 2 * (length[0] / 2 + 1) * istride[0];
            return;
        }

        if(transform_type == fft_transform_type_real_inverse && dim() == 1)
        {
            idist = (length[0] / 2 + 1) * istride[0];
            return;
        }

        idist = (transform_type == fft_transform_type_real_inverse)
                    ? (length[dim() - 1] / 2 + 1) * istride[dim() - 1]
                    : length[dim() - 1] * istride[dim() - 1];
        for(unsigned int i = 0; i < dim() - 1; ++i)
        {
            idist = std::max(length[i] * istride[i], idist);
        }
    }

    // Compute the odist for a given transform based on the placeness, transform type, and data
    // layout.  Row-major.
    void set_odist()
    {
        if(odist != 0)
            return;

        // In-place 1D transforms need extra dist.
        if(transform_type == fft_transform_type_real_inverse && dim() == 1
           && placement == fft_placement_inplace)
        {
            odist = 2 * (length[0] / 2 + 1) * ostride[0];
            return;
        }

        if(transform_type == fft_transform_type_real_forward && dim() == 1)
        {
            odist = (length[0] / 2 + 1) * ostride[0];
            return;
        }

        odist = (transform_type == fft_transform_type_real_forward)
                    ? (length[dim() - 1] / 2 + 1) * ostride[dim() - 1]
                    : length[dim() - 1] * ostride[dim() - 1];
        for(unsigned int i = 0; i < dim() - 1; ++i)
        {
            odist = std::max(length[i] * ostride[i], odist);
        }
    }

    // Return true if the given GPU parameters would produce a valid transform.
    bool valid(const int verbose) const
    {
        if(ioffset.size() < nibuffer() || ooffset.size() < nobuffer())
            return false;

        // Check that in-place transforms have the same input and output stride:
        if(placement == fft_placement_inplace)
        {
            const auto stridesize = std::min(istride.size(), ostride.size());
            bool       samestride = true;
            for(unsigned int i = 0; i < stridesize; ++i)
            {
                if(istride[i] != ostride[i])
                    samestride = false;
            }
            if((transform_type == fft_transform_type_complex_forward
                || transform_type == fft_transform_type_complex_inverse)
               && !samestride)
            {
                // In-place transforms require identical input and output strides.
                if(verbose)
                {
                    std::cout << "istride:";
                    for(const auto& i : istride)
                        std::cout << " " << i;
                    std::cout << " ostride0:";
                    for(const auto& i : ostride)
                        std::cout << " " << i;
                    std::cout << " differ; skipped for in-place transforms: skipping test"
                              << std::endl;
                }
                return false;
            }

            if((transform_type == fft_transform_type_complex_forward
                || transform_type == fft_transform_type_complex_inverse)
               && (idist != odist))
            {
                // In-place transforms require identical distance
                if(verbose)
                {
                    std::cout << "idist:" << idist << " odist:" << odist
                              << " differ; skipped for in-place transforms: skipping test"
                              << std::endl;
                }
                return false;
            }

            if((transform_type == fft_transform_type_real_forward
                || transform_type == fft_transform_type_real_inverse)
               && (istride.back() != 1 || ostride.back() != 1))
            {
                // In-place real/complex transforms require unit strides.
                if(verbose)
                {
                    std::cout
                        << "istride.back(): " << istride.back()
                        << " ostride.back(): " << ostride.back()
                        << " must be unitary for in-place real/complex transforms: skipping test"
                        << std::endl;
                }
                return false;
            }

            if((itype == fft_array_type_complex_interleaved
                && otype == fft_array_type_complex_planar)
               || (itype == fft_array_type_complex_planar
                   && otype == fft_array_type_complex_interleaved))
            {
                if(verbose)
                {
                    std::cout << "In-place c2c transforms require identical io types; skipped.\n";
                }
                return false;
            }

            // Check offsets
            switch(transform_type)
            {
            case fft_transform_type_complex_forward:
            case fft_transform_type_complex_inverse:
                for(unsigned int i = 0; i < nibuffer(); ++i)
                {
                    if(ioffset[i] != ooffset[i])
                        return false;
                }
                break;
            case fft_transform_type_real_forward:
                if(ioffset[0] != 2 * ooffset[0])
                    return false;
                break;
            case fft_transform_type_real_inverse:
                if(2 * ioffset[0] != ooffset[0])
                    return false;
                break;
            }
        }

        if(!check_iotypes())
            return false;

        // we can only check output strides on out-of-place
        // transforms, since we need to initialize output to a known
        // pattern
        if(placement == fft_placement_inplace && check_output_strides)
            return false;

        // The parameters are valid.
        return true;
    }

    // Fill in any missing parameters.
    void validate()
    {
        set_iotypes();
        compute_istride();
        compute_ostride();
        set_idist();
        set_odist();
        compute_isize();
        compute_osize();
    }

    // Column-major getters:
    std::vector<size_t> length_cm() const
    {
        auto length_cm = length;
        std::reverse(std::begin(length_cm), std::end(length_cm));
        return length_cm;
    }
    std::vector<size_t> ilength_cm() const
    {
        auto ilength_cm = ilength();
        std::reverse(std::begin(ilength_cm), std::end(ilength_cm));
        return ilength_cm;
    }
    std::vector<size_t> olength_cm() const
    {
        auto olength_cm = olength();
        std::reverse(std::begin(olength_cm), std::end(olength_cm));
        return olength_cm;
    }
    std::vector<size_t> istride_cm() const
    {
        auto istride_cm = istride;
        std::reverse(std::begin(istride_cm), std::end(istride_cm));
        return istride_cm;
    }
    std::vector<size_t> ostride_cm() const
    {
        auto ostride_cm = ostride;
        std::reverse(std::begin(ostride_cm), std::end(ostride_cm));
        return ostride_cm;
    }

    template <typename Tallocator, typename Tstream = std::ostream>
    void print_ibuffer(const std::vector<std::vector<char, Tallocator>>& buf,
                       Tstream&                                          stream = std::cout) const
    {
        switch(itype)
        {
        case fft_array_type_complex_interleaved:
        case fft_array_type_hermitian_interleaved:
        {
            switch(precision)
            {
            case fft_precision_single:
            {
                buffer_printer<std::complex<float>> s;
                s.print_buffer(buf, ilength(), istride, nbatch, idist, ioffset);
                break;
            }
            case fft_precision_double:
            {
                buffer_printer<std::complex<double>> s;
                s.print_buffer(buf, ilength(), istride, nbatch, idist, ioffset);
                break;
            }
            }
            break;
        }
        case fft_array_type_complex_planar:
        case fft_array_type_hermitian_planar:
        case fft_array_type_real:
        {
            switch(precision)
            {
            case fft_precision_single:
            {
                buffer_printer<float> s;
                s.print_buffer(buf, ilength(), istride, nbatch, idist, ioffset);
                break;
            }
            case fft_precision_double:
            {
                buffer_printer<double> s;
                s.print_buffer(buf, ilength(), istride, nbatch, idist, ioffset);
                break;
            }
            }
            break;
        }
        default:
            throw std::runtime_error("Invalid itype in print_ibuffer");
        }
    }

    template <typename Tallocator, typename Tstream = std::ostream>
    void print_obuffer(const std::vector<std::vector<char, Tallocator>>& buf,
                       Tstream&                                          stream = std::cout) const
    {
        switch(otype)
        {
        case fft_array_type_complex_interleaved:
        case fft_array_type_hermitian_interleaved:
        {
            switch(precision)
            {
            case fft_precision_single:
            {
                buffer_printer<std::complex<float>> s;
                s.print_buffer(buf, olength(), ostride, nbatch, odist, ooffset);
                break;
            }
            case fft_precision_double:
                buffer_printer<std::complex<double>> s;
                s.print_buffer(buf, olength(), ostride, nbatch, odist, ooffset);
                break;
            }
            break;
        }
        case fft_array_type_complex_planar:
        case fft_array_type_hermitian_planar:
        case fft_array_type_real:
        {
            switch(precision)
            {
            case fft_precision_single:
            {
                buffer_printer<float> s;
                s.print_buffer(buf, olength(), ostride, nbatch, odist, ooffset);
                break;
            }
            case fft_precision_double:
            {
                buffer_printer<double> s;
                s.print_buffer(buf, olength(), ostride, nbatch, odist, ooffset);
                break;
            }
            }
            break;
        }

        default:
            throw std::runtime_error("Invalid itype in print_obuffer");
        }
    }

    template <typename Tallocator>
    void print_ibuffer_flat(const std::vector<std::vector<char, Tallocator>>& buf) const
    {
        switch(itype)
        {
        case fft_array_type_complex_interleaved:
        case fft_array_type_hermitian_interleaved:
        {
            switch(precision)
            {
            case fft_precision_single:
            {
                buffer_printer<std::complex<float>> s;
                s.print_buffer_flat(buf, osize, ooffset);
                break;
            }
            case fft_precision_double:
                buffer_printer<std::complex<double>> s;
                s.print_buffer_flat(buf, osize, ooffset);
                break;
            }
            break;
        }
        case fft_array_type_complex_planar:
        case fft_array_type_hermitian_planar:
        case fft_array_type_real:
        {
            switch(precision)
            {
            case fft_precision_single:
            {
                buffer_printer<float> s;
                s.print_buffer_flat(buf, osize, ooffset);
                break;
            }
            case fft_precision_double:
            {
                buffer_printer<double> s;
                s.print_buffer_flat(buf, osize, ooffset);
                break;
            }
            }
            break;
        default:
            throw std::runtime_error("Invalid itype in print_ibuffer_flat");
        }
        }
    }

    template <typename Tallocator>
    void print_obuffer_flat(const std::vector<std::vector<char, Tallocator>>& buf) const
    {
        switch(otype)
        {
        case fft_array_type_complex_interleaved:
        case fft_array_type_hermitian_interleaved:
        {
            switch(precision)
            {
            case fft_precision_single:
            {
                buffer_printer<std::complex<float>> s;
                s.print_buffer_flat(buf, osize, ooffset);
                break;
            }
            case fft_precision_double:
                buffer_printer<std::complex<double>> s;
                s.print_buffer_flat(buf, osize, ooffset);
                break;
            }
            break;
        }
        case fft_array_type_complex_planar:
        case fft_array_type_hermitian_planar:
        case fft_array_type_real:
        {
            switch(precision)
            {
            case fft_precision_single:
            {
                buffer_printer<float> s;
                s.print_buffer_flat(buf, osize, ooffset);
                break;
            }

            case fft_precision_double:
            {
                buffer_printer<double> s;
                s.print_buffer_flat(buf, osize, ooffset);
                break;
            }
            }
            break;
        default:
            throw std::runtime_error("Invalid itype in print_ibuffer_flat");
        }
        }
    }

    virtual fft_status set_callbacks(void* load_cb_host,
                                     void* load_cb_data,
                                     void* store_cb_host,
                                     void* store_cb_data)
    {
        return fft_status_success;
    }

    virtual fft_status execute(void** in, void** out)
    {
        return fft_status_success;
    };

    size_t fft_params_vram_footprint()
    {
        return fft_params::vram_footprint();
    }

    virtual size_t vram_footprint()
    {
        const auto ibuf_size = ibuffer_sizes();
        size_t     val       = std::accumulate(ibuf_size.begin(), ibuf_size.end(), (size_t)1);
        if(placement == fft_placement_notinplace)
        {
            const auto obuf_size = obuffer_sizes();
            val += std::accumulate(obuf_size.begin(), obuf_size.end(), (size_t)1);
        }
        return val;
    }

    // Specific exception type for work buffer allocation failure.
    // Tests that hit this can't fit on the GPU and should be skipped.
    struct work_buffer_alloc_failure : public std::runtime_error
    {
        work_buffer_alloc_failure(const std::string& s)
            : std::runtime_error(s)
        {
        }
    };

    virtual fft_status create_plan()
    {
        return fft_status_success;
    }
};

// This is used with the program_options class so that the user can type an integer on the
// command line and we store into an enum varaible
template <typename _Elem, typename _Traits>
std::basic_istream<_Elem, _Traits>& operator>>(std::basic_istream<_Elem, _Traits>& stream,
                                               fft_array_type&                     atype)
{
    unsigned tmp;
    stream >> tmp;
    atype = fft_array_type(tmp);
    return stream;
}

// similarly for transform type
template <typename _Elem, typename _Traits>
std::basic_istream<_Elem, _Traits>& operator>>(std::basic_istream<_Elem, _Traits>& stream,
                                               fft_transform_type&                 ttype)
{
    unsigned tmp;
    stream >> tmp;
    ttype = fft_transform_type(tmp);
    return stream;
}

// count the number of total iterations for 1-, 2-, and 3-D dimensions
template <typename T1>
size_t count_iters(const T1& i)
{
    return i;
}

template <typename T1>
size_t count_iters(const std::tuple<T1, T1>& i)
{
    return std::get<0>(i) * std::get<1>(i);
}

template <typename T1>
size_t count_iters(const std::tuple<T1, T1, T1>& i)
{
    return std::get<0>(i) * std::get<1>(i) * std::get<2>(i);
}

// Work out how many partitions to break our iteration problem into
template <typename T1>
static size_t compute_partition_count(T1 length)
{
#ifdef BUILD_CLIENTS_TESTS_OPENMP
    // we seem to get contention from too many threads, which slows
    // things down.  particularly noticeable with mix_3D tests
    static const size_t MAX_PARTITIONS = 8;
    size_t              iters          = count_iters(length);
    size_t hw_threads = std::min(MAX_PARTITIONS, static_cast<size_t>(omp_get_num_procs()));
    if(!hw_threads)
        return 1;

    // don't bother threading problem sizes that are too small. pick
    // an arbitrary number of iterations and ensure that each thread
    // has at least that many iterations to process
    static const size_t MIN_ITERS_PER_THREAD = 2048;

    // either use the whole CPU, or use ceil(iters/iters_per_thread)
    return std::min(hw_threads, (iters + MIN_ITERS_PER_THREAD + 1) / MIN_ITERS_PER_THREAD);
#else
    return 1;
#endif
}

// Break a scalar length into some number of pieces, returning
// [(start0, end0), (start1, end1), ...]
template <typename T1>
std::vector<std::pair<T1, T1>> partition_base(const T1& length, size_t num_parts)
{
    static_assert(std::is_integral<T1>::value, "Integral required.");

    // make sure we don't exceed the length
    num_parts = std::min(length, num_parts);

    std::vector<std::pair<T1, T1>> ret(num_parts);
    auto                           partition_size = length / num_parts;
    T1                             cur_partition  = 0;
    for(size_t i = 0; i < num_parts; ++i, cur_partition += partition_size)
    {
        ret[i].first  = cur_partition;
        ret[i].second = cur_partition + partition_size;
    }
    // last partition might not divide evenly, fix it up
    ret.back().second = length;
    return ret;
}

// Returns pairs of startindex, endindex, for 1D, 2D, 3D lengths
template <typename T1>
std::vector<std::pair<T1, T1>> partition_rowmajor(const T1& length)
{
    return partition_base(length, compute_partition_count(length));
}

// Partition on the leftmost part of the tuple, for row-major indexing
template <typename T1>
std::vector<std::pair<std::tuple<T1, T1>, std::tuple<T1, T1>>>
    partition_rowmajor(const std::tuple<T1, T1>& length)
{
    auto partitions = partition_base(std::get<0>(length), compute_partition_count(length));
    std::vector<std::pair<std::tuple<T1, T1>, std::tuple<T1, T1>>> ret(partitions.size());
    for(size_t i = 0; i < partitions.size(); ++i)
    {
        std::get<0>(ret[i].first)  = partitions[i].first;
        std::get<1>(ret[i].first)  = 0;
        std::get<0>(ret[i].second) = partitions[i].second;
        std::get<1>(ret[i].second) = std::get<1>(length);
    }
    return ret;
}
template <typename T1>
std::vector<std::pair<std::tuple<T1, T1, T1>, std::tuple<T1, T1, T1>>>
    partition_rowmajor(const std::tuple<T1, T1, T1>& length)
{
    auto partitions = partition_base(std::get<0>(length), compute_partition_count(length));
    std::vector<std::pair<std::tuple<T1, T1, T1>, std::tuple<T1, T1, T1>>> ret(partitions.size());
    for(size_t i = 0; i < partitions.size(); ++i)
    {
        std::get<0>(ret[i].first)  = partitions[i].first;
        std::get<1>(ret[i].first)  = 0;
        std::get<2>(ret[i].first)  = 0;
        std::get<0>(ret[i].second) = partitions[i].second;
        std::get<1>(ret[i].second) = std::get<1>(length);
        std::get<2>(ret[i].second) = std::get<2>(length);
    }
    return ret;
}

// Returns pairs of startindex, endindex, for 1D, 2D, 3D lengths
template <typename T1>
std::vector<std::pair<T1, T1>> partition_colmajor(const T1& length)
{
    return partition_base(length, compute_partition_count(length));
}

// Partition on the rightmost part of the tuple, for col-major indexing
template <typename T1>
std::vector<std::pair<std::tuple<T1, T1>, std::tuple<T1, T1>>>
    partition_colmajor(const std::tuple<T1, T1>& length)
{
    auto partitions = partition_base(std::get<1>(length), compute_partition_count(length));
    std::vector<std::pair<std::tuple<T1, T1>, std::tuple<T1, T1>>> ret(partitions.size());
    for(size_t i = 0; i < partitions.size(); ++i)
    {
        std::get<1>(ret[i].first)  = partitions[i].first;
        std::get<0>(ret[i].first)  = 0;
        std::get<1>(ret[i].second) = partitions[i].second;
        std::get<0>(ret[i].second) = std::get<0>(length);
    }
    return ret;
}
template <typename T1>
std::vector<std::pair<std::tuple<T1, T1, T1>, std::tuple<T1, T1, T1>>>
    partition_colmajor(const std::tuple<T1, T1, T1>& length)
{
    auto partitions = partition_base(std::get<2>(length), compute_partition_count(length));
    std::vector<std::pair<std::tuple<T1, T1, T1>, std::tuple<T1, T1, T1>>> ret(partitions.size());
    for(size_t i = 0; i < partitions.size(); ++i)
    {
        std::get<2>(ret[i].first)  = partitions[i].first;
        std::get<1>(ret[i].first)  = 0;
        std::get<0>(ret[i].first)  = 0;
        std::get<2>(ret[i].second) = partitions[i].second;
        std::get<1>(ret[i].second) = std::get<1>(length);
        std::get<0>(ret[i].second) = std::get<0>(length);
    }
    return ret;
}

// Specialized computation of index given 1-, 2-, 3- dimension length + stride
template <typename T1, typename T2>
size_t compute_index(T1 length, T2 stride, size_t base)
{
    static_assert(std::is_integral<T1>::value, "Integral required.");
    static_assert(std::is_integral<T2>::value, "Integral required.");
    return (length * stride) + base;
}

template <typename T1, typename T2>
size_t
    compute_index(const std::tuple<T1, T1>& length, const std::tuple<T2, T2>& stride, size_t base)
{
    static_assert(std::is_integral<T1>::value, "Integral required.");
    static_assert(std::is_integral<T2>::value, "Integral required.");
    return (std::get<0>(length) * std::get<0>(stride)) + (std::get<1>(length) * std::get<1>(stride))
           + base;
}

template <typename T1, typename T2>
size_t compute_index(const std::tuple<T1, T1, T1>& length,
                     const std::tuple<T2, T2, T2>& stride,
                     size_t                        base)
{
    static_assert(std::is_integral<T1>::value, "Integral required.");
    static_assert(std::is_integral<T2>::value, "Integral required.");
    return (std::get<0>(length) * std::get<0>(stride)) + (std::get<1>(length) * std::get<1>(stride))
           + (std::get<2>(length) * std::get<2>(stride)) + base;
}

// Copy data of dimensions length with strides istride and length idist between batches to
// a buffer with strides ostride and length odist between batches.  The input and output
// types are identical.
template <typename Tval, typename Tint1, typename Tint2, typename Tint3>
inline void copy_buffers_1to1(const Tval*                input,
                              Tval*                      output,
                              const Tint1&               whole_length,
                              const size_t               nbatch,
                              const Tint2&               istride,
                              const size_t               idist,
                              const Tint3&               ostride,
                              const size_t               odist,
                              const std::vector<size_t>& ioffset,
                              const std::vector<size_t>& ooffset)
{
    const bool idx_equals_odx = istride == ostride && idist == odist;
    size_t     idx_base       = 0;
    size_t     odx_base       = 0;
    auto       partitions     = partition_rowmajor(whole_length);
    for(size_t b = 0; b < nbatch; b++, idx_base += idist, odx_base += odist)
    {
#pragma omp parallel for num_threads(partitions.size())
        for(size_t part = 0; part < partitions.size(); ++part)
        {
            auto       index  = partitions[part].first;
            const auto length = partitions[part].second;
            do
            {
                const auto idx = compute_index(index, istride, idx_base);
                const auto odx = idx_equals_odx ? idx : compute_index(index, ostride, odx_base);
                output[odx + ooffset[0]] = input[idx + ioffset[0]];
            } while(increment_rowmajor(index, length));
        }
    }
}

// Copy data of dimensions length with strides istride and length idist between batches to
// a buffer with strides ostride and length odist between batches.  The input type is
// planar and the output type is complex interleaved.
template <typename Tval, typename Tint1, typename Tint2, typename Tint3>
inline void copy_buffers_2to1(const Tval*                input0,
                              const Tval*                input1,
                              std::complex<Tval>*        output,
                              const Tint1&               whole_length,
                              const size_t               nbatch,
                              const Tint2&               istride,
                              const size_t               idist,
                              const Tint3&               ostride,
                              const size_t               odist,
                              const std::vector<size_t>& ioffset,
                              const std::vector<size_t>& ooffset)
{
    const bool idx_equals_odx = istride == ostride && idist == odist;
    size_t     idx_base       = 0;
    size_t     odx_base       = 0;
    auto       partitions     = partition_rowmajor(whole_length);
    for(size_t b = 0; b < nbatch; b++, idx_base += idist, odx_base += odist)
    {
#pragma omp parallel for num_threads(partitions.size())
        for(size_t part = 0; part < partitions.size(); ++part)
        {
            auto       index  = partitions[part].first;
            const auto length = partitions[part].second;
            do
            {
                const auto idx = compute_index(index, istride, idx_base);
                const auto odx = idx_equals_odx ? idx : compute_index(index, ostride, odx_base);
                output[odx + ooffset[0]]
                    = std::complex<Tval>(input0[idx + ioffset[0]], input1[idx + ioffset[1]]);
            } while(increment_rowmajor(index, length));
        }
    }
}

// Copy data of dimensions length with strides istride and length idist between batches to
// a buffer with strides ostride and length odist between batches.  The input type is
// complex interleaved and the output type is planar.
template <typename Tval, typename Tint1, typename Tint2, typename Tint3>
inline void copy_buffers_1to2(const std::complex<Tval>*  input,
                              Tval*                      output0,
                              Tval*                      output1,
                              const Tint1&               whole_length,
                              const size_t               nbatch,
                              const Tint2&               istride,
                              const size_t               idist,
                              const Tint3&               ostride,
                              const size_t               odist,
                              const std::vector<size_t>& ioffset,
                              const std::vector<size_t>& ooffset)
{
    const bool idx_equals_odx = istride == ostride && idist == odist;
    size_t     idx_base       = 0;
    size_t     odx_base       = 0;
    auto       partitions     = partition_rowmajor(whole_length);
    for(size_t b = 0; b < nbatch; b++, idx_base += idist, odx_base += odist)
    {
#pragma omp parallel for num_threads(partitions.size())
        for(size_t part = 0; part < partitions.size(); ++part)
        {
            auto       index  = partitions[part].first;
            const auto length = partitions[part].second;
            do
            {
                const auto idx = compute_index(index, istride, idx_base);
                const auto odx = idx_equals_odx ? idx : compute_index(index, ostride, odx_base);
                output0[odx + ooffset[0]] = input[idx + ioffset[0]].real();
                output1[odx + ooffset[1]] = input[idx + ioffset[0]].imag();
            } while(increment_rowmajor(index, length));
        }
    }
}

// Copy data of dimensions length with strides istride and length idist between batches to
// a buffer with strides ostride and length odist between batches.  The input type given
// by itype, and the output type is given by otype.
template <typename Tallocator1,
          typename Tallocator2,
          typename Tint1,
          typename Tint2,
          typename Tint3>
inline void copy_buffers(const std::vector<std::vector<char, Tallocator1>>& input,
                         std::vector<std::vector<char, Tallocator2>>&       output,
                         const Tint1&                                       length,
                         const size_t                                       nbatch,
                         const fft_precision                                precision,
                         const fft_array_type                               itype,
                         const Tint2&                                       istride,
                         const size_t                                       idist,
                         const fft_array_type                               otype,
                         const Tint3&                                       ostride,
                         const size_t                                       odist,
                         const std::vector<size_t>&                         ioffset,
                         const std::vector<size_t>&                         ooffset)
{
    if(itype == otype)
    {
        switch(itype)
        {
        case fft_array_type_complex_interleaved:
        case fft_array_type_hermitian_interleaved:
            switch(precision)
            {
            case fft_precision_single:
                copy_buffers_1to1(reinterpret_cast<const std::complex<float>*>(input[0].data()),
                                  reinterpret_cast<std::complex<float>*>(output[0].data()),
                                  length,
                                  nbatch,
                                  istride,
                                  idist,
                                  ostride,
                                  odist,
                                  ioffset,
                                  ooffset);
                break;
            case fft_precision_double:
                copy_buffers_1to1(reinterpret_cast<const std::complex<double>*>(input[0].data()),
                                  reinterpret_cast<std::complex<double>*>(output[0].data()),
                                  length,
                                  nbatch,
                                  istride,
                                  idist,
                                  ostride,
                                  odist,
                                  ioffset,
                                  ooffset);
                break;
            }
            break;
        case fft_array_type_real:
        case fft_array_type_complex_planar:
        case fft_array_type_hermitian_planar:
            for(unsigned int idx = 0; idx < input.size(); ++idx)
            {
                switch(precision)
                {
                case fft_precision_single:
                    copy_buffers_1to1(reinterpret_cast<const float*>(input[idx].data()),
                                      reinterpret_cast<float*>(output[idx].data()),
                                      length,
                                      nbatch,
                                      istride,
                                      idist,
                                      ostride,
                                      odist,
                                      ioffset,
                                      ooffset);
                    break;
                case fft_precision_double:
                    copy_buffers_1to1(reinterpret_cast<const double*>(input[idx].data()),
                                      reinterpret_cast<double*>(output[idx].data()),
                                      length,
                                      nbatch,
                                      istride,
                                      idist,
                                      ostride,
                                      odist,
                                      ioffset,
                                      ooffset);
                    break;
                }
            }
            break;
        default:
            throw std::runtime_error("Invalid data type");
        }
    }
    else if((itype == fft_array_type_complex_interleaved && otype == fft_array_type_complex_planar)
            || (itype == fft_array_type_hermitian_interleaved
                && otype == fft_array_type_hermitian_planar))
    {
        // copy 1to2
        switch(precision)
        {
        case fft_precision_single:
            copy_buffers_1to2(reinterpret_cast<const std::complex<float>*>(input[0].data()),
                              reinterpret_cast<float*>(output[0].data()),
                              reinterpret_cast<float*>(output[1].data()),
                              length,
                              nbatch,
                              istride,
                              idist,
                              ostride,
                              odist,
                              ioffset,
                              ooffset);
            break;
        case fft_precision_double:
            copy_buffers_1to2(reinterpret_cast<const std::complex<double>*>(input[0].data()),
                              reinterpret_cast<double*>(output[0].data()),
                              reinterpret_cast<double*>(output[1].data()),
                              length,
                              nbatch,
                              istride,
                              idist,
                              ostride,
                              odist,
                              ioffset,
                              ooffset);
            break;
        }
    }
    else if((itype == fft_array_type_complex_planar && otype == fft_array_type_complex_interleaved)
            || (itype == fft_array_type_hermitian_planar
                && otype == fft_array_type_hermitian_interleaved))
    {
        // copy 2 to 1
        switch(precision)
        {
        case fft_precision_single:
            copy_buffers_2to1(reinterpret_cast<const float*>(input[0].data()),
                              reinterpret_cast<const float*>(input[1].data()),
                              reinterpret_cast<std::complex<float>*>(output[0].data()),
                              length,
                              nbatch,
                              istride,
                              idist,
                              ostride,
                              odist,
                              ioffset,
                              ooffset);
            break;
        case fft_precision_double:
            copy_buffers_2to1(reinterpret_cast<const double*>(input[0].data()),
                              reinterpret_cast<const double*>(input[1].data()),
                              reinterpret_cast<std::complex<double>*>(output[0].data()),
                              length,
                              nbatch,
                              istride,
                              idist,
                              ostride,
                              odist,
                              ioffset,
                              ooffset);
            break;
        }
    }
    else
    {
        throw std::runtime_error("Invalid input and output types.");
    }
}

// unroll arbitrary-dimension copy_buffers into specializations for 1-, 2-, 3-dimensions
template <typename Tallocator1,
          typename Tallocator2,
          typename Tint1,
          typename Tint2,
          typename Tint3>
inline void copy_buffers(const std::vector<std::vector<char, Tallocator1>>& input,
                         std::vector<std::vector<char, Tallocator2>>&       output,
                         const std::vector<Tint1>&                          length,
                         const size_t                                       nbatch,
                         const fft_precision                                precision,
                         const fft_array_type                               itype,
                         const std::vector<Tint2>&                          istride,
                         const size_t                                       idist,
                         const fft_array_type                               otype,
                         const std::vector<Tint3>&                          ostride,
                         const size_t                                       odist,
                         const std::vector<size_t>&                         ioffset,
                         const std::vector<size_t>&                         ooffset)
{
    switch(length.size())
    {
    case 1:
        return copy_buffers(input,
                            output,
                            length[0],
                            nbatch,
                            precision,
                            itype,
                            istride[0],
                            idist,
                            otype,
                            ostride[0],
                            odist,
                            ioffset,
                            ooffset);
    case 2:
        return copy_buffers(input,
                            output,
                            std::make_tuple(length[0], length[1]),
                            nbatch,
                            precision,
                            itype,
                            std::make_tuple(istride[0], istride[1]),
                            idist,
                            otype,
                            std::make_tuple(ostride[0], ostride[1]),
                            odist,
                            ioffset,
                            ooffset);
    case 3:
        return copy_buffers(input,
                            output,
                            std::make_tuple(length[0], length[1], length[2]),
                            nbatch,
                            precision,
                            itype,
                            std::make_tuple(istride[0], istride[1], istride[2]),
                            idist,
                            otype,
                            std::make_tuple(ostride[0], ostride[1], ostride[2]),
                            odist,
                            ioffset,
                            ooffset);
    default:
        abort();
    }
}

// Compute the L-infinity and L-2 distance between two buffers with strides istride and
// length idist between batches to a buffer with strides ostride and length odist between
// batches.  Both buffers are of complex type.

struct VectorNorms
{
    double l_2 = 0.0, l_inf = 0.0;
};

template <typename Tcomplex, typename Tint1, typename Tint2, typename Tint3>
inline VectorNorms distance_1to1_complex(const Tcomplex*                         input,
                                         const Tcomplex*                         output,
                                         const Tint1&                            whole_length,
                                         const size_t                            nbatch,
                                         const Tint2&                            istride,
                                         const size_t                            idist,
                                         const Tint3&                            ostride,
                                         const size_t                            odist,
                                         std::vector<std::pair<size_t, size_t>>& linf_failures,
                                         const double                            linf_cutoff,
                                         const std::vector<size_t>&              ioffset,
                                         const std::vector<size_t>&              ooffset)
{
    double linf = 0.0;
    double l2   = 0.0;

    std::mutex linf_failure_lock;

    const bool idx_equals_odx = istride == ostride && idist == odist;
    size_t     idx_base       = 0;
    size_t     odx_base       = 0;
    auto       partitions     = partition_colmajor(whole_length);
    for(size_t b = 0; b < nbatch; b++, idx_base += idist, odx_base += odist)
    {
#pragma omp parallel for reduction(max : linf) reduction(+ : l2) num_threads(partitions.size())
        for(size_t part = 0; part < partitions.size(); ++part)
        {
            double     cur_linf = 0.0;
            double     cur_l2   = 0.0;
            auto       index    = partitions[part].first;
            const auto length   = partitions[part].second;

            do
            {
                const auto   idx = compute_index(index, istride, idx_base);
                const auto   odx = idx_equals_odx ? idx : compute_index(index, ostride, odx_base);
                const double rdiff
                    = std::abs(output[odx + ooffset[0]].real() - input[idx + ioffset[0]].real());
                cur_linf = std::max(rdiff, cur_linf);
                if(cur_linf > linf_cutoff)
                {
                    std::pair<size_t, size_t> fval(b, idx);
                    linf_failure_lock.lock();
                    linf_failures.push_back(fval);
                    linf_failure_lock.unlock();
                }
                cur_l2 += rdiff * rdiff;

                const double idiff
                    = std::abs(output[odx + ooffset[0]].imag() - input[idx + ioffset[0]].imag());
                cur_linf = std::max(idiff, cur_linf);
                if(cur_linf > linf_cutoff)
                {
                    std::pair<size_t, size_t> fval(b, idx);
                    linf_failure_lock.lock();
                    linf_failures.push_back(fval);
                    linf_failure_lock.unlock();
                }
                cur_l2 += idiff * idiff;

            } while(increment_rowmajor(index, length));
            linf = std::max(linf, cur_linf);
            l2 += cur_l2;
        }
    }
    return {.l_2 = sqrt(l2), .l_inf = linf};
}

// Compute the L-infinity and L-2 distance between two buffers with strides istride and
// length idist between batches to a buffer with strides ostride and length odist between
// batches.  Both buffers are of real type.
template <typename Tfloat, typename Tint1, typename Tint2, typename Tint3>
inline VectorNorms distance_1to1_real(const Tfloat*                           input,
                                      const Tfloat*                           output,
                                      const Tint1&                            whole_length,
                                      const size_t                            nbatch,
                                      const Tint2&                            istride,
                                      const size_t                            idist,
                                      const Tint3&                            ostride,
                                      const size_t                            odist,
                                      std::vector<std::pair<size_t, size_t>>& linf_failures,
                                      const double                            linf_cutoff,
                                      const std::vector<size_t>&              ioffset,
                                      const std::vector<size_t>&              ooffset)
{
    double linf = 0.0;
    double l2   = 0.0;

    std::mutex linf_failure_lock;

    const bool idx_equals_odx = istride == ostride && idist == odist;
    size_t     idx_base       = 0;
    size_t     odx_base       = 0;
    auto       partitions     = partition_rowmajor(whole_length);
    for(size_t b = 0; b < nbatch; b++, idx_base += idist, odx_base += odist)
    {
#pragma omp parallel for reduction(max : linf) reduction(+ : l2) num_threads(partitions.size())
        for(size_t part = 0; part < partitions.size(); ++part)
        {
            double     cur_linf = 0.0;
            double     cur_l2   = 0.0;
            auto       index    = partitions[part].first;
            const auto length   = partitions[part].second;
            do
            {
                const auto   idx  = compute_index(index, istride, idx_base);
                const auto   odx  = idx_equals_odx ? idx : compute_index(index, ostride, odx_base);
                const double diff = std::abs(output[odx + ooffset[0]] - input[idx + ioffset[0]]);
                cur_linf          = std::max(diff, cur_linf);
                if(cur_linf > linf_cutoff)
                {
                    std::pair<size_t, size_t> fval(b, idx);
                    linf_failure_lock.lock();
                    linf_failures.push_back(fval);
                    linf_failure_lock.unlock();
                }
                cur_l2 += diff * diff;

            } while(increment_rowmajor(index, length));
            linf = std::max(linf, cur_linf);
            l2 += cur_l2;
        }
    }
    return {.l_2 = sqrt(l2), .l_inf = linf};
}

// Compute the L-infinity and L-2 distance between two buffers with strides istride and
// length idist between batches to a buffer with strides ostride and length odist between
// batches.  input is complex-interleaved, output is complex-planar.
template <typename Tval, typename Tint1, typename T2, typename T3>
inline VectorNorms distance_1to2(const std::complex<Tval>*               input,
                                 const Tval*                             output0,
                                 const Tval*                             output1,
                                 const Tint1&                            whole_length,
                                 const size_t                            nbatch,
                                 const T2&                               istride,
                                 const size_t                            idist,
                                 const T3&                               ostride,
                                 const size_t                            odist,
                                 std::vector<std::pair<size_t, size_t>>& linf_failures,
                                 const double                            linf_cutoff,
                                 const std::vector<size_t>&              ioffset,
                                 const std::vector<size_t>&              ooffset)
{
    double linf = 0.0;
    double l2   = 0.0;

    std::mutex linf_failure_lock;

    const bool idx_equals_odx = istride == ostride && idist == odist;
    size_t     idx_base       = 0;
    size_t     odx_base       = 0;
    auto       partitions     = partition_rowmajor(whole_length);
    for(size_t b = 0; b < nbatch; b++, idx_base += idist, odx_base += odist)
    {
#pragma omp parallel for reduction(max : linf) reduction(+ : l2) num_threads(partitions.size())
        for(size_t part = 0; part < partitions.size(); ++part)
        {
            double     cur_linf = 0.0;
            double     cur_l2   = 0.0;
            auto       index    = partitions[part].first;
            const auto length   = partitions[part].second;
            do
            {
                const auto   idx = compute_index(index, istride, idx_base);
                const auto   odx = idx_equals_odx ? idx : compute_index(index, ostride, odx_base);
                const double rdiff
                    = std::abs(output0[odx + ooffset[0]] - input[idx + ioffset[0]].real());
                cur_linf = std::max(rdiff, cur_linf);
                if(cur_linf > linf_cutoff)
                {
                    std::pair<size_t, size_t> fval(b, idx);
                    linf_failure_lock.lock();
                    linf_failures.push_back(fval);
                    linf_failure_lock.unlock();
                }
                cur_l2 += rdiff * rdiff;

                const double idiff
                    = std::abs(output1[odx + ooffset[1]] - input[idx + ioffset[0]].imag());
                cur_linf = std::max(idiff, cur_linf);
                if(cur_linf > linf_cutoff)
                {
                    std::pair<size_t, size_t> fval(b, idx);
                    linf_failure_lock.lock();
                    linf_failures.push_back(fval);
                    linf_failure_lock.unlock();
                }
                cur_l2 += idiff * idiff;

            } while(increment_rowmajor(index, length));
            linf = std::max(linf, cur_linf);
            l2 += cur_l2;
        }
    }
    return {.l_2 = sqrt(l2), .l_inf = linf};
}

// Compute the L-inifnity and L-2 distance between two buffers of dimension length and
// with types given by itype, otype, and precision.
template <typename Tallocator1,
          typename Tallocator2,
          typename Tint1,
          typename Tint2,
          typename Tint3>
inline VectorNorms distance(const std::vector<std::vector<char, Tallocator1>>& input,
                            const std::vector<std::vector<char, Tallocator2>>& output,
                            const Tint1&                                       length,
                            const size_t                                       nbatch,
                            const fft_precision                                precision,
                            const fft_array_type                               itype,
                            const Tint2&                                       istride,
                            const size_t                                       idist,
                            const fft_array_type                               otype,
                            const Tint3&                                       ostride,
                            const size_t                                       odist,
                            std::vector<std::pair<size_t, size_t>>&            linf_failures,
                            const double                                       linf_cutoff,
                            const std::vector<size_t>&                         ioffset,
                            const std::vector<size_t>&                         ooffset)
{
    VectorNorms dist;

    if(itype == otype)
    {
        switch(itype)
        {
        case fft_array_type_complex_interleaved:
        case fft_array_type_hermitian_interleaved:
            switch(precision)
            {
            case fft_precision_single:
                dist = distance_1to1_complex(
                    reinterpret_cast<const std::complex<float>*>(input[0].data()),
                    reinterpret_cast<const std::complex<float>*>(output[0].data()),
                    length,
                    nbatch,
                    istride,
                    idist,
                    ostride,
                    odist,
                    linf_failures,
                    linf_cutoff,
                    ioffset,
                    ooffset);
                break;
            case fft_precision_double:
                dist = distance_1to1_complex(
                    reinterpret_cast<const std::complex<double>*>(input[0].data()),
                    reinterpret_cast<const std::complex<double>*>(output[0].data()),
                    length,
                    nbatch,
                    istride,
                    idist,
                    ostride,
                    odist,
                    linf_failures,
                    linf_cutoff,
                    ioffset,
                    ooffset);
                break;
            }
            dist.l_2 *= dist.l_2;
            break;
        case fft_array_type_real:
        case fft_array_type_complex_planar:
        case fft_array_type_hermitian_planar:
            for(unsigned int idx = 0; idx < input.size(); ++idx)
            {
                VectorNorms d;
                switch(precision)
                {
                case fft_precision_single:
                    d = distance_1to1_real(reinterpret_cast<const float*>(input[idx].data()),
                                           reinterpret_cast<const float*>(output[idx].data()),
                                           length,
                                           nbatch,
                                           istride,
                                           idist,
                                           ostride,
                                           odist,
                                           linf_failures,
                                           linf_cutoff,
                                           ioffset,
                                           ooffset);
                    break;
                case fft_precision_double:
                    d = distance_1to1_real(reinterpret_cast<const double*>(input[idx].data()),
                                           reinterpret_cast<const double*>(output[idx].data()),
                                           length,
                                           nbatch,
                                           istride,
                                           idist,
                                           ostride,
                                           odist,
                                           linf_failures,
                                           linf_cutoff,
                                           ioffset,
                                           ooffset);
                    break;
                }
                dist.l_inf = std::max(d.l_inf, dist.l_inf);
                dist.l_2 += d.l_2 * d.l_2;
            }
            break;
        default:
            throw std::runtime_error("Invalid input and output types.");
        }
    }
    else if((itype == fft_array_type_complex_interleaved && otype == fft_array_type_complex_planar)
            || (itype == fft_array_type_hermitian_interleaved
                && otype == fft_array_type_hermitian_planar))
    {
        switch(precision)
        {
        case fft_precision_single:
            dist = distance_1to2(reinterpret_cast<const std::complex<float>*>(input[0].data()),
                                 reinterpret_cast<const float*>(output[0].data()),
                                 reinterpret_cast<const float*>(output[1].data()),
                                 length,
                                 nbatch,
                                 istride,
                                 idist,
                                 ostride,
                                 odist,
                                 linf_failures,
                                 linf_cutoff,
                                 ioffset,
                                 ooffset);
            break;
        case fft_precision_double:
            dist = distance_1to2(reinterpret_cast<const std::complex<double>*>(input[0].data()),
                                 reinterpret_cast<const double*>(output[0].data()),
                                 reinterpret_cast<const double*>(output[1].data()),
                                 length,
                                 nbatch,
                                 istride,
                                 idist,
                                 ostride,
                                 odist,
                                 linf_failures,
                                 linf_cutoff,
                                 ioffset,
                                 ooffset);
            break;
        }
        dist.l_2 *= dist.l_2;
    }
    else if((itype == fft_array_type_complex_planar && otype == fft_array_type_complex_interleaved)
            || (itype == fft_array_type_hermitian_planar
                && otype == fft_array_type_hermitian_interleaved))
    {
        switch(precision)
        {
        case fft_precision_single:
            dist = distance_1to2(reinterpret_cast<const std::complex<float>*>(output[0].data()),
                                 reinterpret_cast<const float*>(input[0].data()),
                                 reinterpret_cast<const float*>(input[1].data()),
                                 length,
                                 nbatch,
                                 ostride,
                                 odist,
                                 istride,
                                 idist,
                                 linf_failures,
                                 linf_cutoff,
                                 ioffset,
                                 ooffset);
            break;
        case fft_precision_double:
            dist = distance_1to2(reinterpret_cast<const std::complex<double>*>(output[0].data()),
                                 reinterpret_cast<const double*>(input[0].data()),
                                 reinterpret_cast<const double*>(input[1].data()),
                                 length,
                                 nbatch,
                                 ostride,
                                 odist,
                                 istride,
                                 idist,
                                 linf_failures,
                                 linf_cutoff,
                                 ioffset,
                                 ooffset);
            break;
        }
        dist.l_2 *= dist.l_2;
    }
    else
    {
        throw std::runtime_error("Invalid input and output types.");
    }
    dist.l_2 = sqrt(dist.l_2);
    return dist;
}

// Unroll arbitrary-dimension distance into specializations for 1-, 2-, 3-dimensions
template <typename Tallocator1,
          typename Tallocator2,
          typename Tint1,
          typename Tint2,
          typename Tint3>
inline VectorNorms distance(const std::vector<std::vector<char, Tallocator1>>& input,
                            const std::vector<std::vector<char, Tallocator2>>& output,
                            const std::vector<Tint1>&                          length,
                            const size_t                                       nbatch,
                            const fft_precision                                precision,
                            const fft_array_type                               itype,
                            const std::vector<Tint2>&                          istride,
                            const size_t                                       idist,
                            const fft_array_type                               otype,
                            const std::vector<Tint3>&                          ostride,
                            const size_t                                       odist,
                            std::vector<std::pair<size_t, size_t>>&            linf_failures,
                            const double                                       linf_cutoff,
                            const std::vector<size_t>&                         ioffset,
                            const std::vector<size_t>&                         ooffset)
{
    switch(length.size())
    {
    case 1:
        return distance(input,
                        output,
                        length[0],
                        nbatch,
                        precision,
                        itype,
                        istride[0],
                        idist,
                        otype,
                        ostride[0],
                        odist,
                        linf_failures,
                        linf_cutoff,
                        ioffset,
                        ooffset);
    case 2:
        return distance(input,
                        output,
                        std::make_tuple(length[0], length[1]),
                        nbatch,
                        precision,
                        itype,
                        std::make_tuple(istride[0], istride[1]),
                        idist,
                        otype,
                        std::make_tuple(ostride[0], ostride[1]),
                        odist,
                        linf_failures,
                        linf_cutoff,
                        ioffset,
                        ooffset);
    case 3:
        return distance(input,
                        output,
                        std::make_tuple(length[0], length[1], length[2]),
                        nbatch,
                        precision,
                        itype,
                        std::make_tuple(istride[0], istride[1], istride[2]),
                        idist,
                        otype,
                        std::make_tuple(ostride[0], ostride[1], ostride[2]),
                        odist,
                        linf_failures,
                        linf_cutoff,
                        ioffset,
                        ooffset);
    default:
        abort();
    }
}

// Compute the L-infinity and L-2 norm of a buffer with strides istride and
// length idist.  Data is std::complex.
template <typename Tcomplex, typename T1, typename T2>
inline VectorNorms norm_complex(const Tcomplex*            input,
                                const T1&                  whole_length,
                                const size_t               nbatch,
                                const T2&                  istride,
                                const size_t               idist,
                                const std::vector<size_t>& offset)
{
    double linf = 0.0;
    double l2   = 0.0;

    size_t idx_base   = 0;
    auto   partitions = partition_rowmajor(whole_length);
    for(size_t b = 0; b < nbatch; b++, idx_base += idist)
    {
#pragma omp parallel for reduction(max : linf) reduction(+ : l2) num_threads(partitions.size())
        for(size_t part = 0; part < partitions.size(); ++part)
        {
            double     cur_linf = 0.0;
            double     cur_l2   = 0.0;
            auto       index    = partitions[part].first;
            const auto length   = partitions[part].second;
            do
            {
                const auto idx = compute_index(index, istride, idx_base);

                const double rval = std::abs(input[idx + offset[0]].real());
                cur_linf          = std::max(rval, cur_linf);
                cur_l2 += rval * rval;

                const double ival = std::abs(input[idx + offset[0]].imag());
                cur_linf          = std::max(ival, cur_linf);
                cur_l2 += ival * ival;

            } while(increment_rowmajor(index, length));
            linf = std::max(linf, cur_linf);
            l2 += cur_l2;
        }
    }
    return {.l_2 = sqrt(l2), .l_inf = linf};
}

// Compute the L-infinity and L-2 norm of abuffer with strides istride and
// length idist.  Data is real-valued.
template <typename Tfloat, typename T1, typename T2>
inline VectorNorms norm_real(const Tfloat*              input,
                             const T1&                  whole_length,
                             const size_t               nbatch,
                             const T2&                  istride,
                             const size_t               idist,
                             const std::vector<size_t>& offset)
{
    double linf = 0.0;
    double l2   = 0.0;

    size_t idx_base   = 0;
    auto   partitions = partition_rowmajor(whole_length);
    for(size_t b = 0; b < nbatch; b++, idx_base += idist)
    {
#pragma omp parallel for reduction(max : linf) reduction(+ : l2) num_threads(partitions.size())
        for(size_t part = 0; part < partitions.size(); ++part)
        {
            double     cur_linf = 0.0;
            double     cur_l2   = 0.0;
            auto       index    = partitions[part].first;
            const auto length   = partitions[part].second;
            do
            {
                const auto   idx = compute_index(index, istride, idx_base);
                const double val = std::abs(input[idx + offset[0]]);
                cur_linf         = std::max(val, cur_linf);
                cur_l2 += val * val;

            } while(increment_rowmajor(index, length));
            linf = std::max(linf, cur_linf);
            l2 += cur_l2;
        }
    }
    return {.l_2 = sqrt(l2), .l_inf = linf};
}

// Compute the L-infinity and L-2 norm of abuffer with strides istride and
// length idist.  Data format is given by precision and itype.
template <typename Tallocator1, typename T1, typename T2>
inline VectorNorms norm(const std::vector<std::vector<char, Tallocator1>>& input,
                        const T1&                                          length,
                        const size_t                                       nbatch,
                        const fft_precision                                precision,
                        const fft_array_type                               itype,
                        const T2&                                          istride,
                        const size_t                                       idist,
                        const std::vector<size_t>&                         offset)
{
    VectorNorms norm;

    switch(itype)
    {
    case fft_array_type_complex_interleaved:
    case fft_array_type_hermitian_interleaved:
        switch(precision)
        {
        case fft_precision_single:
            norm = norm_complex(reinterpret_cast<const std::complex<float>*>(input[0].data()),
                                length,
                                nbatch,
                                istride,
                                idist,
                                offset);
            break;
        case fft_precision_double:
            norm = norm_complex(reinterpret_cast<const std::complex<double>*>(input[0].data()),
                                length,
                                nbatch,
                                istride,
                                idist,
                                offset);
            break;
        }
        norm.l_2 *= norm.l_2;
        break;
    case fft_array_type_real:
    case fft_array_type_complex_planar:
    case fft_array_type_hermitian_planar:
        for(unsigned int idx = 0; idx < input.size(); ++idx)
        {
            VectorNorms n;
            switch(precision)
            {
            case fft_precision_single:
                n = norm_real(reinterpret_cast<const float*>(input[idx].data()),
                              length,
                              nbatch,
                              istride,
                              idist,
                              offset);
                break;
            case fft_precision_double:
                n = norm_real(reinterpret_cast<const double*>(input[idx].data()),
                              length,
                              nbatch,
                              istride,
                              idist,
                              offset);
                break;
            }
            norm.l_inf = std::max(n.l_inf, norm.l_inf);
            norm.l_2 += n.l_2 * n.l_2;
        }
        break;
    default:
        throw std::runtime_error("Invalid data type");
    }

    norm.l_2 = sqrt(norm.l_2);
    return norm;
}

// Unroll arbitrary-dimension norm into specializations for 1-, 2-, 3-dimensions
template <typename Tallocator1, typename T1, typename T2>
inline VectorNorms norm(const std::vector<std::vector<char, Tallocator1>>& input,
                        const std::vector<T1>&                             length,
                        const size_t                                       nbatch,
                        const fft_precision                                precision,
                        const fft_array_type                               type,
                        const std::vector<T2>&                             stride,
                        const size_t                                       dist,
                        const std::vector<size_t>&                         offset)
{
    switch(length.size())
    {
    case 1:
        return norm(input, length[0], nbatch, precision, type, stride[0], dist, offset);
    case 2:
        return norm(input,
                    std::make_tuple(length[0], length[1]),
                    nbatch,
                    precision,
                    type,
                    std::make_tuple(stride[0], stride[1]),
                    dist,
                    offset);
    case 3:
        return norm(input,
                    std::make_tuple(length[0], length[1], length[2]),
                    nbatch,
                    precision,
                    type,
                    std::make_tuple(stride[0], stride[1], stride[2]),
                    dist,
                    offset);
    default:
        abort();
    }
}

// Given a buffer of complex values stored in a vector of chars (or two vectors in the
// case of planar format), impose Hermitian symmetry.
// NB: length is the dimensions of the FFT, not the data layout dimensions.
template <typename Tfloat, typename Tallocator, typename Tsize>
inline void impose_hermitian_symmetry(std::vector<std::vector<char, Tallocator>>& vals,
                                      const std::vector<Tsize>&                   length,
                                      const std::vector<Tsize>&                   istride,
                                      const Tsize                                 idist,
                                      const Tsize                                 nbatch)
{
    switch(vals.size())
    {
    case 1:
    {
        // Complex interleaved data
        for(unsigned int ibatch = 0; ibatch < nbatch; ++ibatch)
        {
            auto data = ((std::complex<Tfloat>*)vals[0].data()) + ibatch * idist;
            switch(length.size())
            {
            case 3:
                if(length[2] % 2 == 0)
                {
                    data[istride[2] * (length[2] / 2)].imag(0.0);
                }

                if(length[0] % 2 == 0 && length[2] % 2 == 0)
                {
                    data[istride[0] * (length[0] / 2) + istride[2] * (length[2] / 2)].imag(0.0);
                }
                if(length[1] % 2 == 0 && length[2] % 2 == 0)
                {
                    data[istride[1] * (length[1] / 2) + istride[2] * (length[2] / 2)].imag(0.0);
                }

                if(length[0] % 2 == 0 && length[1] % 2 == 0 && length[2] % 2 == 0)
                {
                    // clang format off
                    data[istride[0] * (length[0] / 2) + istride[1] * (length[1] / 2)
                         + istride[2] * (length[2] / 2)]
                        .imag(0.0);
                    // clang format off
                }

                // y-axis:
                for(unsigned int j = 1; j < (length[1] + 1) / 2; ++j)
                {
                    data[istride[1] * (length[1] - j)] = std::conj(data[istride[1] * j]);
                }

                if(length[0] % 2 == 0)
                {
                    // y-axis at x-nyquist
                    for(unsigned int j = 1; j < (length[1] + 1) / 2; ++j)
                    {
                        // clang format off
                        data[istride[0] * (length[0] / 2) + istride[1] * (length[1] - j)]
                            = std::conj(data[istride[0] * (length[0] / 2) + istride[1] * j]);
                        // clang format on
                    }
                }

                // x-axis:
                for(unsigned int i = 1; i < (length[0] + 1) / 2; ++i)
                {
                    data[istride[0] * (length[0] - i)] = std::conj(data[istride[0] * i]);
                }

                if(length[1] % 2 == 0)
                {
                    // x-axis at y-nyquist
                    for(unsigned int i = 1; i < (length[0] + 1) / 2; ++i)
                    {
                        // clang format off
                        data[istride[0] * (length[0] - i) + istride[1] * (length[1] / 2)]
                            = std::conj(data[istride[0] * i + istride[1] * (length[1] / 2)]);
                        // clang format on
                    }
                }

                // x-y plane:
                for(unsigned int i = 1; i < (length[0] + 1) / 2; ++i)
                {
                    for(unsigned int j = 1; j < length[1]; ++j)
                    {
                        // clang format off
                        data[istride[0] * (length[0] - i) + istride[1] * (length[1] - j)]
                            = std::conj(data[istride[0] * i + istride[1] * j]);
                        // clang format on
                    }
                }

                if(length[2] % 2 == 0)
                {
                    // x-axis at z-nyquist
                    for(unsigned int i = 1; i < (length[0] + 1) / 2; ++i)
                    {
                        data[istride[0] * (length[0] - i) + istride[2] * (length[2] / 2)]
                            = std::conj(data[istride[0] * i + istride[2] * (length[2] / 2)]);
                    }
                    if(length[1] % 2 == 0)
                    {
                        // x-axis at yz-nyquist
                        for(unsigned int i = 1; i < (length[0] + 1) / 2; ++i)
                        {
                            data[istride[0] * (length[0] - i) + istride[2] * (length[2] / 2)]
                                = std::conj(data[istride[0] * i + istride[2] * (length[2] / 2)]);
                        }
                    }

                    // y-axis: at z-nyquist
                    for(unsigned int j = 1; j < (length[1] + 1) / 2; ++j)
                    {
                        data[istride[1] * (length[1] - j) + istride[2] * (length[2] / 2)]
                            = std::conj(data[istride[1] * j + istride[2] * (length[2] / 2)]);
                    }

                    if(length[0] % 2 == 0)
                    {
                        // y-axis: at xz-nyquist
                        for(unsigned int j = 1; j < (length[1] + 1) / 2; ++j)
                        {
                            // clang format off
                            data[istride[0] * (length[0] / 2) + istride[1] * (length[1] - j)
                                 + istride[2] * (length[2] / 2)]
                                = std::conj(data[istride[0] * (length[0] / 2) + istride[1] * j
                                                 + istride[2] * (length[2] / 2)]);
                            // clang format on
                        }
                    }

                    // x-y plane: at z-nyquist
                    for(unsigned int i = 1; i < (length[0] + 1) / 2; ++i)
                    {
                        for(unsigned int j = 1; j < length[1]; ++j)
                        {
                            // clang format off
                            data[istride[0] * (length[0] - i) + istride[1] * (length[1] - j)
                                 + istride[2] * (length[2] / 2)]
                                = std::conj(data[istride[0] * i + istride[1] * j
                                                 + istride[2] * (length[2] / 2)]);
                            // clang format on
                        }
                    }
                }

                [[fallthrough]];
            case 2:
                if(length[1] % 2 == 0)
                {
                    data[istride[1] * (length[1] / 2)].imag(0.0);
                }

                if(length[0] % 2 == 0 && length[1] % 2 == 0)
                {
                    data[istride[0] * (length[0] / 2) + istride[1] * (length[1] / 2)].imag(0.0);
                }

                for(unsigned int i = 1; i < (length[0] + 1) / 2; ++i)
                {
                    data[istride[0] * (length[0] - i)] = std::conj(data[istride[0] * i]);
                }

                if(length[1] % 2 == 0)
                {
                    for(unsigned int i = 1; i < (length[0] + 1) / 2; ++i)
                    {
                        data[istride[0] * (length[0] - i) + istride[1] * (length[1] / 2)]
                            = std::conj(data[istride[0] * i + istride[1] * (length[1] / 2)]);
                    }
                }

                [[fallthrough]];
            case 1:
                data[0].imag(0.0);

                if(length[0] % 2 == 0)
                {
                    data[istride[0] * (length[0] / 2)].imag(0.0);
                }
                break;

            default:
                throw std::runtime_error("Invalid dimension for imposeHermitianSymmetry");
            }
        }
        break;
    }
    case 2:
    {
        // Complex planar data
        for(unsigned int ibatch = 0; ibatch < nbatch; ++ibatch)
        {
            auto idata = ((Tfloat*)vals[1].data()) + ibatch * idist;
            switch(length.size())
            {
            case 3:
                throw std::runtime_error("Not implemented");
                // TODO: implement
            case 2:
                throw std::runtime_error("Not implemented");
                // TODO: implement
            case 1:
                idata[0] = 0.0;
                if(length[0] % 2 == 0)
                {
                    idata[istride[0] * (length[0] / 2)] = 0.0;
                }
                break;
            default:
                throw std::runtime_error("Invalid dimension for imposeHermitianSymmetry");
            }
        }
        break;
    }
    default:
        throw std::runtime_error("Invalid data type");
    }
}

// Given an array type and transform length, strides, etc, load random floats in [0,1]
// into the input array of floats/doubles or complex floats/doubles, which is stored in a
// vector of chars (or two vectors in the case of planar format).
// lengths are the memory lengths (ie not the transform parameters)
template <typename Tfloat, typename Tallocator, typename Tint1>
inline void set_input(std::vector<std::vector<char, Tallocator>>& input,
                      const fft_array_type                        itype,
                      const Tint1&                                whole_length,
                      const Tint1&                                istride,
                      const size_t                                idist,
                      const size_t                                nbatch)
{
    switch(itype)
    {
    case fft_array_type_complex_interleaved:
    case fft_array_type_hermitian_interleaved:
    {
        auto   idata      = (std::complex<Tfloat>*)input[0].data();
        size_t i_base     = 0;
        auto   partitions = partition_rowmajor(whole_length);
        for(unsigned int b = 0; b < nbatch; b++, i_base += idist)
        {
#pragma omp parallel for num_threads(partitions.size())
            for(size_t part = 0; part < partitions.size(); ++part)
            {
                auto         index  = partitions[part].first;
                const auto   length = partitions[part].second;
                std::mt19937 gen(compute_index(index, istride, i_base));
                do
                {
                    const auto                 i = compute_index(index, istride, i_base);
                    const Tfloat               x = (Tfloat)gen() / (Tfloat)gen.max();
                    const Tfloat               y = (Tfloat)gen() / (Tfloat)gen.max();
                    const std::complex<Tfloat> val(x, y);
                    idata[i] = val;
                } while(increment_rowmajor(index, length));
            }
        }
        break;
    }
    case fft_array_type_complex_planar:
    case fft_array_type_hermitian_planar:
    {
        auto   ireal      = (Tfloat*)input[0].data();
        auto   iimag      = (Tfloat*)input[1].data();
        size_t i_base     = 0;
        auto   partitions = partition_rowmajor(whole_length);
        for(unsigned int b = 0; b < nbatch; b++, i_base += idist)
        {
#pragma omp parallel for num_threads(partitions.size())
            for(size_t part = 0; part < partitions.size(); ++part)
            {
                auto         index  = partitions[part].first;
                const auto   length = partitions[part].second;
                std::mt19937 gen(compute_index(index, istride, i_base));
                do
                {
                    const auto                 i = compute_index(index, istride, i_base);
                    const std::complex<Tfloat> val((Tfloat)gen() / (Tfloat)gen.max(),
                                                   (Tfloat)gen() / (Tfloat)gen.max());
                    ireal[i] = val.real();
                    iimag[i] = val.imag();
                } while(increment_rowmajor(index, length));
            }
        }
        break;
    }
    case fft_array_type_real:
    {
        auto   idata      = (Tfloat*)input[0].data();
        size_t i_base     = 0;
        auto   partitions = partition_rowmajor(whole_length);
        for(unsigned int b = 0; b < nbatch; b++, i_base += idist)
        {
#pragma omp parallel for num_threads(partitions.size())
            for(size_t part = 0; part < partitions.size(); ++part)
            {
                auto         index  = partitions[part].first;
                const auto   length = partitions[part].second;
                std::mt19937 gen(compute_index(index, istride, i_base));
                do
                {
                    const auto   i   = compute_index(index, istride, i_base);
                    const Tfloat val = (Tfloat)gen() / (Tfloat)gen.max();
                    idata[i]         = val;
                } while(increment_rowmajor(index, length));
            }
        }
        break;
    }
    default:
        throw std::runtime_error("Input layout format not yet supported");
    }
}

// unroll set_input for dimension 1, 2, 3
template <typename Tfloat, typename Tallocator>
inline void set_input(std::vector<std::vector<char, Tallocator>>& input,
                      const fft_array_type                        itype,
                      const std::vector<size_t>&                  length,
                      const std::vector<size_t>&                  istride,
                      const size_t                                idist,
                      const size_t                                nbatch)
{
    switch(length.size())
    {
    case 1:
        set_input<Tfloat>(input, itype, length[0], istride[0], idist, nbatch);
        break;
    case 2:
        set_input<Tfloat>(input,
                          itype,
                          std::make_tuple(length[0], length[1]),
                          std::make_tuple(istride[0], istride[1]),
                          idist,
                          nbatch);
        break;
    case 3:
        set_input<Tfloat>(input,
                          itype,
                          std::make_tuple(length[0], length[1], length[2]),
                          std::make_tuple(istride[0], istride[1], istride[2]),
                          idist,
                          nbatch);
        break;
    default:
        abort();
    }
}

// Given a data type and precision, the distance between batches, and
// the batch size, allocate the required host buffer(s).
template <typename Allocator = std::allocator<char>>
inline std::vector<std::vector<char, Allocator>> allocate_host_buffer(
    const fft_precision precision, const fft_array_type type, const std::vector<size_t>& size)
{
    std::vector<std::vector<char, Allocator>> buffers(size.size());
    for(unsigned int i = 0; i < size.size(); ++i)
    {
        buffers[i].resize(size[i] * var_size<size_t>(precision, type));
    }
    return buffers;
}

// Given a data type and dimensions, fill the buffer, imposing Hermitian symmetry if
// necessary.
// NB: length is the logical size of the FFT, and not necessarily the data dimensions
template <typename Allocator = std::allocator<char>>
inline void compute_input(const fft_params&                          params,
                          std::vector<std::vector<char, Allocator>>& input)
{
    switch(params.precision)
    {
    case fft_precision_double:
        set_input<double>(
            input, params.itype, params.ilength(), params.istride, params.idist, params.nbatch);
        break;
    case fft_precision_single:
        set_input<float>(
            input, params.itype, params.ilength(), params.istride, params.idist, params.nbatch);
        break;
    }

    if(params.itype == fft_array_type_hermitian_interleaved
       || params.itype == fft_array_type_hermitian_planar)
    {
        switch(params.precision)
        {
        case fft_precision_double:
            impose_hermitian_symmetry<double>(
                input, params.length, params.istride, params.idist, params.nbatch);
            break;
        case fft_precision_single:
            impose_hermitian_symmetry<float>(
                input, params.length, params.istride, params.idist, params.nbatch);
            break;
        }
    }
}

// Check if the required buffers fit in the device vram.
inline bool vram_fits_problem(const size_t prob_size, int deviceId = 0)
{
    // We keep a small margin of error for fitting the problem into vram:
    const size_t extra = 1 << 20;

    // Check free and total available memory:
    size_t free   = 0;
    size_t total  = 0;
    auto   retval = hipMemGetInfo(&free, &total);

    if(retval != hipSuccess)
        throw std::runtime_error("Failure in hipMemGetInfo");

    if(total < prob_size + extra)
        return false;

    if(free < prob_size + extra)
        return false;

    return true;
}

// Computes the twiddle table VRAM footprint for r2c/c2r transforms.
// This function will return 0 for the other transform types, since
// the VRAM footprint in rocFFT is negligible for the other cases.
inline size_t twiddle_table_vram_footprint(const fft_params& params)
{
    size_t vram_footprint = 0;

    // Add vram footprint from real/complex even twiddle buffer size.
    if(params.transform_type == fft_transform_type_real_forward
       || params.transform_type == fft_transform_type_real_inverse)
    {
        const auto realdim = params.length.back();
        if(realdim % 2 == 0)
        {
            const auto complex_size = params.precision == fft_precision_single ? 8 : 16;
            // even length twiddle size is 1/4 of the real size, but
            // in complex elements
            vram_footprint += realdim * complex_size / 4;
        }
    }

    return vram_footprint;
}

#endif
