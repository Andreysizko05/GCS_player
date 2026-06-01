#ifndef THROTTLEDSLIDER_H
#define THROTTLEDSLIDER_H

#include <QElapsedTimer>
#include <QSlider>

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

    QElapsedTimer mApplyClock;
    int mApplyIntervalMs = 200;
    int mLastEmittedValue = 0;
    bool mHasLastEmittedValue = false;
};

#endif // THROTTLEDSLIDER_H
