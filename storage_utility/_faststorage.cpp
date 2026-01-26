// storage_utility/_faststorage.cpp
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <cstring>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <stdexcept>
#include <vector>
#include <algorithm>

#ifdef __linux__
#include <linux/falloc.h>
#endif

namespace py = pybind11;

constexpr size_t HEADER_SIZE = 32;
constexpr uint32_t MAGIC = 0xFDB10001;
constexpr size_t PAGE_SIZE = 4096;

// Aligned allocator for better cache performance
template<typename T>
struct aligned_allocator {
    using value_type = T;
    
    T* allocate(std::size_t n) {
        void* p;
        if (posix_memalign(&p, 64, n * sizeof(T)) != 0) {
            throw std::bad_alloc();
        }
        return static_cast<T*>(p);
    }
    
    void deallocate(T* p, std::size_t) {
        free(p);
    }
};

// Custom fast hash with better distribution
struct FastHash {
    inline std::size_t operator()(const std::string& k) const noexcept {
        // FNV-1a hash - very fast and good distribution
        std::size_t h = 14695981039346656037ULL;
        const unsigned char* p = reinterpret_cast<const unsigned char*>(k.data());
        const unsigned char* end = p + k.size();
        
        while (p < end) {
            h ^= *p++;
            h *= 1099511628211ULL;
        }
        return h;
    }
};

// Compact record header
struct __attribute__((packed)) RecordHeader {
    uint32_t magic;
    uint32_t key_len;
    uint64_t value_len;
};

class NativeFastStorage {
private:
    int fd_;
    uint8_t* mmap_ptr_;
    size_t file_size_;
    std::unordered_map<std::string, uint64_t, FastHash> index_;
    uint64_t next_free_offset_;
    bool dirty_;
    uint8_t* prefault_ptr_;
    
    // Prefault pages to avoid page faults during writes
    inline void prefault_range(uint8_t* start, size_t len) {
        // Touch every page to force allocation
        uint8_t* end = start + len;
        for (uint8_t* p = start; p < end; p += PAGE_SIZE) {
            // Volatile to prevent optimization
            *reinterpret_cast<volatile uint8_t*>(p) = 0;
        }
        // Touch last byte
        if (len > 0) {
            *reinterpret_cast<volatile uint8_t*>(end - 1) = 0;
        }
    }
    
    void rebuild_index_fast() {
        index_.clear();
        index_.reserve(100000);
        
        uint64_t offset = HEADER_SIZE;
        uint8_t* ptr = mmap_ptr_ + offset;
        uint8_t* end_ptr = mmap_ptr_ + next_free_offset_;
        
        while (ptr + sizeof(RecordHeader) <= end_ptr) {
            RecordHeader* hdr = reinterpret_cast<RecordHeader*>(ptr);
            
            if (hdr->magic != MAGIC) break;
            if (hdr->key_len == 0 || hdr->key_len > 10000) break;
            
            ptr += sizeof(RecordHeader);
            
            std::string key(reinterpret_cast<const char*>(ptr), hdr->key_len);
            
            size_t record_size = sizeof(RecordHeader) + hdr->key_len + hdr->value_len;
            
            if (offset + record_size > file_size_) break;
            
            index_.emplace(std::move(key), offset);
            
            offset += record_size;
            ptr += hdr->key_len + hdr->value_len;
        }
    }
    
    inline void update_header() {
        if (!dirty_) return;
        uint64_t* header = reinterpret_cast<uint64_t*>(mmap_ptr_);
        header[0] = MAGIC;
        header[1] = next_free_offset_;
        header[2] = index_.size();
        dirty_ = false;
    }

public:
    NativeFastStorage(const std::string& filename, size_t size) 
        : fd_(-1), mmap_ptr_(nullptr), file_size_(size), 
          next_free_offset_(HEADER_SIZE), dirty_(false), prefault_ptr_(nullptr) {
        
        fd_ = open(filename.c_str(), O_RDWR | O_CREAT | O_NOATIME, 0644);
        if (fd_ == -1) {
            fd_ = open(filename.c_str(), O_RDWR | O_CREAT, 0644);
        }
        if (fd_ == -1) {
            throw std::runtime_error("Failed to open file");
        }
        
        struct stat st;
        fstat(fd_, &st);
        
        bool is_new = st.st_size < static_cast<off_t>(HEADER_SIZE);
        
        if (static_cast<size_t>(st.st_size) < size) {
            #ifdef __linux__
            // Try fallocate first (doesn't zero, much faster)
            if (fallocate(fd_, 0, 0, size) == -1) {
                // Fallback to ftruncate
                if (ftruncate(fd_, size) == -1) {
                    close(fd_);
                    throw std::runtime_error("Failed to allocate file space");
                }
            }
            #else
            if (ftruncate(fd_, size) == -1) {
                close(fd_);
                throw std::runtime_error("Failed to allocate file space");
            }
            #endif
        } else {
            file_size_ = st.st_size;
        }
        
        // Map with MAP_POPULATE to prefault all pages immediately
        mmap_ptr_ = static_cast<uint8_t*>(mmap(nullptr, file_size_, 
            PROT_READ | PROT_WRITE, MAP_SHARED | MAP_POPULATE, fd_, 0));
        
        if (mmap_ptr_ == MAP_FAILED) {
            close(fd_);
            throw std::runtime_error("Failed to mmap file");
        }
        
        #ifdef __linux__
        // Lock pages in memory to prevent swapping
        mlock(mmap_ptr_, file_size_);
        
        // Aggressive memory advice
        madvise(mmap_ptr_, file_size_, MADV_WILLNEED);
        madvise(mmap_ptr_, file_size_, MADV_SEQUENTIAL);
        #endif
        
        // Prefault ALL pages upfront to eliminate runtime page faults
        if (is_new) {
            // For new files, touch every page
            prefault_range(mmap_ptr_, file_size_);
        }
        
        index_.reserve(100000);
        
        if (!is_new) {
            uint64_t* header = reinterpret_cast<uint64_t*>(mmap_ptr_);
            if (header[0] == MAGIC) {
                uint64_t stored_offset = header[1];
                if (stored_offset >= HEADER_SIZE && stored_offset <= file_size_) {
                    next_free_offset_ = stored_offset;
                    rebuild_index_fast();
                }
            }
        }
        
        prefault_ptr_ = mmap_ptr_ + next_free_offset_;
        update_header();
    }
    
    ~NativeFastStorage() {
        if (mmap_ptr_ != nullptr && mmap_ptr_ != MAP_FAILED) {
            update_header();
            #ifdef __linux__
            munlock(mmap_ptr_, file_size_);
            #endif
            munmap(mmap_ptr_, file_size_);
        }
        if (fd_ != -1) {
            close(fd_);
        }
    }
    
    void write(const std::string& key, const std::string& value) {
        uint32_t key_len = key.size();
        uint64_t value_len = value.size();
        size_t record_size = sizeof(RecordHeader) + key_len + value_len;
        
        if (next_free_offset_ + record_size > file_size_) {
            throw std::runtime_error("Storage full");
        }
        
        uint8_t* ptr = mmap_ptr_ + next_free_offset_;
        uint64_t offset = next_free_offset_;
        
        // Prefault pages for large writes (>4KB)
        if (record_size > PAGE_SIZE && ptr > prefault_ptr_) {
            size_t prefault_size = std::min(size_t(1024 * 1024), // 1MB at a time
                                           file_size_ - next_free_offset_);
            prefault_range(ptr, prefault_size);
            prefault_ptr_ = ptr + prefault_size;
        }
        
        // Write header
        RecordHeader* hdr = reinterpret_cast<RecordHeader*>(ptr);
        hdr->magic = MAGIC;
        hdr->key_len = key_len;
        hdr->value_len = value_len;
        ptr += sizeof(RecordHeader);
        
        // Write key and value
        std::memcpy(ptr, key.data(), key_len);
        ptr += key_len;
        std::memcpy(ptr, value.data(), value_len);
        
        // Update index
        auto result = index_.emplace(key, offset);
        if (!result.second) {
            result.first->second = offset;
        }
        
        next_free_offset_ += record_size;
        dirty_ = true;
    }
    
    std::string read(const std::string& key) {
        auto it = index_.find(key);
        if (it == index_.end()) {
            throw std::runtime_error("Key not found");
        }
        
        uint64_t offset = it->second;
        uint8_t* ptr = mmap_ptr_ + offset;
        
        RecordHeader* hdr = reinterpret_cast<RecordHeader*>(ptr);
        ptr += sizeof(RecordHeader) + hdr->key_len;
        
        return std::string(reinterpret_cast<const char*>(ptr), hdr->value_len);
    }
    
    void remove(const std::string& key) {
        if (index_.erase(key) == 0) {
            throw std::runtime_error("Key not found");
        }
        dirty_ = true;
    }
    
    void flush() {
        update_header();
        if (mmap_ptr_ != nullptr && mmap_ptr_ != MAP_FAILED) {
            msync(mmap_ptr_, next_free_offset_, MS_ASYNC);
        }
    }
    
    bool contains(const std::string& key) const {
        return index_.find(key) != index_.end();
    }
    
    std::vector<std::string> keys() const {
        std::vector<std::string> result;
        result.reserve(index_.size());
        for (const auto& [key, _] : index_) {
            result.push_back(key);
        }
        return result;
    }
    
    size_t size() const { return index_.size(); }
    size_t bytes_used() const { return next_free_offset_ - HEADER_SIZE; }
    size_t capacity() const { return file_size_ - HEADER_SIZE; }
};

PYBIND11_MODULE(_faststorage, m) {
    m.doc() = "Ultra-fast mmap-backed storage engine";
    
    py::class_<NativeFastStorage>(m, "NativeFastStorage")
        .def(py::init<const std::string&, size_t>(),
             py::arg("filename"),
             py::arg("size") = 100 * 1024 * 1024)
        .def("write", &NativeFastStorage::write)
        .def("read", &NativeFastStorage::read)
        .def("remove", &NativeFastStorage::remove)
        .def("flush", &NativeFastStorage::flush)
        .def("contains", &NativeFastStorage::contains)
        .def("keys", &NativeFastStorage::keys)
        .def("size", &NativeFastStorage::size)
        .def("bytes_used", &NativeFastStorage::bytes_used)
        .def("capacity", &NativeFastStorage::capacity);
}