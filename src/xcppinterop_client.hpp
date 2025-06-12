#include <chrono>
#include <fcntl.h>
#include <signal.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>

#include <iostream>

using Args = std::vector<const char*>;


#include "xeus-cpp/xshared_memory.hpp"

namespace xcpp
{
    class CppInterOpClient
    {
    private:

        SharedMemoryBuffer* m_shared_buffer;
        int m_shm_fd;
        std::string m_shm_name;
        pid_t m_child_pid;
        bool m_initialized;

    public:

        CppInterOpClient()
            : m_shared_buffer(nullptr)
            , m_shm_fd(-1)
            , m_child_pid(-1)
            , m_initialized(false)
        {
            // Generate unique shared memory name
            m_shm_name = "/xcpp_shm_" + std::to_string(getpid());
        }

        ~CppInterOpClient()
        {
            std::cerr << "[~CppInterOpClient] Cleaning up...\n";
            cleanup();
        }

        bool initialize()
        {
            // Create shared memory
            shm_unlink(m_shm_name.c_str());
            m_shm_fd = shm_open(m_shm_name.c_str(), O_CREAT | O_RDWR | O_EXCL, 0666);
            if (m_shm_fd == -1)
            {
                std::cerr << "Failed to create shared memory" << std::endl;
                return false;
            }

            // Set size
            if (ftruncate(m_shm_fd, sizeof(SharedMemoryBuffer)) == -1)
            {
                std::cerr << "Failed to set shared memory size" << std::endl;
                return false;
            }

            // Map shared memory
            m_shared_buffer = static_cast<SharedMemoryBuffer*>(
                mmap(nullptr, sizeof(SharedMemoryBuffer), PROT_READ | PROT_WRITE, MAP_SHARED, m_shm_fd, 0)
            );

            if (m_shared_buffer == MAP_FAILED)
            {
                std::cerr << "Failed to map shared memory" << std::endl;
                return false;
            }

            // Initialize shared buffer
            m_shared_buffer->reset();

            // Fork and exec the CppInterOp process, redirecting child stdout/stderr to parent's
            int pipefd[2];
            if (pipe(pipefd) == -1)
            {
                std::cerr << "Failed to create pipe for child logs" << std::endl;
                return false;
            }

            // ********************
            // This code block can be simplified more. Currently, it also fetches the logs of the child process.
            m_child_pid = fork();
            if (m_child_pid == -1)
            {
                std::cerr << "Failed to fork CppInterOp process" << std::endl;
                close(pipefd[0]);
                close(pipefd[1]);
                return false;
            }

            if (m_child_pid == 0)
            {
                pid_t parent_pid = getppid();
                // Start monitoring thread
                std::thread([]() {
                    while (true) {
                        if (getppid() == 1) {  // Parent died, we're now child of init
                            exit(1);
                        }
                        std::this_thread::sleep_for(std::chrono::milliseconds(500));
                    }
                }).detach();

                close(pipefd[0]); 
                dup2(pipefd[1], STDOUT_FILENO);
                dup2(pipefd[1], STDERR_FILENO);
                close(pipefd[1]);

                execl("./cppinterop_process", "cppinterop_process", m_shm_name.c_str(), nullptr);
                std::cerr << "Failed to exec CppInterOp process" << std::endl;
                exit(1);
            }
            else
            {
                close(pipefd[1]);
                std::thread([read_fd = pipefd[0]]() {
                    char buffer[256];
                    ssize_t n;
                    while ((n = read(read_fd, buffer, sizeof(buffer) - 1)) > 0)
                    {
                        buffer[n] = '\0';
                        // std::cout << "[CppInterOp child] " << buffer;
                    }
                    close(read_fd);
                }).detach();
            }
            // ********************

            std::this_thread::sleep_for(std::chrono::milliseconds(100));

            int status;
            pid_t result = waitpid(m_child_pid, &status, WNOHANG);
            if (result != 0)
            {
                std::cerr << "CppInterOp process failed to start" << std::endl;
                return false;
            }

            m_initialized = true;
            std::clog << "CppInterOp client initialized with child PID: " << m_child_pid << std::endl;
            return true;
        }

        bool processCode(const std::string& code, std::string& output, std::string& error)
        {
            if (!m_initialized)
            {
                return false;
            }

            m_shared_buffer->reset();
            m_shared_buffer->set_code(code);
            m_shared_buffer->request_type = SharedMemoryBuffer::RequestType::PROCESS_CODE;
            m_shared_buffer->request_ready.store(true, std::memory_order_release);

            // Wait for response
            if (!waitForResponse())
            {
                return false;
            }

            output = m_shared_buffer->get_output();
            error = m_shared_buffer->get_error();

            return m_shared_buffer->response_status.load() == SharedMemoryBuffer::ResponseStatus::SUCCESS;
        }

        bool codeComplete(const std::string& code, int cursor_pos, std::vector<std::string>& results)
        {
            if (!m_initialized)
            {
                return false;
            }

            m_shared_buffer->reset();
            m_shared_buffer->set_code(code);
            m_shared_buffer->cursor_pos = cursor_pos;
            m_shared_buffer->request_type = SharedMemoryBuffer::RequestType::CODE_COMPLETE;
            m_shared_buffer->request_ready = true;

            // Wait for response
            if (!waitForResponse())
            {
                return false;
            }

            results = m_shared_buffer->get_completions();
            return m_shared_buffer->response_status.load() == SharedMemoryBuffer::ResponseStatus::SUCCESS;
        }

        bool evaluate(const std::string& code, int64_t& result)
        {
            if (!m_initialized)
            {
                return false;
            }

            m_shared_buffer->reset();
            m_shared_buffer->set_code(code);
            m_shared_buffer->request_type = SharedMemoryBuffer::RequestType::EVALUATE;
            m_shared_buffer->request_ready = true;

            // Wait for response
            if (!waitForResponse())
            {
                return false;
            }

            if (m_shared_buffer->response_status.load() == SharedMemoryBuffer::ResponseStatus::SUCCESS)
            {
                result = m_shared_buffer->evaluation_result;
                return true;
            }

            return false;
        }

        void shutdown()
        {
            if (!m_initialized)
            {
                return;
            }

            m_shared_buffer->request_type = SharedMemoryBuffer::RequestType::SHUTDOWN;
            m_shared_buffer->request_ready = true;

            // Wait a bit for graceful shutdown
            std::this_thread::sleep_for(std::chrono::milliseconds(100));

            // Kill child process if it's still running
            if (m_child_pid > 0)
            {
                std::cout << "Terminating CppInterOp child process with PID: " << m_child_pid << std::endl;
                kill(m_child_pid, SIGTERM);

                // Wait for child to exit
                int status;
                waitpid(m_child_pid, &status, 0);
                m_child_pid = -1;
            }
        }

        void cleanup()
        {
            if (m_child_pid > 0) {
                kill(-m_child_pid, SIGKILL);  // Negative PID kills process group
            }
            
            if (m_initialized)
            {
                shutdown();
            }

            if (m_shared_buffer && m_shared_buffer != MAP_FAILED)
            {
                munmap(m_shared_buffer, sizeof(SharedMemoryBuffer));
                m_shared_buffer = nullptr;
            }

            if (m_shm_fd != -1)
            {
                close(m_shm_fd);
                shm_unlink(m_shm_name.c_str());
                m_shm_fd = -1;
            }
        }

    private:

        bool waitForResponse(int timeout_ms = 100000)
        {
            auto start = std::chrono::steady_clock::now();

            while (!m_shared_buffer->response_ready.load())
            {
                auto now = std::chrono::steady_clock::now();
                if (std::chrono::duration_cast<std::chrono::milliseconds>(now - start).count() > timeout_ms)
                {
                    std::cerr << "Timeout waiting for CppInterOp response" << std::endl;
                    return false;
                }

                // Check if child process is still alive
                int status;
                pid_t result = waitpid(m_child_pid, &status, WNOHANG);
                static int elapsed_ms = 0;
                if (result != 0)
                {
                    elapsed_ms += 1;
                }
                else 
                {
                    elapsed_ms = 0;
                }
                // If more than 100 seconds have passed, return false
                if (elapsed_ms > 100000)
                {
                    std::cerr << "Timeout: No response from CppInterOp process after 10 seconds" << std::endl;
                    return false;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                elapsed_ms += 1;
            }

            return true;
        }
    };
};

