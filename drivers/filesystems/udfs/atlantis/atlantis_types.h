////////////////////////////////////////////////////////////////////
// Atlantis Cache System - Kernel Mode Port for ReactOS UDFS
// Copyright (C) Rogerio Regis, adapted for ReactOS
// This file was adapted from the original Atlantis project
////////////////////////////////////////////////////////////////////

#ifndef ATLANTIS_TYPES_H
#define ATLANTIS_TYPES_H

// Kernel mode compatibility includes
extern "C" {
#include <ntifs.h>
}

// Basic type definitions for Atlantis cache system
namespace Atlantis {

// Simple min/max functions for kernel mode
template<typename T>
inline T min(const T& a, const T& b) {
    return (a < b) ? a : b;
}

template<typename T>
inline T max(const T& a, const T& b) {
    return (a > b) ? a : b;
}

// Forward declarations
class Archive;
class CacheModel;
class MgrCacheFile;
class BigFile;
class Config;
class Logger;

// Smart pointer replacements for kernel mode
template<typename T>
class unique_ptr {
private:
    T* ptr_;

public:
    unique_ptr() : ptr_(nullptr) {}
    explicit unique_ptr(T* p) : ptr_(p) {}
    
    ~unique_ptr() {
        delete ptr_;
    }
    
    // Move constructor
    unique_ptr(unique_ptr&& other) : ptr_(other.ptr_) {
        other.ptr_ = nullptr;
    }
    
    // Move assignment
    unique_ptr& operator=(unique_ptr&& other) {
        if (this != &other) {
            delete ptr_;
            ptr_ = other.ptr_;
            other.ptr_ = nullptr;
        }
        return *this;
    }
    
    // Disable copy
    unique_ptr(const unique_ptr&) = delete;
    unique_ptr& operator=(const unique_ptr&) = delete;
    
    T* get() const { return ptr_; }
    T& operator*() const { return *ptr_; }
    T* operator->() const { return ptr_; }
    
    T* release() {
        T* temp = ptr_;
        ptr_ = nullptr;
        return temp;
    }
    
    void reset(T* p = nullptr) {
        delete ptr_;
        ptr_ = p;
    }
    
    operator bool() const { return ptr_ != nullptr; }
};

template<typename T>
class shared_ptr {
private:
    T* ptr_;
    LONG* ref_count_;
    
    void release() {
        if (ref_count_ && InterlockedDecrement(ref_count_) == 0) {
            delete ptr_;
            ExFreePool(ref_count_);
        }
    }

public:
    shared_ptr() : ptr_(nullptr), ref_count_(nullptr) {}
    
    explicit shared_ptr(T* p) : ptr_(p) {
        if (p) {
            ref_count_ = (LONG*)ExAllocatePool(NonPagedPool, sizeof(LONG));
            if (ref_count_) {
                *ref_count_ = 1;
            } else {
                delete p;
                ptr_ = nullptr;
            }
        } else {
            ref_count_ = nullptr;
        }
    }
    
    shared_ptr(const shared_ptr& other) : ptr_(other.ptr_), ref_count_(other.ref_count_) {
        if (ref_count_) {
            InterlockedIncrement(ref_count_);
        }
    }
    
    shared_ptr& operator=(const shared_ptr& other) {
        if (this != &other) {
            release();
            ptr_ = other.ptr_;
            ref_count_ = other.ref_count_;
            if (ref_count_) {
                InterlockedIncrement(ref_count_);
            }
        }
        return *this;
    }
    
    ~shared_ptr() {
        release();
    }
    
    T* get() const { return ptr_; }
    T& operator*() const { return *ptr_; }
    T* operator->() const { return ptr_; }
    
    operator bool() const { return ptr_ != nullptr; }
    
    long use_count() const {
        return ref_count_ ? *ref_count_ : 0;
    }
};

// Kernel mode string class
class string {
private:
    char* data_;
    size_t size_;
    size_t capacity_;
    
    void ensure_capacity(size_t new_size) {
        if (new_size > capacity_) {
            size_t new_capacity = max(new_size * 2, capacity_ * 2);
            char* new_data = (char*)ExAllocatePool(PagedPool, new_capacity);
            if (new_data) {
                if (data_) {
                    RtlCopyMemory(new_data, data_, size_);
                    ExFreePool(data_);
                }
                data_ = new_data;
                capacity_ = new_capacity;
            }
        }
    }

public:
    string() : data_(nullptr), size_(0), capacity_(0) {}
    
    string(const char* str) : data_(nullptr), size_(0), capacity_(0) {
        if (str) {
            size_t len = strlen(str);
            ensure_capacity(len + 1);
            if (data_) {
                RtlCopyMemory(data_, str, len);
                data_[len] = '\0';
                size_ = len;
            }
        }
    }
    
    string(const string& other) : data_(nullptr), size_(0), capacity_(0) {
        if (other.data_) {
            ensure_capacity(other.size_ + 1);
            if (data_) {
                RtlCopyMemory(data_, other.data_, other.size_);
                data_[other.size_] = '\0';
                size_ = other.size_;
            }
        }
    }
    
    string& operator=(const string& other) {
        if (this != &other) {
            if (data_) {
                ExFreePool(data_);
                data_ = nullptr;
                size_ = 0;
                capacity_ = 0;
            }
            if (other.data_) {
                ensure_capacity(other.size_ + 1);
                if (data_) {
                    RtlCopyMemory(data_, other.data_, other.size_);
                    data_[other.size_] = '\0';
                    size_ = other.size_;
                }
            }
        }
        return *this;
    }
    
    ~string() {
        if (data_) {
            ExFreePool(data_);
        }
    }
    
    const char* c_str() const {
        return data_ ? data_ : "";
    }
    
    size_t size() const { return size_; }
    size_t length() const { return size_; }
    bool empty() const { return size_ == 0; }
    
    void append(const char* str, size_t len) {
        if (str && len > 0) {
            ensure_capacity(size_ + len + 1);
            if (data_) {
                RtlCopyMemory(data_ + size_, str, len);
                size_ += len;
                data_[size_] = '\0';
            }
        }
    }
    
    void append(const char* str) {
        if (str) {
            append(str, strlen(str));
        }
    }
    
    string& operator+=(const char* str) {
        append(str);
        return *this;
    }
};

// Pair template
template<typename T1, typename T2>
struct pair {
    T1 first;
    T2 second;
    
    pair() : first(), second() {}
    pair(const T1& f, const T2& s) : first(f), second(s) {}
};

template<typename T1, typename T2>
pair<T1, T2> make_pair(const T1& first, const T2& second) {
    return pair<T1, T2>(first, second);
}

// Vector template
template<typename T>
class vector {
private:
    T* data_;
    size_t size_;
    size_t capacity_;
    
    void ensure_capacity(size_t new_capacity) {
        if (new_capacity > capacity_) {
            T* new_data = (T*)ExAllocatePool(PagedPool, new_capacity * sizeof(T));
            if (new_data) {
                for (size_t i = 0; i < size_; ++i) {
                    new (new_data + i) T(data_[i]);
                    data_[i].~T();
                }
                if (data_) {
                    ExFreePool(data_);
                }
                data_ = new_data;
                capacity_ = new_capacity;
            }
        }
    }

public:
    vector() : data_(nullptr), size_(0), capacity_(0) {}
    
    ~vector() {
        if (data_) {
            for (size_t i = 0; i < size_; ++i) {
                data_[i].~T();
            }
            ExFreePool(data_);
        }
    }
    
    void push_back(const T& value) {
        if (size_ >= capacity_) {
            ensure_capacity(capacity_ == 0 ? 1 : capacity_ * 2);
        }
        if (data_ && size_ < capacity_) {
            new (data_ + size_) T(value);
            ++size_;
        }
    }
    
    size_t size() const { return size_; }
    bool empty() const { return size_ == 0; }
    
    T& operator[](size_t index) { return data_[index]; }
    const T& operator[](size_t index) const { return data_[index]; }
};

} // namespace Atlantis

#endif // ATLANTIS_TYPES_H