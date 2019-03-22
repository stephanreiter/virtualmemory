#ifndef _VIRTUALMEMORY_H_
#define _VIRTUALMEMORY_H_

#ifdef WINDOWS
#define WIN32_LEAN_AND_MEAN
#define STRICT
#include <Windows.h>
#else
#include <sys/mman.h>
#endif

template <typename T>
class VirtualMemory {
public:
  VirtualMemory(const VirtualMemory&) = delete;
  VirtualMemory& operator=(const VirtualMemory&) = delete;

  VirtualMemory(size_t capacity)
    : capacity_(capacity)
    , count_(0) {
    if (capacity_ == 0) {
      memory_ = nullptr;
    }
    else {
      size_t length = sizeof(T) * capacity_;
#ifdef WINDOWS
      void* memory = VirtualAlloc(0, length, MEM_RESERVE, PAGE_NOACCESS);
      rmsAssert(memory != 0);
#else
      void* memory = mmap(0, length, PROT_READ | PROT_WRITE,
                          MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
      rmsAssert(memory != MAP_FAILED);
      msync(memory, length, MS_SYNC | MS_INVALIDATE);
#endif
      memory_ = reinterpret_cast<T*>(memory);
    }
  }

  ~VirtualMemory() {
    if (memory_) {
#ifdef WINDOWS
      int ret = VirtualFree(memory_, 0, MEM_RELEASE);
      rmsAssert(ret != 0);
#else
      size_t length = sizeof(T) * capacity_;
      msync(memory_, length, MS_SYNC);
      int ret = munmap(memory_, length);
      rmsAssert(ret != -1);
#endif
    }
  }

  void
  Reset() {
    if (memory_) {
#ifdef WINDOWS
      int ret = VirtualFree(memory_, 0, MEM_DECOMMIT);
      rmsAssert(ret != 0);
#else
      // http://blog.nervus.org/managing-virtual-address-spaces-with-mmap/
      size_t length = sizeof(T) * capacity_;
      msync(memory_, length, MS_SYNC | MS_INVALIDATE);
      void* memory = mmap(memory_, length, PROT_READ | PROT_WRITE,
                          MAP_FIXED | MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
      rmsAssert(memory == memory_);
#endif
    }
    count_ = 0;
  }

  T& operator[](size_t index) {
    rmsAssert(index < count_);
    return memory_[index];
  }

  const T& operator[](size_t index) const {
    rmsAssert(index < count_);
    return memory_[index];
  }

  template <typename... Args>
  T&
  emplace_back(Args&&... args) {
    rmsAssert(count_ < capacity_);
    T& dest = memory_[count_++];
#ifdef WINDOWS
    __try {
      new (&dest) T(std::forward<Args>(args)...);
    }
    __except (PageFaultExceptionFilter(GetExceptionCode(),
                                       reinterpret_cast<void*>(&dest))) {
      rmsAssert(false, "Failed to commit page!");
      std::terminate();
    }
#else
    new (&dest) T(std::forward<Args>(args)...);
#endif
    return dest;
  }

  size_t
  capacity() const {
    return capacity_;
  }

  size_t
  size() const {
    return count_;
  }

private:
  T*     memory_;
  size_t capacity_;
  size_t count_;

#ifdef WINDOWS
  // https://msdn.microsoft.com/en-us/library/windows/desktop/aa366803(v=vs.85).aspx
  static INT
  PageFaultExceptionFilter(DWORD dwCode, void* target) {
    if (dwCode == EXCEPTION_ACCESS_VIOLATION) {
      // commit a page for a single T, note: "the allocated pages include all
      // pages containing one or more bytes in the range from lpAddress to
      // lpAddress+dwSize" -> so we actually will get a full page
      // https://msdn.microsoft.com/en-us/library/windows/desktop/aa366887(v=vs.85).aspx
      void* result =
        VirtualAlloc(target, sizeof(T), MEM_COMMIT, PAGE_READWRITE);
      if (result) {
        return EXCEPTION_CONTINUE_EXECUTION;
      }
    }
    return EXCEPTION_EXECUTE_HANDLER;
  }
#endif
};

#endif
