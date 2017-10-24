/* Copyright Fabrice Triboix */

#pragma once

#include <atomic>
#include <stdexcept>

namespace ft {

struct mutex_terminated : public std::runtime_error
{
    mutex_terminated() :
        std::runtime_error("Can't lock mutex because it is terminated") { }
};

/** Safe mutex wrapper class
 *
 * If the mutex is being destroyed while locked, the following occurs:
 *  - Any subsequent attempt to lock the mutex will throw `mutex_terminated`
 *  - The destructor will wait for the mutex to be unlocked
 *  - Once the mutex is unlocked, the mutex is destroyed
 *
 * The template type `T` must have the following public methods:
 *  - `lock()` to lock the mutex, blocking if necessary
 *  - `try_lock()` to try to lock the mutex, and return immediately;
 *    this method should return a boolean: `true` if the mutex has been
 *    locked, `false` otherwise
 *  - `unlock()` to unlock the mutex
 *
 * The template type `T` must have a default constructor which construct an
 * unlocked mutex. Its methods must not throw.
 *
 * You can wrap the `safe_mutex` around a RAII lock, such as `std::unique_lock`.
 */
template <typename T>
class safe_mutex final
{
public:
    safe_mutex() : mutex_(), terminated_(false)
    {
    }

    ~safe_mutex()
    {
        terminated_ = true;
        mutex_.lock();
    }

    void lock()
    {
        if (terminated_) {
            throw mutex_terminated();
        }
        mutex_.lock();
    }

    bool try_lock()
    {
        if (terminated_) {
            throw mutex_terminated();
        }
        return mutex_.try_lock();
    }

    void unlock()
    {
        mutex_.unlock();
    }

private:
    T mutex_;
    std::atomic_bool terminated_;

    safe_mutex(const safe_mutex&) = delete;
    safe_mutex& operator=(const safe_mutex&) = delete;
};

} // namespace ft
