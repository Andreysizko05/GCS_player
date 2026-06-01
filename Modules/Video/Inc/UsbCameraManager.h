#ifndef USBCAMERAMANAGER_H
#define USBCAMERAMANAGER_H

#include <QMap>
#include <QString>
#include <QVector>

struct UsbCameraDevice
{
    QString id;
    QString displayName;
    QString backend;
    int index = -1;
};

struct UsbCameraMode
{
    QString id;
    QString label;
    QString caps;
    QString format;
    int width = 0;
    int height = 0;
    int fpsNumerator = 0;
    int fpsDenominator = 1;
};

struct UsbCameraControlState
{
    int value = 0;
    bool automatic = false;
};

struct UsbCameraControl
{
    QString id;
    QString displayName;
    int minimum = 0;
    int maximum = 0;
    int step = 1;
    int defaultValue = 0;
    bool supportsAuto = false;
    bool defaultAutomatic = false;
    UsbCameraControlState state;
};

class UsbCameraManager
{
public:
    static QVector<UsbCameraDevice> devices();
    static QVector<UsbCameraMode> modes(const QString& deviceId, int deviceIndex = -1);
    static QVector<UsbCameraControl> controls(const QString& deviceId);

    static QString sourceFactoryName();
    static bool setControl(
        const QString& deviceId,
        const QString& controlId,
        const UsbCameraControlState& state,
        QString* errorMessage = nullptr
    );
};

#endif // USBCAMERAMANAGER_H
