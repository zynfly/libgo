#include "block_object.h"
#include "scheduler.h"
#include "error.h"
#include <unistd.h>
#include <mutex>
#include <limits>

namespace co
{

TSQueue<Task> BlockObject::sys_block_queue_;

BlockSentry::BlockSentry(Task* tk, std::vector<BlockObjectWeakPtr> && block_objects)
    : task_ptr_(SharedFromThis(tk)), block_objects_(std::move(block_objects))
{
    block_state_ = pending;
}
BlockSentry::~BlockSentry()
{
    for (auto &wp : block_objects_)
    {
        auto sp = wp.lock();
        if (!sp) continue;
        sp->CancelWait(GetBound());
    }
}
BlockSentryBoundPtr BlockSentry::GetBound()
{
    if (!bound_)
        bound_ = MakeShared<BlockSentryBound>(shared_from_this(), task_ptr_.get());

    return bound_;
}
bool BlockSentry::switch_state_to_triggered()
{
    long expected = pending;
    return std::atomic_compare_exchange_strong(&block_state_, &expected, (long)triggered);
}

BlockObject::BlockObject(std::size_t init_wakeup, std::size_t max_wakeup)
    : wakeup_(init_wakeup), max_wakeup_(max_wakeup)
{
    DebugPrint(dbg_syncblock, "BlockObject::BlockObject this=%p", this);
}

BlockObject::~BlockObject()
{
    if (!lock_.try_lock()) {
        ThrowError(eCoErrorCode::ec_block_object_locked);
    }

    if (!wait_queue_.empty()) {
        ThrowError(eCoErrorCode::ec_block_object_waiting);
    }

    DebugPrint(dbg_syncblock, "BlockObject::~BlockObject this=%p", this);
}

void BlockObject::CoBlockWait()
{
    if (!g_Scheduler.IsCoroutine()) {
        while (!TryBlockWait()) usleep(10 * 1000);
        return ;
    }

    std::unique_lock<LFLock> lock(lock_);
    if (wakeup_ > 0) {
        DebugPrint(dbg_syncblock, "wait immedaitely done in coroutine.");
        --wakeup_;
        return ;
    }
    lock.unlock();

    Task* tk = g_Scheduler.GetCurrentTask();
    std::vector<BlockObjectWeakPtr> blocks{shared_from_this()};
    BlockSentry block_sentry = MakeShared<BlockSentry>(tk, std::move(blocks));

    tk->block_sentry_ = block_sentry;
    CoSwitch();
}

void BlockObject::CoSwitch()
{
    Task* tk = g_Scheduler.GetCurrentTask();
    tk->state_ = TaskState::sys_block;
    DebugPrint(dbg_syncblock, "wait to switch. task(%s)", tk->DebugInfo());
    g_Scheduler.CoYield();
}

bool BlockObject::SchedulerSwitch(Task* tk)
{
    bool blocked = false;
    for (auto &wp : tk->block_sentry_->block_objects_)
    {
        auto sp = wp.lock();
        if (!sp) continue;
        if (!sp->SchedulerSwitch(tk->block_sentry_->GetBound())) {
            return false;
        }

        blocked = true;
    }

    if (!blocked) return false;
    sys_block_queue_.push(tk);
    return true;
}
bool BlockObject::SchedulerSwitch(BlockSentryBoundPtr bound_ptr)
{
    std::unique_lock<LFLock> lock(lock_);
    if (wakeup_ > 0) {
        DebugPrint(dbg_syncblock, "wait immedaitely done in scheduler.");
        --wakeup_;
        return false;
    }

    wait_queue_.push(bound_ptr.get());
    return true;
}

bool BlockObject::Wakeup()
{
    std::unique_lock<LFLock> lock(lock_);

    BlockSentryPtr block_sentry;
    BlockSentryBoundPtr bound;

continue_pop:
    bound = wait_queue_.pop_sharedptr();
    if (bound) {
        block_sentry = bound->wp_.lock();
        if (!block_sentry)
            goto continue_pop;
    }

    if (!block_sentry) {
        if (wakeup_ >= max_wakeup_) {
            DebugPrint(dbg_syncblock, "wakeup failed.");
            return false;
        }

        ++wakeup_;
        DebugPrint(dbg_syncblock, "wakeup to %lu.", (long unsigned)wakeup_);
        return true;
    }
    lock.unlock();

    if (!sys_block_queue_.pop(block_sentry->task_ptr_.get())) {
        goto continue_pop;
    }

    tk->block_ = nullptr;
    if (tk->block_timer_) { // block cancel timer必须在lock之外, 因为里面会lock
        if (g_Scheduler.BlockCancelTimer(tk->block_timer_))
            tk->DecrementRef();
        tk->block_timer_.reset();
    }
    DebugPrint(dbg_syncblock, "wakeup task(%s).", tk->DebugInfo());
    g_Scheduler.AddTaskRunnable(tk);
    return true;
}


bool BlockObject::CoBlockWaitTimed(MininumTimeDurationType timeo)
{
    auto begin = std::chrono::steady_clock::now();
    if (!g_Scheduler.IsCoroutine()) {
        while (!TryBlockWait() &&
				std::chrono::duration_cast<MininumTimeDurationType>
                (std::chrono::steady_clock::now() - begin) < timeo)
            usleep(10 * 1000);
        return false;
    }

    std::unique_lock<LFLock> lock(lock_);
    if (wakeup_ > 0) {
        DebugPrint(dbg_syncblock, "wait immedaitely done.");
        --wakeup_;
        return true;
    }
    lock.unlock();

    Task* tk = g_Scheduler.GetCurrentTask();
    tk->block_ = this;
    tk->state_ = TaskState::sys_block;
    ++tk->block_sequence_;
    tk->block_timeout_ = timeo;
    tk->is_block_timeout_ = false;
    DebugPrint(dbg_syncblock, "wait to switch. task(%s)", tk->DebugInfo());
    g_Scheduler.CoYield();

    return !tk->is_block_timeout_;
}

bool BlockObject::TryBlockWait()
{
    std::unique_lock<LFLock> lock(lock_);
    if (wakeup_ == 0)
        return false;

    --wakeup_;
    DebugPrint(dbg_syncblock, "try wait success.");
    return true;
}

void BlockObject::CancelWait(Task* tk, uint32_t block_sequence, bool in_timer)
{
    std::unique_lock<LFLock> lock(lock_);
    if (tk->block_ != this) {
        DebugPrint(dbg_syncblock, "cancelwait task(%s) failed. tk->block_ is not this!", tk->DebugInfo());
        return;
    }

    if (tk->block_sequence_ != block_sequence) {
        DebugPrint(dbg_syncblock, "cancelwait task(%s) failed. tk->block_sequence_ = %u, block_sequence = %u.",
                tk->DebugInfo(), tk->block_sequence_, block_sequence);
        return;
    }

    if (!wait_queue_.erase(tk)) {
        DebugPrint(dbg_syncblock, "cancelwait task(%s) erase failed.", tk->DebugInfo());
        return;
    }
    lock.unlock();

    tk->block_ = nullptr;
    if (!in_timer && tk->block_timer_) { // block cancel timer必须在lock之外, 因为里面会lock
        g_Scheduler.BlockCancelTimer(tk->block_timer_);
        tk->block_timer_.reset();
    }
    tk->is_block_timeout_ = true;
    DebugPrint(dbg_syncblock, "cancelwait task(%s).", tk->DebugInfo());
    g_Scheduler.AddTaskRunnable(tk);
}

bool BlockObject::IsWakeup()
{
    std::unique_lock<LFLock> lock(lock_);
    return wakeup_ > 0;
}

bool BlockObject::AddWaitTask(Task* tk)
{
    std::unique_lock<LFLock> lock(lock_);
    if (wakeup_) {
        --wakeup_;
        return false;
    }

    wait_queue_.push(tk);
    DebugPrint(dbg_syncblock, "add wait task(%s). timeout=%ld", tk->DebugInfo(),
            (long int)std::chrono::duration_cast<std::chrono::milliseconds>(tk->block_timeout_).count());

    // 带超时的, 增加定时器
	if (MininumTimeDurationType::zero() != tk->block_timeout_) {
        uint32_t seq = tk->block_sequence_;
        tk->IncrementRef();
        tk->block_timer_ = g_Scheduler.ExpireAt(tk->block_timeout_, [=]{
                    // 此定时器超过block_object生命期时会crash或死锁,
                    // 所以wakeup或cancelwait时一定要kill此定时器
                    if (tk->block_sequence_ == seq) {
                        DebugPrint(dbg_syncblock,
                            "wait timeout, will cancelwait task(%s). this=%p, tk->block_=%p, seq=%u, timeout=%ld",
                            tk->DebugInfo(), this, tk->block_, seq,
                            (long int)std::chrono::duration_cast<std::chrono::milliseconds>(tk->block_timeout_).count());
                        this->CancelWait(tk, seq, true);
                    }
                    tk->DecrementRef();
                });
    }

    return true;
}

} //namespace co
