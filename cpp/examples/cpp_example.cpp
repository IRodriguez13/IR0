// SPDX-License-Identifier: GPL-3.0-only
/**
 * IR0 Kernel — Example C++ Component
 * Copyright (C) 2025  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: cpp_example.cpp
 * Description: Reference implementation of a C++ kernel component for IR0
 */

#include <cpp/include/compat.h>

extern "C" {
    #include <ir0/memory/kmem.h>
    #include <ir0/logging.h>
    #include <ir0/driver.h>
}

namespace ir0 {
namespace example {

// RAII RESOURCE GUARD (Demonstrates C++ RAII pattern)

template<typename T>
class ResourceGuard {
private:
    T* resource_;
    size_t size_;
    
public:
    ResourceGuard(size_t size) : size_(size) {
        resource_ = static_cast<T*>(kmalloc(size * sizeof(T)));
        if (!resource_) {
            log_error("ResourceGuard", "Allocation failed");
        }
    }
    
    ~ResourceGuard() {
        if (resource_) {
            kfree(resource_);
            resource_ = nullptr;
        }
    }
    
    // Disable copy
    ResourceGuard(const ResourceGuard&) = delete;
    ResourceGuard& operator=(const ResourceGuard&) = delete;
    
    // Enable move
    ResourceGuard(ResourceGuard&& other) noexcept 
        : resource_(other.resource_), size_(other.size_) {
        other.resource_ = nullptr;
        other.size_ = 0;
    }
    
    T* get() { return resource_; }
    const T* get() const { return resource_; }
    bool valid() const { return resource_ != nullptr; }
    size_t size() const { return size_; }
};

// TEMPLATE-BASED CIRCULAR BUFFER

template<typename T, size_t Size>
class CircularBuffer {
private:
    T buffer_[Size];
    size_t head_;
    size_t tail_;
    size_t count_;
    
public:
    CircularBuffer() : head_(0), tail_(0), count_(0) {}
    
    bool push(const T& item) {
        if (count_ >= Size) {
            return false;  // Buffer full
        }
        
        buffer_[tail_] = item;
        tail_ = (tail_ + 1) % Size;
        count_++;
        return true;
    }
    
    bool pop(T& item) {
        if (count_ == 0) {
            return false;  // Buffer empty
        }
        
        item = buffer_[head_];
        head_ = (head_ + 1) % Size;
        count_--;
        return true;
    }
    
    size_t size() const { return count_; }
    bool empty() const { return count_ == 0; }
    bool full() const { return count_ >= Size; }
    size_t capacity() const { return Size; }
};

// EXAMPLE C++ COMPONENT CLASS

class ExampleComponent {
private:
    bool initialized_;
    CircularBuffer<uint32_t, 64> event_queue_;
    uint32_t event_count_;
    
public:
    ExampleComponent() 
        : initialized_(false), event_count_(0) {
    }
    
    ~ExampleComponent() {
        shutdown();
    }
    
    bool init() {
        if (initialized_) {
            log_warn("CPPExample", "Component already initialized");
            return true;
        }
        
        log_info("CPPExample", "Example C++ component initializing...");
        
        initialized_ = true;
        event_count_ = 0;
        
        log_info("CPPExample", "Component initialized successfully");
        log_info("CPPExample", "  - RAII resource management");
        log_info("CPPExample", "  - Template-based data structures");
        log_info("CPPExample", "  - Object-oriented design");
        log_info("CPPExample", "  - Exception-free C++ (freestanding)");
        
        return true;
    }
    
    void shutdown() {
        if (!initialized_) {
            return;
        }
        
        log_info("CPPExample", "Shutting down C++ component...");
        
        initialized_ = false;
        event_count_ = 0;
        
        log_info("CPPExample", "Component shutdown complete");
    }
    
    bool process_event(uint32_t event) {
        if (!initialized_) {
            log_error("CPPExample", "Cannot process event: not initialized");
            return false;
        }
        
        if (!event_queue_.push(event)) {
            log_error("CPPExample", "Event queue full, dropping event");
            return false;
        }
        
        event_count_++;
        return true;
    }
    
    bool get_event(uint32_t& event) {
        return event_queue_.pop(event);
    }
    
    size_t pending_events() const {
        return event_queue_.size();
    }
    
    uint32_t total_events() const {
        return event_count_;
    }
    
    bool is_initialized() const {
        return initialized_;
    }
};

// Global component instance
static ExampleComponent* g_component = nullptr;

} // namespace example
} // namespace ir0

// C INTERFACE (for kernel integration)

extern "C" {
    #include <ir0/memory/kmem.h>
    #include <ir0/logging.h>
    #include <ir0/driver.h>
    #include <ir0/vga.h>  // For print_success
}

namespace ir0 {
namespace example {

// ... (rest of the namespace code remains the same)

} // namespace example
} // namespace ir0

// ============================================================================
// C INTERFACE (for kernel integration)
// ============================================================================

extern "C" {

using namespace ir0::example;

/**
 * Initialize the C++ example component
 */
IR0_API int32_t cpp_example_init() {
    log_info("CPPExample", "Initializing C++ example component...");
    
    // Allocate component using kernel allocator (calls our custom new)
    g_component = new ExampleComponent();
    
    if (!g_component) {
        log_error("CPPExample", "Failed to allocate component");
        return IR0_DRIVER_ERR;
    }
    
    if (!g_component->init()) {
        delete g_component;
        g_component = nullptr;
        return IR0_DRIVER_ERR;
    }
    
    return IR0_DRIVER_OK;
}

/**
 * Shutdown the component
 */
IR0_API void cpp_example_shutdown() {
    if (g_component) {
        log_info("CPPExample", "Shutting down component...");
        delete g_component;
        g_component = nullptr;
    }
}

/**
 * Process an event
 */
IR0_API int32_t cpp_example_process_event(uint32_t event) {
    if (!g_component) {
        log_error("CPPExample", "Component not initialized");
        return IR0_DRIVER_ERR;
    }
    
    return g_component->process_event(event) ? IR0_DRIVER_OK : IR0_DRIVER_ERR;
}

/**
 * Get pending event count
 */
IR0_API size_t cpp_example_pending_events() {
    if (!g_component) {
        return 0;
    }
    
    return g_component->pending_events();
}

/**
 * Demonstrate RAII with ResourceGuard
 */
IR0_API int32_t cpp_example_test_raii() {
    log_info("CPPExample", "Testing RAII resource management...");
    
    {
        // ResourceGuard automatically allocates in constructor
        ResourceGuard<uint8_t> guard(1024);
        
        if (!guard.valid()) {
            log_error("CPPExample", "RAII test failed: allocation error");
            return IR0_DRIVER_ERR;
        }
        
        uint8_t* buffer = guard.get();
        
        // Use the buffer
        for (size_t i = 0; i < guard.size(); i++) {
            buffer[i] = static_cast<uint8_t>(i & 0xFF);
        }
        
        print_success("[CPPExample] RAII test: buffer allocated and used\n");
        
        // ResourceGuard automatically frees in destructor when exiting scope
    }
    
    print_success("[CPPExample] RAII test: resource automatically freed\n");
    return IR0_DRIVER_OK;
}

/**
 * Register as a driver (optional)
 * Note: Using simplified registration - full metadata via separate call if needed
 */
IR0_API ir0_driver_t* register_cpp_example_driver() {
    static const ir0_driver_info_t info = {
        .name = "cpp_example",
        .version = "1.0.0",
        .author = "IR0 Kernel Team",
        .description = "Example C++ component",
        .language = IR0_DRIVER_LANG_CPP
    };
    
    static const ir0_driver_ops_t ops = {
        .init = cpp_example_init,
        .probe = nullptr,
        .remove = nullptr,
        .shutdown = cpp_example_shutdown,
        .read = nullptr,
        .write = nullptr,
        .ioctl = nullptr,
        .suspend = nullptr,
        .resume = nullptr
    };
    
    return ir0_register_driver(&info, &ops);
}

} // extern "C"
