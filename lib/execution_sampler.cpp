#include "execution_sampler.hpp"
#include "env.hpp"
#include <algorithm>
#include <cassert>
#include <csignal>
#include <cstring>
#include <ctime>
#include <exception> // for std::terminate

namespace
{
// TODO(thelink2012): Use a compact hash table instead of a singleton
jvmtiprof::ExecutionSampler* singleton{};
}

namespace
{
/// Old signal handler before installing our signal handler.
struct sigaction g_old_action;

/// Whether the old signal handler is meangniful and should be called in case
/// the signal event is not targetted at us.
bool g_forward_to_old_action{false};

/// The number of calls to `add_capability` without a corresponding
/// `relinquish_capability` thus far.
jint g_num_add_capability{0};

/// Signal handler for the CPU-time consumption event.
void signal_action(int signum, siginfo_t* siginfo, void* ucontext)
{
    // In case this signal is targetted at us, it must have been triggered from
    // a POSIX timer and its user-provided value (`si_value`) is a pointer to
    // the `ExecutionSampler` owning the timer.
    if(siginfo->si_code == SI_TIMER)
    {
        std::atomic_signal_fence(std::memory_order_acquire);

        if(auto* sampler = jvmtiprof::ExecutionSampler::from_opaque_pointer(
                   siginfo->si_value.sival_ptr))
        {
            // TODO(thelink2012): Fix possible race condition in which the
            // obtained sampler can be deleted from the point its converted from
            // a opaque pointer to the point it is used to call
            // `post_execution_sample`. Remember this is a signal and we cannot
            // use a mutex.

            // The event callback may invoke functions changing errno. Its value
            // must be preserved.
            const auto old_errno = errno;

            // Post the event 1 + number of timer overruns since last call.
            // Timer overruns may occur if e.g. a signal is to be delivered,
            // a signal handler for that same timer is still being executed.
            // See timer_getoverrun(2) for details.
            sampler->post_execution_sample();
            for(int i = 0; i < siginfo->si_overrun; ++i)
                sampler->post_execution_sample();

            // TODO(thelink2012): Scope guard
            errno = old_errno;

            return;
        }
    }

    // At this point, we know the signal wasn't targetted at us (or perhaps it
    // was but its `ExecutionSampler` has been deleted in the mean time).
    // Anyway, forward it to the next handler.
    // TODO(thelink2012): Fix possible race condition in which
    // g_forward_to_old_action / g_old_action changes while the following code
    // path executes. Remember this is a signal and we cannot use a mutex.
    if(g_forward_to_old_action)
    {
        if(g_old_action.sa_flags & SA_SIGINFO)
            return g_old_action.sa_sigaction(signum, siginfo, ucontext);
        else
            return g_old_action.sa_handler(signum);
    }
}
}

namespace jvmtiprof
{
bool ExecutionSampler::add_capability()
{
    assert(g_num_add_capability >= 0);

    if(g_num_add_capability > 0)
    {
        ++g_num_add_capability;
        return true;
    }

    struct sigaction action, old_action;

    std::memset(&action, 0, sizeof(action));
    sigemptyset(&action.sa_mask);
    action.sa_sigaction = signal_action;
    action.sa_flags = SA_RESTART | SA_SIGINFO;

    if(sigaction(SIGPROF, &action, &old_action) != 0)
        return false;

    // TODO(thelink2012): Fix possible race condition in which `signal_action`
    // is called before these being set.
    g_forward_to_old_action = (old_action.sa_flags & SA_SIGINFO)
                              || (old_action.sa_handler != SIG_DFL
                                  && old_action.sa_handler != SIG_IGN);
    g_old_action = old_action;
    ++g_num_add_capability;

    assert(g_num_add_capability == 1);
    return true;
}

void ExecutionSampler::relinquish_capability()
{
    assert(g_num_add_capability >= 0);

    if(--g_num_add_capability != 0)
        return;

    if(sigaction(SIGPROF, &g_old_action, nullptr) != 0)
    {
        // TODO(thelink2012): log fatal
        std::terminate();
    }

    assert(g_num_add_capability == 0);
}

bool ExecutionSampler::has_capability()
{
    return g_num_add_capability > 0;
}

ExecutionSampler::ExecutionSampler(JvmtiProfEnv& jvmtiprof_env) :
    m_jvmtiprof_env(&jvmtiprof_env)
{
    assert(has_capability());
    assert(singleton == nullptr);
    singleton = this;

    struct sigevent sev;
    sev.sigev_notify = SIGEV_SIGNAL;
    sev.sigev_signo = SIGPROF;
    sev.sigev_value.sival_ptr = this;

    if(timer_create(CLOCK_PROCESS_CPUTIME_ID, &sev, &m_timer_id) != 0)
    {
        // TODO(thelink2012): log fatal
        std::terminate();
    }

    // Ensure visibility of `singleton` and `m_jvmtiprof_env` in the
    // signal handler.
    std::atomic_signal_fence(std::memory_order_release);
}

ExecutionSampler::~ExecutionSampler()
{
    assert(!m_has_started);

    if(timer_delete(m_timer_id) != 0)
    {
        // TODO(thelink2012): log fatal
        std::terminate();
    }

    assert(singleton == this);
    singleton = nullptr;

    // Ensure visibility of `singleton` in the signal handler.
    std::atomic_signal_fence(std::memory_order_release);
}

void ExecutionSampler::start()
{
    assert(!m_has_started);
    restart();
}

void ExecutionSampler::restart()
{
    constexpr jlong second_to_nanos = 1000000000L;

    struct itimerspec timerspec;
    timerspec.it_interval.tv_sec = m_interval_nanos / second_to_nanos;
    timerspec.it_interval.tv_nsec = m_interval_nanos % second_to_nanos;
    timerspec.it_value = timerspec.it_interval;

    if(timer_settime(m_timer_id, 0, &timerspec, nullptr) != 0)
    {
        // TODO(thelink2012): log fatal
        std::terminate();
    }

    m_has_started = true;
}

void ExecutionSampler::stop()
{
    assert(m_has_started);

    struct itimerspec timerspec;
    timerspec.it_interval.tv_sec = 0;
    timerspec.it_interval.tv_nsec = 0;
    timerspec.it_value = timerspec.it_interval;

    if(timer_settime(m_timer_id, 0, &timerspec, nullptr) != 0)
    {
        // TODO(thelink2012): log fatal
        std::terminate();
    }

    m_has_started = false;
}

void ExecutionSampler::set_sampling_interval(jlong nanos_interval)
{
    assert(nanos_interval > 0);

    // An interval of zero would cause `timer_settime` to disasm the timer.
    // Ensure it's not zero by giving at least the value of one.
    m_interval_nanos = std::max<jlong>(1, nanos_interval);

    if(m_has_started)
        restart();
}

void ExecutionSampler::post_execution_sample()
{
    // NOTE: This method must be simple enough to be thread safe and
    // async-signal safe.
    m_jvmtiprof_env->post_execution_sample();
}

auto ExecutionSampler::from_opaque_pointer(void* ptr) -> ExecutionSampler*
{
    // TODO(thelink2012): This method must be thread/async-signal safe.
    if(ptr == singleton && singleton != nullptr)
        return singleton;
    return nullptr;
}
}

// TODO(thelink2012): What should be the default sampling interval?
// TODO(thelink2012): This should be implemented through CLOCK_THREAD_CPUTIME_ID
// + SIGEV_THREAD_ID (itself). The results should be more accurate (profile it
// though). This will significantly change the interface guarantees
// (documentation) and it needs direct interaction with thread start/end events.
