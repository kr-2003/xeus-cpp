// cppinterop_process.cpp
// Separate process that handles CppInterOp operations - FIXED VERSION

#include <iostream>
#include <string>
#include <vector>
#include <memory>
#include <chrono>
#include <thread>
#include <sstream>
#include <fstream>
#include <cerrno>
#include <cstring>
#include <mutex>

#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>

#include "clang/Interpreter/CppInterOp.h" // from CppInterOp package
#include "xeus-cpp/xshared_memory.hpp"

class CppInterOpProcess {
private:
    void* m_interpreter;
    SharedMemoryBuffer* m_shared_buffer;
    int m_shm_fd;
    bool m_running;
    std::string m_shm_name;
    size_t m_shm_size;
    static std::atomic<bool> initialized;
    static std::mutex init_mutex;
    
    // Helper function to get system shared memory limits
    size_t getMaxShmSize() {
        // Try to read system limits
        std::ifstream shmmax("/proc/sys/kernel/shmmax");
        if (shmmax.is_open()) {
            size_t max_size;
            shmmax >> max_size;
            shmmax.close();
            return max_size;
        }
        
        // Fallback to a conservative size (1MB for better compatibility)
        return 1024 * 1024;
    }
    
    // Helper function to validate and adjust shared memory size
    size_t validateShmSize(size_t requested_size) {
        size_t max_size = getMaxShmSize();
        size_t min_size = sizeof(SharedMemoryBuffer);
        
        std::clog << "Requested SHM size: " << requested_size << " bytes" << std::endl;
        std::clog << "System max SHM size: " << max_size << " bytes" << std::endl;
        std::clog << "Minimum required size: " << min_size << " bytes" << std::endl;
        
        if (requested_size > max_size) {
            std::clog << "Warning: Requested size exceeds system limit, using " << max_size << " bytes" << std::endl;
            return max_size;
        }
        
        if (requested_size < min_size) {
            std::clog << "Warning: Requested size too small, using minimum " << min_size << " bytes" << std::endl;
            return min_size;
        }
        
        return requested_size;
    }
    
    // Fix corrupted include paths
    std::vector<std::string> sanitizeIncludePaths(const std::vector<std::string>& paths) {
        std::vector<std::string> sanitized;
        
        for (size_t i = 0; i < paths.size(); ++i) {
            const std::string& path = paths[i];
            
            std::clog << "Processing path[" << i << "]: '" << path << "' (length: " << path.length() << ")" << std::endl;
            
            // Skip empty or obviously corrupted paths
            if (path.empty() || path.length() < 3) {
                std::clog << "Skipping invalid path (too short): '" << path << "'" << std::endl;
                continue;
            }
            
            // Check if path contains non-printable characters or null bytes
            bool has_invalid_chars = false;
            for (size_t j = 0; j < path.length(); ++j) {
                char c = path[j];
                if (c == '\0' || (c > 0 && c < 32 && c != '\n' && c != '\t')) {
                    has_invalid_chars = true;
                    std::clog << "Found invalid character at position " << j << ": 0x" << std::hex << (int)(unsigned char)c << std::dec << std::endl;
                    break;
                }
            }
            
            if (has_invalid_chars) {
                std::clog << "Skipping path with invalid characters: '" << path << "'" << std::endl;
                continue;
            }
            
            // Additional check: path should start with '/' on Unix systems
            if (path[0] != '/') {
                std::clog << "Skipping relative path: '" << path << "'" << std::endl;
                continue;
            }
            
            // Check if directory actually exists
            struct stat st;
            if (stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode)) {
                sanitized.push_back(path);
                std::clog << "Added valid include path: " << path << std::endl;
            } else {
                std::clog << "Skipping non-existent path: " << path << " (error: " << strerror(errno) << ")" << std::endl;
            }
        }
        
        return sanitized;
    }
    
    // Alternative: try to use minimal system includes if detection fails
    std::vector<std::string> getMinimalSystemIncludes() {
        std::vector<std::string> minimal_includes;
        
        // Common macOS system include paths
        std::vector<std::string> candidates = {
            "/Library/Developer/CommandLineTools/SDKs/MacOSX.sdk/usr/include/c++/v1",
            "/Library/Developer/CommandLineTools/usr/lib/clang/17/include",
            "/Library/Developer/CommandLineTools/SDKs/MacOSX.sdk/usr/include",
            "/usr/include",
            "/usr/local/include"
        };
        
        for (const std::string& path : candidates) {
            struct stat st;
            if (stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode)) {
                minimal_includes.push_back(path);
                std::clog << "Added minimal system include: " << path << std::endl;
            }
        }
        
        return minimal_includes;
    }
    
public:
    CppInterOpProcess(const std::string& shm_name, size_t shm_size = sizeof(SharedMemoryBuffer)) 
        : m_interpreter(nullptr), m_shared_buffer(nullptr), 
          m_shm_fd(-1), m_running(true), m_shm_name(shm_name) {
        m_shm_size = validateShmSize(shm_size);
    }
    
    ~CppInterOpProcess() {
        cleanup();
    }
    
    bool initialize() {
        // Create shared memory
        m_shm_fd = -1;
        for (int i = 0; i < 50; ++i) {
            m_shm_fd = shm_open(m_shm_name.c_str(), O_RDWR, 0666);
            if (m_shm_fd != -1) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

        if(m_shm_fd == -1) {
            std::cerr << "Failed to open shared memory: " << strerror(errno) << std::endl;
            return false;
        }
        
        // Map shared memory
        m_shared_buffer = static_cast<SharedMemoryBuffer*>(
            mmap(nullptr, m_shm_size, 
                 PROT_READ | PROT_WRITE, MAP_SHARED, m_shm_fd, 0));
        
        if (m_shared_buffer == MAP_FAILED) {
            std::cerr << "Failed to map shared memory: " << strerror(errno) << std::endl;
            return false;
        }
        
        std::clog << "Successfully mapped shared memory at " << m_shared_buffer << std::endl;
        
        // Initialize shared buffer
        m_shared_buffer->reset();
        
        // Initialize CppInterOp interpreter
        if (!initializeInterpreter()) {
            std::cerr << "Failed to initialize CppInterOp interpreter" << std::endl;
            cleanup();
            return false;
        }
        
        std::clog << "CppInterOp process initialized successfully at shm_name: " << m_shm_name <<  std::endl;
        initialized.store(true);
        return true;
    }
    
    void run() {
        std::clog << "CppInterOp process started, waiting for requests..." << std::endl;
        
        while (m_running) {
            // Check for new requests
            // std::clog << m_shared_buffer->request_ready.load() << std::endl;
            if (m_shared_buffer->request_ready.load(std::memory_order_acquire)) {
                processRequest();
                m_shared_buffer->request_ready.store(false);
                m_shared_buffer->response_ready.store(true);
            }
            
            // Small delay to prevent busy waiting
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        
        std::clog << "CppInterOp process shutting down..." << std::endl;
    }
    
    // Get current shared memory size
    size_t getSharedMemorySize() const {
        return m_shm_size;
    }
    
private:
bool initializeInterpreter() {
        try {
            // Simplified ClangArgs construction
            std::vector<const char*> ClangArgs;
            std::vector<std::string> args_storage;

            ClangArgs.push_back("-g");
            ClangArgs.push_back("-O0");

            // Add resource directory
            std::string resource_dir = Cpp::DetectResourceDir();
            if (!resource_dir.empty()) {
                ClangArgs.push_back("-resource-dir");
                ClangArgs.push_back(resource_dir.c_str());
                std::clog << "Using resource directory: " << resource_dir << std::endl;
            } else {
                std::clog << "Failed to detect resource-dir" << std::endl;
            }

            // Detect and sanitize system includes
            std::vector<std::string> CxxSystemIncludes;
            Cpp::DetectSystemCompilerIncludePaths(CxxSystemIncludes);
            std::clog << "Detected " << CxxSystemIncludes.size() << " system include paths (before sanitization)" << std::endl;
            
            CxxSystemIncludes = sanitizeIncludePaths(CxxSystemIncludes);
            std::clog << "Using " << CxxSystemIncludes.size() << " valid system include paths (after sanitization)" << std::endl;

            // Add -isystem for each include path
            for (const std::string& include : CxxSystemIncludes) {
                ClangArgs.push_back("-isystem");
                ClangArgs.push_back(include.c_str());
                std::clog << "Added: -isystem " << include << std::endl;
            }

            for (size_t i = 0; i < ClangArgs.size(); ++i) {
                std::clog << "  Arg[" << i << "]: '" << (ClangArgs[i] ? ClangArgs[i] : "<null>") << "'" << std::endl;
            }

            m_interpreter = Cpp::CreateInterpreter(ClangArgs);

            if (m_interpreter) {
                std::clog << "CppInterOp interpreter created successfully" << std::endl;
                return true;
            } else {
                std::cerr << "CppInterOp interpreter creation returned null" << std::endl;
                return false;
            }
        } catch (const std::exception& e) {
            std::cerr << "Exception during interpreter initialization: " << e.what() << std::endl;
            return false;
        }
    }
    
    void processRequest() {
        auto request_type = m_shared_buffer->request_type.load();
        std::clog << "Processing request type: " << static_cast<uint32_t>(request_type) << std::endl;
        
        try {
            switch (request_type) {
                case SharedMemoryBuffer::RequestType::PROCESS_CODE:
                    processCode();
                    break;
                    
                case SharedMemoryBuffer::RequestType::CODE_COMPLETE:
                    processCodeCompletion();
                    break;
                    
                case SharedMemoryBuffer::RequestType::EVALUATE:
                    processEvaluation();
                    break;
                    
                case SharedMemoryBuffer::RequestType::SHUTDOWN:
                    m_running = false;
                    m_shared_buffer->response_status = SharedMemoryBuffer::ResponseStatus::SUCCESS;
                    break;
                    
                default:
                    m_shared_buffer->response_status = SharedMemoryBuffer::ResponseStatus::SYSTEM_ERROR;
                    m_shared_buffer->set_error("Unknown request type");
                    break;
            }
        }
        catch (const std::exception& e) {
            m_shared_buffer->response_status = SharedMemoryBuffer::ResponseStatus::SYSTEM_ERROR;
            m_shared_buffer->set_error(std::string("Exception: ") + e.what());
        }
    }
    
    void processCode() {
        std::string code = m_shared_buffer->get_code();

        std::clog << "Processing code in CppInterOpProcess: " << code << std::endl;
        
        if (!m_interpreter) {
            m_shared_buffer->response_status = SharedMemoryBuffer::ResponseStatus::SYSTEM_ERROR;
            m_shared_buffer->set_error("Interpreter not initialized");
            return;
        }
        
        try {
            // Capture streams similar to your original StreamRedirectRAII
            Cpp::BeginStdStreamCapture(Cpp::kStdErr);  // stderr
            Cpp::BeginStdStreamCapture(Cpp::kStdOut);  // stdout
            
            bool compilation_result = Cpp::Process(code.c_str());
            
            std::string output = Cpp::EndStdStreamCapture();  // stdout
            std::string error = Cpp::EndStdStreamCapture();   // stderr
            
            m_shared_buffer->compilation_result = compilation_result;
            m_shared_buffer->set_output(output);
            m_shared_buffer->set_error(error);
            
            if (!compilation_result) {
                m_shared_buffer->response_status = SharedMemoryBuffer::ResponseStatus::COMPILATION_ERROR;
            } else {
                m_shared_buffer->response_status = SharedMemoryBuffer::ResponseStatus::SUCCESS;
            }
        } catch (const std::exception& e) {
            m_shared_buffer->response_status = SharedMemoryBuffer::ResponseStatus::SYSTEM_ERROR;
            m_shared_buffer->set_error(std::string("Code processing exception: ") + e.what());
        }
    }
    
    void processCodeCompletion() {
        if (!m_interpreter) {
            m_shared_buffer->response_status = SharedMemoryBuffer::ResponseStatus::SYSTEM_ERROR;
            m_shared_buffer->set_error("Interpreter not initialized");
            return;
        }
        
        try {
            std::string code = m_shared_buffer->get_code();
            int cursor_pos = m_shared_buffer->cursor_pos;
            
            std::vector<std::string> results;
            Cpp::CodeComplete(results, code.c_str(), 1, cursor_pos + 1);
            
            m_shared_buffer->set_completions(results);
            m_shared_buffer->response_status = SharedMemoryBuffer::ResponseStatus::SUCCESS;
        } catch (const std::exception& e) {
            m_shared_buffer->response_status = SharedMemoryBuffer::ResponseStatus::SYSTEM_ERROR;
            m_shared_buffer->set_error(std::string("Code completion exception: ") + e.what());
        }
    }
    
    void processEvaluation() {
        if (!m_interpreter) {
            m_shared_buffer->response_status = SharedMemoryBuffer::ResponseStatus::SYSTEM_ERROR;
            m_shared_buffer->set_error("Interpreter not initialized");
            return;
        }
        
        try {
            std::string code = m_shared_buffer->get_code();
            
            int64_t result = Cpp::Evaluate(code.c_str());
            m_shared_buffer->evaluation_result = result;
            m_shared_buffer->response_status = SharedMemoryBuffer::ResponseStatus::SUCCESS;
        }
        catch (const std::exception& e) {
            m_shared_buffer->response_status = SharedMemoryBuffer::ResponseStatus::RUNTIME_ERROR;
            m_shared_buffer->set_error(std::string("Evaluation exception: ") + e.what());
        }
    }
    
    void cleanup() {
        std::lock_guard<std::mutex> lock(init_mutex);
        initialized.store(false);
        
        if (m_interpreter) {
            // Note: CppInterOp might not have explicit cleanup, 
            // but setting to nullptr is safer
            m_interpreter = nullptr;
        }
        
        if (m_shared_buffer && m_shared_buffer != MAP_FAILED) {
            munmap(m_shared_buffer, m_shm_size);
            m_shared_buffer = nullptr;
        }
        
        if (m_shm_fd != -1) {
            close(m_shm_fd);
            shm_unlink(m_shm_name.c_str());
            m_shm_fd = -1;
        }
    }
};

// Signal handler for graceful shutdown
std::atomic<bool> CppInterOpProcess::initialized{false};
std::mutex CppInterOpProcess::init_mutex;
static CppInterOpProcess* g_process = nullptr;

void signal_handler(int sig) {
    if (g_process) {
        std::clog << "Received signal " << sig << ", shutting down..." << std::endl;
        // The process will exit on next iteration
    }
}

int main(int argc, char* argv[]) {
    if (argc < 2 || argc > 3) {
        std::cerr << "Usage: " << argv[0] << " <shared_memory_name> [shared_memory_size]" << std::endl;
        return 1;
    }
    
    std::string shm_name = argv[1];
    size_t shm_size = sizeof(SharedMemoryBuffer);
    
    if (argc == 3) {
        try {
            shm_size = std::stoull(argv[2]);
        } catch (const std::exception& e) {
            std::cerr << "Invalid shared memory size: " << argv[2] << std::endl;
            return 1;
        }
    }
    
    // Setup signal handlers
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    CppInterOpProcess process(shm_name, shm_size);
    g_process = &process;
    
    std::clog << "Initializing CppInterOp process with shared memory '" 
              << shm_name << "' (size: " << process.getSharedMemorySize() << " bytes)" << std::endl;
    
    if (!process.initialize()) {
        std::cerr << "Failed to initialize CppInterOp process" << std::endl;
        return 1;
    }
    
    process.run();
    
    g_process = nullptr;
    return 0;
}