#pragma once
#include "ts_queue.h"
#include "task.h"

namespace co
{

typedef std::weak_ptr<Task> TaskWeakPtr;
typedef std::shared_ptr<Task> TaskPtr;

class BlockObject;
typedef std::weak_ptr<BlockObject> BlockObjectWeakPtr;

struct BlockSentryBound;
typedef std::shared_ptr<BlockSentryBound> BlockSentryBoundPtr;

struct BlockSentry
    : public RefObject, public TSQueueHook,
    public std::enable_shared_from_this<BlockSentry>,
    public CoDebugger::DebuggerBase<BlockSentry>
{
    enum task_block_state
    {
        pending,
        triggered,
    };

    std::atomic<long> block_state_;
    std::vector<BlockObjectWeakPtr> block_objects_;
    CoTimerPtr timer_;
    TaskPtr task_ptr_;
    BlockSentryBoundPtr bound_;

    explicit BlockSentry(Task* tk, std::vector<BlockObjectWeakPtr> && block_objects);
    ~BlockSentry();

    BlockSentryBoundPtr GetBound();

    // return: cas pending to triggered
    bool switch_state_to_triggered();
};
typedef std::weak_ptr<BlockSentry> BlockSentryWeakPtr;
typedef std::shared_ptr<BlockSentry> BlockSentryPtr;

typedef Task* TaskKey;

struct BlockSentryBound
    : public RefObject, public TSQueueHook
{
    BlockSentryWeakPtr wp_;
    TaskKey tk_key_;

    BlockSentryBound(BlockSentryWeakPtr wp, TaskKey key)
        : wp_(wp), tk_key_(key) {}
};

// 信号管理对象
// @线程安全

class BlockObject
    : public RefObject, public TSQueueHook,
    public std::enable_shared_from_this<BlockObject>
{
protected:
    friend class Processer;
    std::size_t wakeup_;        // 当前信号数量
    std::size_t max_wakeup_;    // 可以积累的信号数量上限
    TSQueue<BlockSentryBound, false> wait_queue_;   // 等待信号的协程队列
    LFLock lock_;
    static TSQueue<Task> sys_block_queue_;

public:
    explicit BlockObject(std::size_t init_wakeup = 0, std::size_t max_wakeup = -1);
    ~BlockObject();

    static void CoSwitch();
    static bool SchedulerSwitch(Task* tk);

    bool SchedulerSwitch(BlockSentryBoundPtr bound_ptr);

    // 阻塞式等待信号
    void CoBlockWait();

    // 带超时的阻塞式等待信号
    // @returns: 是否成功等到信号
	bool CoBlockWaitTimed(MininumTimeDurationType timeo);

    template <typename R, typename P>
    bool CoBlockWaitTimed(std::chrono::duration<R, P> duration)
    {
        return CoBlockWaitTimed(std::chrono::duration_cast<MininumTimeDurationType>(duration));
    }

    template <typename Clock, typename Dur>
    bool CoBlockWaitTimed(std::chrono::time_point<Clock, Dur> const& deadline)
    {
        auto now = Clock::now();
        if (deadline < now)
            return CoBlockWaitTimed(MininumTimeDurationType(0));

        return CoBlockWaitTimed(std::chrono::duration_cast<MininumTimeDurationType>
                (deadline - Clock::now()));
    }

    bool TryBlockWait();

    bool Wakeup();

    bool IsWakeup();

private:
    void CancelWait(Task* tk, uint32_t block_sequence, bool in_timer = false);

    bool AddWaitTask(Task* tk);
};

} //namespace co
