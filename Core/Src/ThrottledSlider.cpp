#include "ThrottledSlider.h"

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

    connect(this, &QSlider::sliderPressed, this, [this]() {
        mApplyClock.invalidate();
    });
    connect(this, &QSlider::valueChanged, this, [this](int value) {
        if (!isSliderDown()
            || !mApplyClock.isValid()
            || mApplyClock.elapsed() >= mApplyIntervalMs) {
            emitThrottledValue(value);
        }
    });
    connect(this, &QSlider::sliderReleased, this, [this]() {
        emitThrottledValue(value());
    });
}

void ThrottledSlider::emitThrottledValue(int value)
{
    if (mHasLastEmittedValue && mLastEmittedValue == value) {
        return;
    }

    mLastEmittedValue = value;
    mHasLastEmittedValue = true;
    mApplyClock.restart();
    emit throttledValueChanged(value);
}
