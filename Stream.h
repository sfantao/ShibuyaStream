
#pragma once

#include "Workload.h"
#include "DeviceArray.h"

#include <chrono>
#include <vector>

#if defined(__HIPCC__)
#include <hip/hip_runtime.h>
#elif defined(__NVCC__)
#include <cuda_runtime.h>
#include "hip2cuda.h"
#endif

//------------------------------------------------------------------------------
/// \class Stream
/// \brief parent class for HostStream and DeviceStream
template <typename T>
class Stream {
    friend class Report;

public:
    static void enablePeerAccess(int from, int to);
    static Stream<T>* make(std::string const& string,
                           std::size_t size, double duration, T alpha);

    Stream(std::string label,
           Workload workload,
           std::size_t length,
           double duration,
           T alpha,
           Array<T>* a,
           Array<T>* b,
           Array<T>* c = nullptr)
        : label_(label), workload_(workload), length_(length),
          size_(length*sizeof(T)), duration_(duration),
          alpha_(alpha), a_(a), b_(b), c_(c) {}

    virtual ~Stream()
    {
        delete a_;
        delete b_;
        if (workload_.type() == Workload::Type::Add ||
            workload_.type() == Workload::Type::Triad)
            delete c_;
    }

    void run();
    void test();

    virtual void printInfo() = 0;

    double maxTime() { return timestamps_[timestamps_.size()-1]; }

    double minInterval()
    {
        double min_interval = timestamps_[0];
        for (int i = 1; i < timestamps_.size(); ++i) {
            double interval = timestamps_[i]-timestamps_[i-1];
            if (interval < min_interval) {
                min_interval = interval;
            }
        }
        return min_interval;
    }

    void printStats()
    {
        for (std::size_t i = 0; i < timestamps_.size(); ++i)
            printf("\t%10lf\t%lf\n", timestamps_[i], bandwidths_[i]);
    }

protected:
    void printWorkload() { workload_.print(); }

    void printArrays()
    {
        a_->printInfo();
        b_->printInfo();
        if (workload_.type() == Workload::Type::Add ||
            workload_.type() == Workload::Type::Triad)
            c_->printInfo();
    }

    void setCoreAffinity(int core_id)
    {
        cpu_set_t cpuset;
        CPU_ZERO(&cpuset);
        CPU_SET(core_id, &cpuset);
        int retval = pthread_setaffinity_np(pthread_self(),
                                            sizeof(cpu_set_t), &cpuset);
        ASSERT(retval == 0,
               "Setting thread affinity failed.");
    }

    std::string label_;
    Workload workload_;
    std::size_t length_; ///< length in elements
    std::size_t size_;   ///< size in bytes
    T alpha_;
    Array<T>* a_;
    Array<T>* b_;
    Array<T>* c_;
    T dot_sum_;

    double duration_;                ///< desired duration in seconds
    std::vector<double> timestamps_; ///< timestamps in seconds
    std::vector<double> bandwidths_; ///< bandwidths in GBPS

private:
    static void scanString(std::string const& string,
                           char& hardware_char, int& hardware_id,
                           char& workload_char,
                           char& a_char, int& a_id,
                           char& b_char, int& b_id,
                           char& c_char, int& c_id,
                           int& host_core_id);

    virtual void setAffinity() = 0;

    virtual void copy()  = 0;
    virtual void mul()   = 0;
    virtual void add()   = 0;
    virtual void triad() = 0;
    virtual void dot()   = 0;

    void hip()
    {
        auto a = this->a_->host_ptr();
        if (typeid(this->a_) == typeid(DeviceArray<T>))
            a = this->a_->device_ptr();

        auto b = this->b_->host_ptr();
        if (typeid(this->b_) == typeid(DeviceArray<T>))
            b = this->b_->device_ptr();

        HIP_CALL(hipMemcpy(b, a, this->size_, hipMemcpyDefault),
                 "HIP copy failed.");
        HIP_CALL(hipDeviceSynchronize(),
                 "Device synchronization failed.");
    }

    void dispatch()
    {
        switch (workload_.type()) {
            case Workload::Type::Hip:   hip();   break;
            case Workload::Type::Copy:  copy();  break;
            case Workload::Type::Mul:   mul();   break;
            case Workload::Type::Add:   add();   break;
            case Workload::Type::Triad: triad(); break;
            case Workload::Type::Dot:   dot();   break;
            default: ERROR("Invalid workload type.");
        }
    }

    double bandwidth(double time)
    {
        switch (workload_.type()) {
            case Workload::Type::Hip:   return 2.0*size_/time/1e9;
            case Workload::Type::Copy:  return 2.0*size_/time/1e9;
            case Workload::Type::Mul:   return 2.0*size_/time/1e9;
            case Workload::Type::Add:   return 3.0*size_/time/1e9;
            case Workload::Type::Triad: return 3.0*size_/time/1e9;
            case Workload::Type::Dot:   return 2.0*size_/time/1e9;
            default: ERROR("Invalid workload type.");
        }
        return 0.0; // suppressing NVCC warning
    }

    static std::chrono::high_resolution_clock::time_point beginning_;
};
