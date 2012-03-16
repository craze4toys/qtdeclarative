/****************************************************************************
**
** Copyright (C) 2012 Nokia Corporation and/or its subsidiary(-ies).
** Contact: http://www.qt-project.org/
**
** This file is part of the QtQml module of the Qt Toolkit.
**
** $QT_BEGIN_LICENSE:LGPL$
** GNU Lesser General Public License Usage
** This file may be used under the terms of the GNU Lesser General Public
** License version 2.1 as published by the Free Software Foundation and
** appearing in the file LICENSE.LGPL included in the packaging of this
** file. Please review the following information to ensure the GNU Lesser
** General Public License version 2.1 requirements will be met:
** http://www.gnu.org/licenses/old-licenses/lgpl-2.1.html.
**
** In addition, as a special exception, Nokia gives you certain additional
** rights. These rights are described in the Nokia Qt LGPL Exception
** version 1.1, included in the file LGPL_EXCEPTION.txt in this package.
**
** GNU General Public License Usage
** Alternatively, this file may be used under the terms of the GNU General
** Public License version 3.0 as published by the Free Software Foundation
** and appearing in the file LICENSE.GPL included in the packaging of this
** file. Please review the following information to ensure the GNU General
** Public License version 3.0 requirements will be met:
** http://www.gnu.org/copyleft/gpl.html.
**
** Other Usage
** Alternatively, this file may be used in accordance with the terms and
** conditions contained in a signed written agreement between you and Nokia.
**
**
**
**
**
**
** $QT_END_LICENSE$
**
****************************************************************************/

#include <QtCore/qthreadstorage.h>

#include "private/qabstractanimationjob_p.h"
#include "private/qanimationgroupjob_p.h"
#include "private/qanimationjobutil_p.h"

#define DEFAULT_TIMER_INTERVAL 16

QT_BEGIN_NAMESPACE

#ifndef QT_NO_THREAD
Q_GLOBAL_STATIC(QThreadStorage<QQmlAnimationTimer *>, animationTimer)
#endif

QQmlAnimationTimer::QQmlAnimationTimer() :
    QAbstractAnimationTimer(), lastTick(0), lastDelta(0),
    currentAnimationIdx(0), insideTick(false),
    startAnimationPending(false), stopTimerPending(false),
    runningLeafAnimations(0)
{
}

QQmlAnimationTimer *QQmlAnimationTimer::instance(bool create)
{
    QQmlAnimationTimer *inst;
#ifndef QT_NO_THREAD
    if (create && !animationTimer()->hasLocalData()) {
        inst = new QQmlAnimationTimer;
        animationTimer()->setLocalData(inst);
    } else {
        inst = animationTimer() ? animationTimer()->localData() : 0;
    }
#else
    static QAnimationTimer unifiedTimer;
    inst = &unifiedTimer;
#endif
    return inst;
}

QQmlAnimationTimer *QQmlAnimationTimer::instance()
{
    return instance(true);
}

void QQmlAnimationTimer::ensureTimerUpdate()
{
    QQmlAnimationTimer *inst = QQmlAnimationTimer::instance(false);
    QUnifiedTimer *instU = QUnifiedTimer::instance(false);
    if (instU && inst && inst->isPaused)
        instU->updateAnimationTimers(-1);
}

void QQmlAnimationTimer::updateAnimationsTime(qint64 delta)
{
    //setCurrentTime can get this called again while we're the for loop. At least with pauseAnimations
    if (insideTick)
        return;

    lastTick += delta;
    lastDelta = delta;

    //we make sure we only call update time if the time has actually changed
    //it might happen in some cases that the time doesn't change because events are delayed
    //when the CPU load is high
    if (delta) {
        insideTick = true;
        for (currentAnimationIdx = 0; currentAnimationIdx < animations.count(); ++currentAnimationIdx) {
            QAbstractAnimationJob *animation = animations.at(currentAnimationIdx);
            int elapsed = animation->m_totalCurrentTime
                          + (animation->direction() == QAbstractAnimationJob::Forward ? delta : -delta);
            animation->setCurrentTime(elapsed);
        }
        insideTick = false;
        currentAnimationIdx = 0;
    }
}

void QQmlAnimationTimer::updateAnimationTimer()
{
    QQmlAnimationTimer *inst = QQmlAnimationTimer::instance(false);
    if (inst)
        inst->restartAnimationTimer();
}

void QQmlAnimationTimer::restartAnimationTimer()
{
    if (runningLeafAnimations == 0 && !runningPauseAnimations.isEmpty())
        QUnifiedTimer::pauseAnimationTimer(this, closestPauseAnimationTimeToFinish());
    else if (isPaused)
        QUnifiedTimer::resumeAnimationTimer(this);
    else if (!isRegistered)
        QUnifiedTimer::startAnimationTimer(this);
}

void QQmlAnimationTimer::startAnimations()
{
    startAnimationPending = false;
    //force timer to update, which prevents large deltas for our newly added animations
    if (!animations.isEmpty())
        QUnifiedTimer::instance()->maybeUpdateAnimationsToCurrentTime();

    //we transfer the waiting animations into the "really running" state
    animations += animationsToStart;
    animationsToStart.clear();
    if (!animations.isEmpty())
        restartAnimationTimer();
}

void QQmlAnimationTimer::stopTimer()
{
    stopTimerPending = false;
    if (animations.isEmpty()) {
        QUnifiedTimer::resumeAnimationTimer(this);
        QUnifiedTimer::stopAnimationTimer(this);
        // invalidate the start reference time
        lastTick = 0;
        lastDelta = 0;
    }
}

void QQmlAnimationTimer::registerAnimation(QAbstractAnimationJob *animation, bool isTopLevel)
{
    if (animation->userControlDisabled())
        return;

    QQmlAnimationTimer *inst = instance(true); //we create the instance if needed
    inst->registerRunningAnimation(animation);
    if (isTopLevel) {
        Q_ASSERT(!animation->m_hasRegisteredTimer);
        animation->m_hasRegisteredTimer = true;
        inst->animationsToStart << animation;
        if (!inst->startAnimationPending) {
            inst->startAnimationPending = true;
            QMetaObject::invokeMethod(inst, "startAnimations", Qt::QueuedConnection);
        }
    }
}

void QQmlAnimationTimer::unregisterAnimation(QAbstractAnimationJob *animation)
{
    QQmlAnimationTimer *inst = QQmlAnimationTimer::instance(false);
    if (inst) {
        //at this point the unified timer should have been created
        //but it might also have been already destroyed in case the application is shutting down

        inst->unregisterRunningAnimation(animation);

        if (!animation->m_hasRegisteredTimer)
            return;

        int idx = inst->animations.indexOf(animation);
        if (idx != -1) {
            inst->animations.removeAt(idx);
            // this is needed if we unregister an animation while its running
            if (idx <= inst->currentAnimationIdx)
                --inst->currentAnimationIdx;

            if (inst->animations.isEmpty() && !inst->stopTimerPending) {
                inst->stopTimerPending = true;
                QMetaObject::invokeMethod(inst, "stopTimer", Qt::QueuedConnection);
            }
        } else {
            inst->animationsToStart.removeOne(animation);
        }
    }
    animation->m_hasRegisteredTimer = false;
}

void QQmlAnimationTimer::registerRunningAnimation(QAbstractAnimationJob *animation)
{
    Q_ASSERT(!animation->userControlDisabled());

    if (animation->m_isGroup)
        return;

    if (animation->m_isPause) {
        runningPauseAnimations << animation;
    } else
        runningLeafAnimations++;
}

void QQmlAnimationTimer::unregisterRunningAnimation(QAbstractAnimationJob *animation)
{
    if (animation->userControlDisabled())
        return;

    if (animation->m_isGroup)
        return;

    if (animation->m_isPause)
        runningPauseAnimations.removeOne(animation);
    else
        runningLeafAnimations--;
    Q_ASSERT(runningLeafAnimations >= 0);
}

int QQmlAnimationTimer::closestPauseAnimationTimeToFinish()
{
    int closestTimeToFinish = INT_MAX;
    for (int i = 0; i < runningPauseAnimations.size(); ++i) {
        QAbstractAnimationJob *animation = runningPauseAnimations.at(i);
        int timeToFinish;

        if (animation->direction() == QAbstractAnimationJob::Forward)
            timeToFinish = animation->duration() - animation->currentLoopTime();
        else
            timeToFinish = animation->currentLoopTime();

        if (timeToFinish < closestTimeToFinish)
            closestTimeToFinish = timeToFinish;
    }
    return closestTimeToFinish;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////

QAbstractAnimationJob::QAbstractAnimationJob()
    : m_loopCount(1)
    , m_group(0)
    , m_direction(QAbstractAnimationJob::Forward)
    , m_state(QAbstractAnimationJob::Stopped)
    , m_totalCurrentTime(0)
    , m_currentTime(0)
    , m_currentLoop(0)
    , m_uncontrolledFinishTime(-1)
    , m_nextSibling(0)
    , m_previousSibling(0)
    , m_wasDeleted(0)
    , m_hasRegisteredTimer(false)
    , m_isPause(false)
    , m_isGroup(false)
    , m_disableUserControl(false)
{
}

QAbstractAnimationJob::~QAbstractAnimationJob()
{
    if (m_wasDeleted)
        *m_wasDeleted = true;

    //we can't call stop here. Otherwise we get pure virtual calls
    if (m_state != Stopped) {
        State oldState = m_state;
        m_state = Stopped;
        stateChanged(oldState, m_state);
        if (oldState == Running)
            QQmlAnimationTimer::unregisterAnimation(this);
    }

    if (m_group)
        m_group->removeAnimation(this);
}

void QAbstractAnimationJob::setState(QAbstractAnimationJob::State newState)
{
    if (m_state == newState)
        return;

    if (m_loopCount == 0)
        return;

    State oldState = m_state;
    int oldCurrentTime = m_currentTime;
    int oldCurrentLoop = m_currentLoop;
    Direction oldDirection = m_direction;

    // check if we should Rewind
    if ((newState == Paused || newState == Running) && oldState == Stopped) {
        //here we reset the time if needed
        //we don't call setCurrentTime because this might change the way the animation
        //behaves: changing the state or changing the current value
        m_totalCurrentTime = m_currentTime = (m_direction == Forward) ?
            0 : (m_loopCount == -1 ? duration() : totalDuration());
    }

    m_state = newState;
    //(un)registration of the animation must always happen before calls to
    //virtual function (updateState) to ensure a correct state of the timer
    bool isTopLevel = !m_group || m_group->isStopped();
    if (oldState == Running) {
        if (newState == Paused && m_hasRegisteredTimer)
            QQmlAnimationTimer::ensureTimerUpdate();
        //the animation, is not running any more
        QQmlAnimationTimer::unregisterAnimation(this);
    } else if (newState == Running) {
        QQmlAnimationTimer::registerAnimation(this, isTopLevel);
    }

    //starting an animation qualifies as a top level loop change
    if (newState == Running && oldState == Stopped && !m_group)
        topLevelAnimationLoopChanged();

    RETURN_IF_DELETED(updateState(newState, oldState));

    if (newState != m_state) //this is to be safe if updateState changes the state
        return;

    // Notify state change
    stateChanged(newState, oldState);
    if (newState != m_state) //this is to be safe if updateState changes the state
        return;

    switch (m_state) {
    case Paused:
        break;
    case Running:
        {
            // this ensures that the value is updated now that the animation is running
            if (oldState == Stopped) {
                if (isTopLevel) {
                    // currentTime needs to be updated if pauseTimer is active
                    QQmlAnimationTimer::ensureTimerUpdate();
                    setCurrentTime(m_totalCurrentTime);
                }
            }
        }
        break;
    case Stopped:
        // Leave running state.
        int dura = duration();

        if (dura == -1 || m_loopCount < 0
            || (oldDirection == Forward && (oldCurrentTime * (oldCurrentLoop + 1)) == (dura * m_loopCount))
            || (oldDirection == Backward && oldCurrentTime == 0)) {
               finished();
        }
        break;
    }
}

void QAbstractAnimationJob::setDirection(Direction direction)
{
    if (m_direction == direction)
        return;

    if (m_state == Stopped) {
        if (m_direction == Backward) {
            m_currentTime = duration();
            m_currentLoop = m_loopCount - 1;
        } else {
            m_currentTime = 0;
            m_currentLoop = 0;
        }
    }

    // the commands order below is important: first we need to setCurrentTime with the old direction,
    // then update the direction on this and all children and finally restart the pauseTimer if needed
    if (m_hasRegisteredTimer)
        QQmlAnimationTimer::ensureTimerUpdate();

    m_direction = direction;
    updateDirection(direction);

    if (m_hasRegisteredTimer)
        // needed to update the timer interval in case of a pause animation
        QQmlAnimationTimer::updateAnimationTimer();
}

void QAbstractAnimationJob::setLoopCount(int loopCount)
{
    m_loopCount = loopCount;
}

int QAbstractAnimationJob::totalDuration() const
{
    int dura = duration();
    if (dura <= 0)
        return dura;
    int loopcount = loopCount();
    if (loopcount < 0)
        return -1;
    return dura * loopcount;
}

void QAbstractAnimationJob::setCurrentTime(int msecs)
{
    msecs = qMax(msecs, 0);
    // Calculate new time and loop.
    int dura = duration();
    int totalDura = dura <= 0 ? dura : ((m_loopCount < 0) ? -1 : dura * m_loopCount);
    if (totalDura != -1)
        msecs = qMin(totalDura, msecs);
    m_totalCurrentTime = msecs;

    // Update new values.
    int oldLoop = m_currentLoop;
    m_currentLoop = ((dura <= 0) ? 0 : (msecs / dura));
    if (m_currentLoop == m_loopCount) {
        //we're at the end
        m_currentTime = qMax(0, dura);
        m_currentLoop = qMax(0, m_loopCount - 1);
    } else {
        if (m_direction == Forward) {
            m_currentTime = (dura <= 0) ? msecs : (msecs % dura);
        } else {
            m_currentTime = (dura <= 0) ? msecs : ((msecs - 1) % dura) + 1;
            if (m_currentTime == dura)
                --m_currentLoop;
        }
    }

    if (m_currentLoop != oldLoop && !m_group)   //### verify Running as well?
        topLevelAnimationLoopChanged();

    RETURN_IF_DELETED(updateCurrentTime(m_currentTime));

    if (m_currentLoop != oldLoop)
        currentLoopChanged(m_currentLoop);

    // All animations are responsible for stopping the animation when their
    // own end state is reached; in this case the animation is time driven,
    // and has reached the end.
    if ((m_direction == Forward && m_totalCurrentTime == totalDura)
        || (m_direction == Backward && m_totalCurrentTime == 0)) {
        stop();
    }
}

void QAbstractAnimationJob::start()
{
    if (m_state == Running)
        return;
    setState(Running);
}

void QAbstractAnimationJob::stop()
{
    if (m_state == Stopped)
        return;
    setState(Stopped);
}

void QAbstractAnimationJob::pause()
{
    if (m_state == Stopped) {
        qWarning("QAbstractAnimationJob::pause: Cannot pause a stopped animation");
        return;
    }

    setState(Paused);
}

void QAbstractAnimationJob::resume()
{
    if (m_state != Paused) {
        qWarning("QAbstractAnimationJob::resume: "
                 "Cannot resume an animation that is not paused");
        return;
    }
    setState(Running);
}

void QAbstractAnimationJob::setEnableUserControl()
{
    m_disableUserControl = false;
}

bool QAbstractAnimationJob::userControlDisabled() const
{
    return m_disableUserControl;
}

void QAbstractAnimationJob::setDisableUserControl()
{
    m_disableUserControl = true;
    start();
    pause();
}

void QAbstractAnimationJob::updateState(QAbstractAnimationJob::State newState,
                                     QAbstractAnimationJob::State oldState)
{
    Q_UNUSED(oldState);
    Q_UNUSED(newState);
}

void QAbstractAnimationJob::updateDirection(QAbstractAnimationJob::Direction direction)
{
    Q_UNUSED(direction);
}

void QAbstractAnimationJob::finished()
{
    //TODO: update this code so it is valid to delete the animation in animationFinished
    for (int i = 0; i < changeListeners.count(); ++i) {
        const QAbstractAnimationJob::ChangeListener &change = changeListeners.at(i);
        if (change.types & QAbstractAnimationJob::Completion) {
            RETURN_IF_DELETED(change.listener->animationFinished(this));
        }
    }

    if (m_group && (duration() == -1 || loopCount() < 0)) {
        //this is an uncontrolled animation, need to notify the group animation we are finished
        m_group->uncontrolledAnimationFinished(this);
    }
}

void QAbstractAnimationJob::stateChanged(QAbstractAnimationJob::State newState, QAbstractAnimationJob::State oldState)
{
    for (int i = 0; i < changeListeners.count(); ++i) {
        const QAbstractAnimationJob::ChangeListener &change = changeListeners.at(i);
        if (change.types & QAbstractAnimationJob::StateChange) {
            RETURN_IF_DELETED(change.listener->animationStateChanged(this, newState, oldState));
        }
    }
}

void QAbstractAnimationJob::currentLoopChanged(int currentLoop)
{
    Q_UNUSED(currentLoop);
    for (int i = 0; i < changeListeners.count(); ++i) {
        const QAbstractAnimationJob::ChangeListener &change = changeListeners.at(i);
        if (change.types & QAbstractAnimationJob::CurrentLoop) {
           RETURN_IF_DELETED(change.listener->animationCurrentLoopChanged(this));
        }
    }
}

void QAbstractAnimationJob::addAnimationChangeListener(QAnimationJobChangeListener *listener, QAbstractAnimationJob::ChangeTypes changes)
{
    changeListeners.append(ChangeListener(listener, changes));
}

void QAbstractAnimationJob::removeAnimationChangeListener(QAnimationJobChangeListener *listener, QAbstractAnimationJob::ChangeTypes changes)
{
    changeListeners.removeOne(ChangeListener(listener, changes));
}

QT_END_NAMESPACE

//#include "moc_qabstractanimation2_p.cpp"
