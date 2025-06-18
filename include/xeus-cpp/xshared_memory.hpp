#pragma once

#include <string>
#include <cstring>
#include <atomic>
#include <vector>
#include <sstream>

struct SharedMemoryBuffer {
    // FIXED: Reduced buffer sizes to fit within 64KB system limit
    // Total struct size should be around 52KB, leaving room for other fields
    static constexpr size_t MAX_CODE_SIZE = 16 * 1024;      // 16KB for code
    static constexpr size_t MAX_OUTPUT_SIZE = 16 * 1024;    // 16KB for output  
    static constexpr size_t MAX_ERROR_SIZE = 8 * 1024;      // 8KB for errors
    static constexpr size_t MAX_COMPLETION_SIZE = 8 * 1024; // 8KB for completions
    
    enum class RequestType : uint32_t {
        NONE = 0,
        PROCESS_CODE,
        CODE_COMPLETE,
        EVALUATE,
        SHUTDOWN
    };
    
    enum class ResponseStatus : uint32_t {
        NONE = 0,
        SUCCESS,
        COMPILATION_ERROR,
        RUNTIME_ERROR,
        SYSTEM_ERROR
    };
    
    std::atomic<bool> request_ready{false};
    std::atomic<bool> response_ready{false};
    std::atomic<RequestType> request_type{RequestType::NONE};
    std::atomic<ResponseStatus> response_status{ResponseStatus::NONE};
    
    char code_buffer[MAX_CODE_SIZE];
    uint32_t code_length;
    int cursor_pos; 
    
    char output_buffer[MAX_OUTPUT_SIZE];
    char error_buffer[MAX_ERROR_SIZE];
    uint32_t output_length;
    uint32_t error_length;
    bool compilation_result;
    int64_t evaluation_result;
    
    char completion_buffer[MAX_COMPLETION_SIZE]; // Use separate size constant
    uint32_t completion_length;
    
    void reset() {
        request_ready.store(false, std::memory_order_relaxed);
        response_ready.store(false, std::memory_order_relaxed);
        request_type.store(RequestType::NONE, std::memory_order_relaxed);
        response_status.store(ResponseStatus::NONE, std::memory_order_relaxed);
        code_length = 0;
        output_length = 0;
        error_length = 0;
        completion_length = 0;
        cursor_pos = 0;
        compilation_result = false;
        evaluation_result = 0;
        
        memset(code_buffer, 0, MAX_CODE_SIZE);
        memset(output_buffer, 0, MAX_OUTPUT_SIZE);
        memset(error_buffer, 0, MAX_ERROR_SIZE);
        memset(completion_buffer, 0, MAX_COMPLETION_SIZE);
    }
    
    void set_code(const std::string& code) {
        code_length = std::min(code.length(), MAX_CODE_SIZE - 1);
        memcpy(code_buffer, code.c_str(), code_length);
        code_buffer[code_length] = '\0';
    }
    
    std::string get_code() const {
        return std::string(code_buffer, code_length);
    }
    
    void set_output(const std::string& output) {
        output_length = std::min(output.length(), MAX_OUTPUT_SIZE - 1);
        memcpy(output_buffer, output.c_str(), output_length);
        output_buffer[output_length] = '\0';
    }
    
    std::string get_output() const {
        return std::string(output_buffer, output_length);
    }
    
    void set_error(const std::string& error) {
        error_length = std::min(error.length(), MAX_ERROR_SIZE - 1);
        memcpy(error_buffer, error.c_str(), error_length);
        error_buffer[error_length] = '\0';
    }
    
    std::string get_error() const {
        return std::string(error_buffer, error_length);
    }
    
    void set_completions(const std::vector<std::string>& completions) {
        std::string combined;
        for (const auto& comp : completions) {
            if (!combined.empty()) combined += "\n";
            combined += comp;
        }
        completion_length = std::min(combined.length(), MAX_COMPLETION_SIZE - 1);
        memcpy(completion_buffer, combined.c_str(), completion_length);
        completion_buffer[completion_length] = '\0';
    }
    
    std::vector<std::string> get_completions() const {
        std::vector<std::string> result;
        std::string data(completion_buffer, completion_length);
        std::istringstream iss(data);
        std::string line;
        while (std::getline(iss, line)) {
            result.push_back(line);
        }
        return result;
    }
    
    // Helper function to get the total size of this struct
    static constexpr size_t total_size() {
        return sizeof(SharedMemoryBuffer);
    }
    
    // Helper function to check if a given size can accommodate this struct
    static bool fits_in_size(size_t available_size) {
        return available_size >= sizeof(SharedMemoryBuffer);
    }
};

// Static assertion to ensure the struct fits in reasonable shared memory limits
static_assert(sizeof(SharedMemoryBuffer) <= 65536, 
              "SharedMemoryBuffer too large for typical shared memory limits");

// Print size information for debugging
inline void print_buffer_size_info() {
    std::cout << "SharedMemoryBuffer size breakdown:" << std::endl;
    std::cout << "  Code buffer: " << SharedMemoryBuffer::MAX_CODE_SIZE << " bytes" << std::endl;
    std::cout << "  Output buffer: " << SharedMemoryBuffer::MAX_OUTPUT_SIZE << " bytes" << std::endl;
    std::cout << "  Error buffer: " << SharedMemoryBuffer::MAX_ERROR_SIZE << " bytes" << std::endl;
    std::cout << "  Completion buffer: " << SharedMemoryBuffer::MAX_COMPLETION_SIZE << " bytes" << std::endl;
    std::cout << "  Total struct size: " << sizeof(SharedMemoryBuffer) << " bytes" << std::endl;
    std::cout << "  Fits in 64KB: " << (sizeof(SharedMemoryBuffer) <= 65536 ? "YES" : "NO") << std::endl;
}