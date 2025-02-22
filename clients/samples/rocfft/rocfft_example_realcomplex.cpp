// Copyright (C) 2019 - 2022 Advanced Micro Devices, Inc. All rights reserved.
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

#include <boost/program_options.hpp>
namespace po = boost::program_options;
#include <complex>
#include <functional>
#include <iostream>
#include <numeric>
#include <vector>

#include <hip/hip_runtime_api.h>
#include <hip/hip_vector_types.h>
#include <rocfft.h>

#include "examplekernels.h"
#include "exampleutils.h"

int main(int argc, char* argv[])
{
    std::cout << "rocfft double-precision real/complex transform\n" << std::endl;

    // Length of transform:
    std::vector<size_t> length = {8};

    // Gpu device id:
    int deviceId = 0;

    // Command-line options:
    // clang-format off
    po::options_description desc("rocfft sample command line options");
    desc.add_options()("help,h", "produces this help message")
        ("device", po::value<int>(&deviceId)->default_value(0), "Select a specific device id")
        ("outofplace,o", "Perform an out-of-place transform")
        ("inverse,i", "Perform an inverse transform")
        ("length", po::value<std::vector<size_t>>(&length)->multitoken(),
         "Lengths of the transform separated by spaces");
    // clang-format on
    po::variables_map vm;
    po::store(po::parse_command_line(argc, argv, desc), vm);
    po::notify(vm);
    if(vm.count("help"))
    {
        std::cout << desc << std::endl;
        return 0;
    }

    // Placeness for the transform
    rocfft_setup();
    const rocfft_result_placement place
        = vm.count("outofplace") ? rocfft_placement_notinplace : rocfft_placement_inplace;
    const bool inplace = place == rocfft_placement_inplace;

    // Direction of transform
    const rocfft_transform_type direction = vm.count("inverse")
                                                ? rocfft_transform_type_real_inverse
                                                : rocfft_transform_type_real_forward;
    const bool                  forward   = direction == rocfft_transform_type_real_forward;

    // Set up the strides and buffer size for the real values:
    std::vector<size_t> rstride = {1};
    for(unsigned int i = 1; i < length.size(); ++i)
    {
        // In-place transforms need space for two extra real values in the contiguous
        // direction.
        auto val = (length[i - 1] + ((inplace && i == 1) ? 2 : 0)) * rstride[i - 1];
        rstride.push_back(val);
    }
    // NB: not tight, but hey
    const size_t        real_size = length[length.size() - 1] * rstride[rstride.size() - 1];
    std::vector<double> rdata(real_size); // host storage

    // The complex data length is half + 1 of the real data length in the contiguous
    // dimensions.  Since rocFFT is column-major, this is the first index.
    std::vector<size_t> clength = length;
    clength[0]                  = clength[0] / 2 + 1;
    std::vector<size_t> cstride = {1};
    for(unsigned int i = 1; i < clength.size(); ++i)
    {
        cstride.push_back(clength[i - 1] * cstride[i - 1]);
    }
    const size_t complex_size = clength[clength.size() - 1] * cstride[cstride.size() - 1];
    std::vector<std::complex<double>> cdata(complex_size); // host storage

    // Based on the direction, we set the input and output parameters appropriately.
    const size_t isize  = forward ? real_size : complex_size;
    const size_t ibytes = isize * (forward ? sizeof(double) : sizeof(std::complex<double>));
    const std::vector<size_t> ilength = forward ? length : clength;
    const std::vector<size_t> istride = forward ? rstride : cstride;

    const size_t osize  = forward ? complex_size : real_size;
    const size_t obytes = osize * (forward ? sizeof(std::complex<double>) : sizeof(double));
    const std::vector<size_t> olength = forward ? clength : length;
    const std::vector<size_t> ostride = forward ? cstride : rstride;

    // Print information about the transform:
    std::cout << "direction: ";
    if(forward)
        std::cout << "forward\n";
    else
        std::cout << "inverse\n";
    std::cout << "length:";
    for(const auto i : length)
        std::cout << " " << i;
    std::cout << "\n";
    if(inplace)
        std::cout << "in-place transform\n";
    else
        std::cout << "out-of-place transform\n";
    std::cout << "deviceID: " << deviceId << "\n";
    std::cout << "input length:";
    for(auto i : ilength)
        std::cout << " " << i;
    std::cout << "\n";
    std::cout << "input buffer stride:";
    for(auto i : istride)
        std::cout << " " << i;
    std::cout << "\n";
    std::cout << "input buffer size: " << ibytes << "\n";

    std::cout << "output length:";
    for(auto i : olength)
        std::cout << " " << i;
    std::cout << "\n";
    std::cout << "output buffer stride:";
    for(auto i : ostride)
        std::cout << " " << i;
    std::cout << "\n";
    std::cout << "output buffer size: " << obytes << "\n";
    std::cout << std::endl;

    // Set the device:
    hipSetDevice(deviceId);

    // Create HIP device object and initialize data
    // Kernels are provided in examplekernels.h
    void*      gpu_in     = NULL;
    hipError_t hip_status = hipMalloc(&gpu_in, inplace ? std::max(ibytes, obytes) : ibytes);
    if(hip_status != hipSuccess)
        throw std::runtime_error("device error");

    if(forward)
    {
        initreal_cm(length, istride, gpu_in);
    }
    else
    {
        init_hermitiancomplex_cm(length, ilength, istride, gpu_in);
    }

    // Print the input:
    std::cout << "input:\n";
    if(forward)
    {
        hipMemcpy(rdata.data(), gpu_in, ibytes, hipMemcpyDeviceToHost);
        printbuffer_cm(rdata, ilength, istride, 1, isize);
    }
    else
    {
        hipMemcpy(cdata.data(), gpu_in, ibytes, hipMemcpyDeviceToHost);
        printbuffer_cm(cdata, ilength, istride, 1, isize);

        // Check that the buffer is Hermitian symmetric:
        check_symmetry_cm(cdata, length, istride, 1, isize);
    }

    // rocfft_status can be used to capture API status info
    rocfft_status rc = rocfft_status_success;

    // Create the a descrition struct to set data layout:
    rocfft_plan_description gpu_description = NULL;
    rc                                      = rocfft_plan_description_create(&gpu_description);
    if(rc != rocfft_status_success)
        throw std::runtime_error("failed to create plan description");

    rc = rocfft_plan_description_set_data_layout(
        gpu_description,
        // input data format:
        forward ? rocfft_array_type_real : rocfft_array_type_hermitian_interleaved,
        // output data format:
        forward ? rocfft_array_type_hermitian_interleaved : rocfft_array_type_real,
        NULL,
        NULL,
        istride.size(), // input stride length
        istride.data(), // input stride data
        0, // input batch distance
        ostride.size(), // output stride length
        ostride.data(), // output stride data
        0); // ouptut batch distance
    if(rc != rocfft_status_success)
        throw std::runtime_error("failed to set data layout");

    // We can also pass "NULL" instead of a description; rocFFT will use reasonable
    // default parameters.  If the data isn't contiguous, we need to set strides, etc,
    // using the description.

    // Create the FFT plan:
    rocfft_plan gpu_plan = NULL;
    rc                   = rocfft_plan_create(&gpu_plan,
                            place,
                            direction,
                            rocfft_precision_double,
                            length.size(), // Dimension
                            length.data(), // lengths
                            1, // Number of transforms
                            gpu_description); // Description
    if(rc != rocfft_status_success)
        throw std::runtime_error("failed to create plan");

    // Get the execution info for the fft plan (in particular, work memory requirements):
    rocfft_execution_info planinfo = NULL;
    rc                             = rocfft_execution_info_create(&planinfo);
    if(rc != rocfft_status_success)
        throw std::runtime_error("failed to create execution info");

    size_t workbuffersize = 0;
    rc                    = rocfft_plan_get_work_buffer_size(gpu_plan, &workbuffersize);
    if(rc != rocfft_status_success)
        throw std::runtime_error("failed to get work buffer size");

    // If the transform requires work memory, allocate a work buffer:
    void* wbuffer = NULL;
    if(workbuffersize > 0)
    {
        hip_status = hipMalloc(&wbuffer, workbuffersize);
        if(hip_status != hipSuccess)
            throw std::runtime_error("hipMalloc failed");

        rc = rocfft_execution_info_set_work_buffer(planinfo, wbuffer, workbuffersize);
        if(rc != rocfft_status_success)
            throw std::runtime_error("failed to set work buffer");
    }

    // If the transform is out-of-place, allocate the output buffer as well:
    void* gpu_out = inplace ? gpu_in : NULL;
    if(!inplace)
    {
        hip_status = hipMalloc(&gpu_out, obytes);
        if(hip_status != hipSuccess)
            throw std::runtime_error("hipMalloc failed");
    }

    // Execute the GPU transform:
    rc = rocfft_execute(gpu_plan, // plan
                        (void**)&gpu_in, // in_buffer
                        (void**)&gpu_out, // out_buffer
                        planinfo); // execution info
    if(rc != rocfft_status_success)
        throw std::runtime_error("failed to execute");

    // Get the output from the device and print to cout:
    std::cout << "output:\n";
    if(forward)
    {
        hipMemcpy(cdata.data(), gpu_out, obytes, hipMemcpyDeviceToHost);
        printbuffer_cm(cdata, olength, ostride, 1, osize);
    }
    else
    {
        hipMemcpy(rdata.data(), gpu_out, obytes, hipMemcpyDeviceToHost);
        printbuffer_cm(rdata, olength, ostride, 1, osize);
    }

    // Clean up: free GPU memory:
    hipFree(gpu_in);
    if(!inplace)
    {
        hipFree(gpu_out);
    }
    if(wbuffer != NULL)
    {
        hipFree(wbuffer);
    }

    // Clean up: destroy plans:
    rocfft_execution_info_destroy(planinfo);
    rocfft_plan_description_destroy(gpu_description);
    rocfft_plan_destroy(gpu_plan);

    rocfft_cleanup();
    return 0;
}
