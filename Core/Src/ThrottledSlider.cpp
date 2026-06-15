#include "ThrottledSlider.h"

#include <QTimer>

#include <algorithm>

ThrottledSlider::ThrottledSlider(QWidget* parent)
    : QSlider(parent)
{
    setupConnections();
}

ThrottledSlider::ThrottledSlider(Qt::Orientation orientation, QWidget* parent)
    : QSlider(orientation, parent)
{
    setupConnections();
}

int ThrottledSlider::applyIntervalMs() const
{
    return mApplyIntervalMs;
}

void ThrottledSlider::setApplyIntervalMs(int intervalMs)
{
    mApplyIntervalMs = std::max(0, intervalMs);
}

void ThrottledSlider::setupConnections()
{
    mLastEmittedValue = value();
    mHasLastEmittedValue = true;

    mTrailingTimer = new QTimer(this);
    mTrailingTimer->setSingleShot(true);
    connect(mTrailingTimer, &QTimer::timeout, this, [this]() {
        if (mHasPendingValue) {
            emitThrottledValue(mPendingValue);
        }
    });

    connect(this, &QSlider::sliderPressed, this, [this]() {
        mApplyClock.invalidate();
    });
    connect(this, &QSlider::valueChanged, this, [this](int value) {
        if (!isSliderDown()
            || !mApplyClock.isValid()
            || mApplyClock.elapsed() >= mApplyIntervalMs) {
            emitThrottledValue(value);
        } else {
            // Within the throttle window while dragging: remember the latest
            // value and schedule it to be flushed once the interval elapses,
            // so the final position is always delivered.
            scheduleTrailingEmit(value);
        }
    });
    connect(this, &QSlider::sliderReleased, this, [this]() {
        emitThrottledValue(value());
    });
}

void ThrottledSlider::emitThrottledValue(int value)
{
    // Any actual emit supersedes a pending trailing flush.
    mHasPendingValue = false;
    if (mTrailingTimer != nullptr) {
        mTrailingTimer->stop();
    }

    if (mHasLastEmittedValue && mLastEmittedValue == value) {
        return;
    }

    mLastEmittedValue = value;
    mHasLastEmittedValue = true;
    mApplyClock.restart();
    emit throttledValueChanged(value);
}

void ThrottledSlider::scheduleTrailingEmit(int value)
{
    mPendingValue = value;
    mHasPendingValue = true;

    const qint64 elapsed = mApplyClock.isValid() ? mApplyClock.elapsed() : 0;
    const int remaining =
        std::max(0, mApplyIntervalMs - static_cast<int>(elapsed));
    mTrailingTimer->start(remaining);
}
