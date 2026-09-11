#ifndef USBCAMERAMANAGER_H
#define USBCAMERAMANAGER_H

#include <QMap>
#include <QString>
#include <QVector>

struct UsbCameraDevice
{
    // Stable identity used by the UI (combo box data, persisted settings) and by
    // controls()/setControl(). On Windows this is the DirectShow moniker display name whenever the
    // device is visible to DirectShow, otherwise the capture path. Not a stream selector.
    QString id;
    QString displayName;
    // Name of the API that will actually open the stream ("Media Foundation", "DirectShow", ...).
    QString backend;
    // Ordinal inside the enumeration `id` came from (DirectShow on Windows). Never pass this to a
    // capture element - use sourceIndex.
    int index = -1;

    // --- Stream opening. Use these three, never `id`/`index`. ---
    // GStreamer element factory that sourcePath/sourceIndex belong to, e.g. "mfvideosrc",
    // "ksvideosrc", "v4l2src". Empty when no GStreamer provider exposed this device, in which case
    // the caller must fall back to sourceFactoryName() plus a device-name selector.
    QString sourceElement;
    // Native device path that sourceElement accepts. For mfvideosrc this is the Media Foundation
    // symbolic link, i.e. the {e5323777-f976-4f5b-9b55-b94699c46e44} form with no "@device:pnp:"
    // prefix - the DirectShow {65e8773d-...} form and the prefixed form are both rejected by it.
    QString sourcePath;
    // Ordinal inside the SAME enumeration sourceElement uses; -1 when unknown. A DirectShow ordinal
    // must never reach a Media Foundation element: the two enumerations differ in length and order,
    // so the index either opens the wrong camera or fails the state change outright.
    int sourceIndex = -1;

    // --- Control binding only (IAMVideoProcAmp / IAMCameraControl). ---
    // DirectShow moniker display name; empty when the device is invisible to DirectShow, which means
    // "no UVC controls" rather than "unusable". Never pass this to a capture element as a path.
    QString controlId;
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
