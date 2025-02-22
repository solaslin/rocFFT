// Copyright (C) 2021 - 2022 Advanced Micro Devices, Inc. All rights reserved.
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

#include "../../shared/environment.h"
#include "library_path.h"
#include "rtc.h"

#ifdef WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <fcntl.h>
#include <poll.h>
#include <spawn.h>
#include <stdio.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

#if __has_include(<filesystem>)
#include <filesystem>
#else
#include <experimental/filesystem>
namespace std
{
    namespace filesystem = experimental::filesystem;
}
#endif

namespace fs = std::filesystem;

#ifdef WIN32
static const char* HELPER_EXE = "rocfft_rtc_helper.exe";
#else
static const char* HELPER_EXE = "rocfft_rtc_helper";
typedef int        file_handle_type;
#endif

static fs::path find_rtc_helper()
{
    // candidate directories for the helper
    std::vector<fs::path> helper_dirs;

    auto var = rocfft_getenv("ROCFFT_RTC_PROCESS_HELPER");
    if(!var.empty())
        return var;

    fs::path library_path = get_library_path();
    if(!library_path.empty())
    {
        // try same dir as library
        fs::path library_parent_path = library_path.parent_path();
        helper_dirs.push_back(library_parent_path);

        // try bin dir, one dir up from library
        fs::path bin_path = library_parent_path.parent_path() / "bin";
        helper_dirs.push_back(bin_path);

        // look for helper in the candidate directories
        for(const auto& dir : helper_dirs)
        {
            auto helper_path = dir / HELPER_EXE;
            if(fs::exists(helper_path))
                return helper_path;
        }
    }
    throw std::runtime_error("unable to find rtc helper");
}

// simple RAII wrapper around file handles
struct file_handle_wrapper
{
#ifdef WIN32
    typedef HANDLE                    file_handle_type;
    static constexpr file_handle_type FILE_HANDLE_INVALID = 0;
#else
    typedef int                       file_handle_type;
    static constexpr file_handle_type FILE_HANDLE_INVALID = -1;
#endif

    file_handle_wrapper() = default;
    explicit file_handle_wrapper(file_handle_type fd)
        : fd(fd)
    {
    }
    // no copies, moves
    file_handle_wrapper(const file_handle_wrapper&) = delete;
    file_handle_wrapper(file_handle_wrapper&&)      = delete;
    void operator=(const file_handle_wrapper&) = delete;
    void operator=(file_handle_wrapper&&) = delete;
    ~file_handle_wrapper()
    {
        this->close();
    }
    void close()
    {
        if(fd == FILE_HANDLE_INVALID)
            return;
#ifdef WIN32
        CloseHandle(fd);
#else
        ::close(fd);
#endif
        fd = FILE_HANDLE_INVALID;
    }
    operator file_handle_type() const
    {
        return fd;
    }
    file_handle_type fd = FILE_HANDLE_INVALID;
};

std::vector<char> RTCKernel::compile_subprocess(const std::string& kernel_src,
                                                const std::string& gpu_arch)
{
    static std::string  rtc_helper_exe = find_rtc_helper().string();
    std::vector<char>   code;
    bool                subprocess_failed = false;
    static const size_t READ_CHUNK_SIZE   = 1024;

#ifdef WIN32
    file_handle_wrapper child_stdin_read;
    file_handle_wrapper child_stdin_write;
    file_handle_wrapper child_stdout_read;
    file_handle_wrapper child_stdout_write;

    // create a named pipe with overlapped flag for async i/o
    auto make_overlapped_pipe = [](HANDLE& read, HANDLE& write) {
        // assemble pipe name from process id, threadid, variable
        // addresses - these pipes are closed by the wrapper once
        // parent scope exits so this should be unique enough
        char        buf[200];
        std::string pipe_name = "\\\\.\\pipe\\rocfft_rtc_subprocess_";
        snprintf(
            buf, 200, "%lx_%lx_%p_%p", GetCurrentProcessId(), GetCurrentThreadId(), &read, &write);
        pipe_name += buf;

        SECURITY_ATTRIBUTES sa;
        sa.nLength              = sizeof(SECURITY_ATTRIBUTES);
        sa.bInheritHandle       = TRUE;
        sa.lpSecurityDescriptor = NULL;

        static const DWORD pipe_size = 4096;
        // create read end of pipe
        read = CreateNamedPipeA(pipe_name.c_str(),
                                PIPE_ACCESS_INBOUND | FILE_FLAG_OVERLAPPED,
                                PIPE_TYPE_BYTE | PIPE_WAIT,
                                1,
                                pipe_size,
                                pipe_size,
                                0,
                                &sa);
        if(read == INVALID_HANDLE_VALUE)
        {
            throw std::runtime_error(std::string("failed to create read pipe: ")
                                     + std::to_string(GetLastError()));
        }

        // create write end of pipe
        write = CreateFileA(pipe_name.c_str(),
                            GENERIC_WRITE,
                            0,
                            &sa,
                            OPEN_EXISTING,
                            FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED,
                            nullptr);

        if(write == INVALID_HANDLE_VALUE)
        {
            throw std::runtime_error(std::string("failed to create write pipe: ")
                                     + std::to_string(GetLastError()));
        }
    };

    make_overlapped_pipe(child_stdin_read.fd, child_stdin_write.fd);
    make_overlapped_pipe(child_stdout_read.fd, child_stdout_write.fd);

    // uninherit pipe ends the child does not need
    if(!SetHandleInformation(child_stdout_read, HANDLE_FLAG_INHERIT, 0))
        throw std::runtime_error("cannot uninherit stdout read");
    if(!SetHandleInformation(child_stdin_write, HANDLE_FLAG_INHERIT, 0))
        throw std::runtime_error("cannot uninherit stdin write");

    STARTUPINFO si = {0};
    si.cb          = sizeof(STARTUPINFO);
    si.dwFlags     = STARTF_USESTDHANDLES;
    si.hStdInput   = child_stdin_read;
    si.hStdOutput  = child_stdout_write;

    std::string cmdline = rtc_helper_exe + " " + gpu_arch;

    PROCESS_INFORMATION pi;
    if(!CreateProcessA(rtc_helper_exe.c_str(),
                       const_cast<char*>(cmdline.c_str()),
                       nullptr,
                       nullptr,
                       TRUE,
                       0,
                       nullptr,
                       nullptr,
                       &si,
                       &pi))
        throw std::runtime_error("failed to create process");

    file_handle_wrapper hProcess(pi.hProcess);
    file_handle_wrapper hThread(pi.hThread);

    // overlapped I/O handles and structs
    file_handle_wrapper stdin_write_event{CreateEventA(NULL, TRUE, FALSE, NULL)};
    file_handle_wrapper stdout_read_event{CreateEventA(NULL, TRUE, FALSE, NULL)};
    OVERLAPPED          stdin_write_ovl = {0};
    stdin_write_ovl.hEvent              = stdin_write_event;
    OVERLAPPED stdout_read_ovl          = {0};
    stdout_read_ovl.hEvent              = stdout_read_event;

    HANDLE handles[3];
    handles[0] = hProcess;
    handles[1] = stdout_read_event;
    handles[2] = stdin_write_event;

    size_t total_bytes_written = 0;
    size_t total_bytes_read    = 0;

    // do initial async read + write to child
    if(!WriteFile(child_stdin_write, kernel_src.data(), kernel_src.size(), NULL, &stdin_write_ovl)
       && GetLastError() != ERROR_IO_PENDING)
    {
        throw std::runtime_error("failed to write initial input to child");
    }

    code.resize(READ_CHUNK_SIZE);
    if(!ReadFile(child_stdout_read, code.data(), READ_CHUNK_SIZE, nullptr, &stdout_read_ovl)
       && GetLastError() != ERROR_IO_PENDING)
        throw std::runtime_error("failed to read initial data from child");

    DWORD wait_handle_count = 3;
    for(;;)
    {
        auto wait_result = WaitForMultipleObjects(wait_handle_count, handles, FALSE, INFINITE);
        if(wait_result == WAIT_OBJECT_0)
        {
            // process died unexpectedly - we should be able to finish I/O first
            break;
        }
        else if(wait_result == WAIT_OBJECT_0 + 1)
        {
            // read handle is ready
            DWORD bytes_read = 0;
            if(GetOverlappedResult(child_stdout_read, &stdout_read_ovl, &bytes_read, FALSE))
            {
                total_bytes_read += bytes_read;
                code.resize(total_bytes_read);
            }
            if(GetLastError() == ERROR_HANDLE_EOF)
                break;

            // allocate space for the read
            code.resize(total_bytes_read + READ_CHUNK_SIZE);
            if(!ReadFile(child_stdout_read,
                         code.data() + total_bytes_read,
                         READ_CHUNK_SIZE,
                         nullptr,
                         &stdout_read_ovl))
            {
                auto err = GetLastError();
                // end of file
                if(err == ERROR_BROKEN_PIPE)
                {
                    code.resize(total_bytes_read);
                    break;
                }
                // normally expect ERROR_IO_PENDING for any async read
                else if(err != ERROR_IO_PENDING)
                    throw std::runtime_error(std::string("failed to read data from child: ")
                                             + std::to_string(GetLastError()));
            }
        }
        else if(wait_result == WAIT_OBJECT_0 + 2)
        {
            // write handle is ready
            DWORD bytes_written = 0;
            if(GetOverlappedResult(child_stdin_write, &stdin_write_ovl, &bytes_written, FALSE))
            {
                total_bytes_written += bytes_written;
            }

            if(total_bytes_written >= kernel_src.size())
            {
                // done writing, don't add write handle to wait anymore
                --wait_handle_count;
                // close child's stdin so it knows we're done writing
                child_stdin_write.close();
                child_stdin_read.close();
                child_stdout_write.close();
            }
            else
            {
                // write remaining data to child
                if(!WriteFile(child_stdin_write,
                              kernel_src.data() + total_bytes_written,
                              kernel_src.size() - total_bytes_written,
                              NULL,
                              &stdin_write_ovl)
                   && GetLastError() != ERROR_IO_PENDING)
                {
                    throw std::runtime_error("failed to write input to child");
                }
            }
        }
    }
    child_stdout_read.close();

    // wait for process to terminate
    if(WaitForSingleObject(hProcess, INFINITE) != WAIT_OBJECT_0)
        throw std::runtime_error("failed to wait for child process");

    DWORD exit_code = 0;
    if(!GetExitCodeProcess(hProcess, &exit_code))
        throw std::runtime_error("failed to get child exit code");
    subprocess_failed = exit_code != 0;
#else
    int stdin_fds[2] = {-1, -1};
    if(pipe2(stdin_fds, O_CLOEXEC) != 0)
        throw std::runtime_error("failed to create stdin pipe");
    file_handle_wrapper child_stdin_read(stdin_fds[0]);
    file_handle_wrapper child_stdin_write(stdin_fds[1]);

    int stdout_fds[2] = {-1, -1};
    if(pipe2(stdout_fds, O_CLOEXEC) != 0)
        throw std::runtime_error("failed to create stdout pipe");
    file_handle_wrapper child_stdout_read(stdout_fds[0]);
    file_handle_wrapper child_stdout_write(stdout_fds[1]);

    pid_t pid = 0;
    char* argv[]
        = {const_cast<char*>(rtc_helper_exe.c_str()), const_cast<char*>(gpu_arch.c_str()), 0};
    char* envp[] = {nullptr};

    // set up child's stdin/stdout
    posix_spawn_file_actions_t spawn_file_actions;
    posix_spawn_file_actions_init(&spawn_file_actions);
    posix_spawn_file_actions_adddup2(&spawn_file_actions, child_stdin_read, STDIN_FILENO);
    posix_spawn_file_actions_adddup2(&spawn_file_actions, child_stdout_write, STDOUT_FILENO);

    int spawn_result
        = posix_spawn(&pid, rtc_helper_exe.c_str(), &spawn_file_actions, nullptr, argv, envp);
    if(spawn_result != 0)
    {
        throw std::runtime_error("failed to spawn child process");
    }

    child_stdin_read.close();
    child_stdout_write.close();

    // poll read and write fd's
    pollfd fds[2];
    fds[0].fd     = child_stdin_write;
    fds[0].events = POLLOUT;
    fds[1].fd     = child_stdout_read;
    fds[1].events = POLLIN;

    ssize_t total_bytes_written = 0;
    while(poll(fds, 2, 1000) >= 0)
    {
        // error conditions on fds, break
        if(fds[0].revents & POLLERR || fds[1].revents & POLLERR)
            break;

        // write kernel source to child
        if(fds[0].revents & POLLOUT)
        {
            // write a page at a time to prevent the write from
            // blocking
            size_t  bytes_to_write = std::min<size_t>(kernel_src.size(), 4096);
            ssize_t bytes_written
                = write(child_stdin_write, kernel_src.data() + total_bytes_written, bytes_to_write);
            if(bytes_written <= 0)
                break;
            total_bytes_written += bytes_written;

            if(total_bytes_written >= static_cast<ssize_t>(kernel_src.size()))
            {
                // close child's stdin so it knows we're done writing
                child_stdin_write.close();
                fds[0].events = 0;
            }
        }

        // read code object back from child
        if(fds[1].revents & POLLIN)
        {
            size_t code_written_bytes = code.size();
            code.resize(code_written_bytes + READ_CHUNK_SIZE);
            auto bytes_read
                = read(child_stdout_read, code.data() + code_written_bytes, READ_CHUNK_SIZE);
            // resize code to number of bytes actually written
            code.resize(code_written_bytes + (bytes_read > 0 ? bytes_read : 0));
            if(bytes_read == -1)
            {
                // read error, data is probably not correct
                throw std::runtime_error("failed read data from child process");
            }
            if(bytes_read == 0)
            {
                // end of file
                break;
            }
        }
        else if(fds[1].revents & POLLHUP)
        {
            // no data to read, other side hung up
            break;
        }
    }

    child_stdout_read.close();

    // wait for the child process
    int wait_status = 0;
    if(waitpid(pid, &wait_status, 0) != pid)
        throw std::runtime_error("failed to wait for child process");
    subprocess_failed = WIFSIGNALED(wait_status) || WEXITSTATUS(wait_status) != 0;
#endif

    if(code.empty())
    {
        throw std::runtime_error("child process failed to produce code");
    }

    if(subprocess_failed)
    {
        // stdout of process is actually an error message, so throw that
        throw std::runtime_error(std::string(code.data(), code.size()));
    }
    return code;
}
