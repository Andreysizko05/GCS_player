#ifndef THROTTLEDSLIDER_H
#define THROTTLEDSLIDER_H

#include <QElapsedTimer>
#include <QSlider>

class QTimer;

class ThrottledSlider : public QSlider
{
    Q_OBJECT

public:
    explicit ThrottledSlider(QWidget* parent = nullptr);
    explicit ThrottledSlider(Qt::Orientation orientation, QWidget* parent = nullptr);

    int applyIntervalMs() const;
    void setApplyIntervalMs(int intervalMs);

signals:
    void throttledValueChanged(int value);

private:
    void setupConnections();
    void emitThrottledValue(int value);
    void scheduleTrailingEmit(int value);

    QElapsedTimer mApplyClock;
    QTimer* mTrailingTimer = nullptr;
    int mApplyIntervalMs = 200;
    int mLastEmittedValue = 0;
    bool mHasLastEmittedValue = false;
    int mPendingValue = 0;
    bool mHasPendingValue = false;
};

#endif // THROTTLEDSLIDER_H
