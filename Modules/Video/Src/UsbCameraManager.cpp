#include "UsbCameraManager.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QSet>
#include <QStringList>
#include <QtGlobal>

#include <algorithm>
#include <limits>
#include <numeric>

#include <gst/gst.h>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <dshow.h>
#include <dvdmedia.h>
#endif

namespace
{
void prependPath(const QString& path)
{
    if (path.isEmpty()) {
        return;
    }

    QByteArray currentPath = qgetenv("PATH");
#ifdef Q_OS_WIN
    const char separator = ';';
#else
    const char separator = ':';
#endif
    const QByteArray nativePath = QDir::toNativeSeparators(path).toUtf8();
    if (currentPath.isEmpty()) {
        qputenv("PATH", nativePath);
    } else if (!currentPath.split(separator).contains(nativePath)) {
        qputenv("PATH", nativePath + QByteArray(1, separator) + currentPath);
    }
}

void setEnvIfPathExists(const char* name, const QString& path)
{
    if (QFileInfo::exists(path)) {
        qputenv(name, QDir::toNativeSeparators(path).toUtf8());
    }
}

QString findFirstExistingPath(const QStringList& candidates)
{
    for (const QString& candidate : candidates) {
        if (QFileInfo::exists(candidate)) {
            return candidate;
        }
    }
    return {};
}

void prepareGStreamerEnvironment()
{
    const QString appDir = QCoreApplication::applicationDirPath();
    const QString pluginDir = QDir(appDir).filePath(QStringLiteral("gstreamer-1.0"));
    const QString gioModulesDir = QDir(appDir).filePath(QStringLiteral("gio/modules"));
    const QString runtimeDir = QDir(appDir).filePath(QStringLiteral("gstreamer-runtime"));
    const QString toolsDir = QDir(appDir).filePath(QStringLiteral("gstreamer-tools"));
#ifdef Q_OS_WIN
    const QString scannerFileName = QStringLiteral("gst-plugin-scanner.exe");
#else
    const QString scannerFileName = QStringLiteral("gst-plugin-scanner");
#endif
    const QString scannerPath = findFirstExistingPath({
        QDir(toolsDir).filePath(scannerFileName),
        QDir(QDir(toolsDir).filePath(QStringLiteral("gstreamer-1.0"))).filePath(scannerFileName)
    });
    const bool useBundledRuntime = QFileInfo::exists(pluginDir) && QFileInfo::exists(scannerPath);

    prependPath(appDir);

    if (useBundledRuntime) {
        prependPath(runtimeDir);
        prependPath(toolsDir);
        setEnvIfPathExists("GST_PLUGIN_PATH", pluginDir);
        setEnvIfPathExists("GST_PLUGIN_PATH_1_0", pluginDir);
        setEnvIfPathExists("GST_PLUGIN_SYSTEM_PATH", pluginDir);
        setEnvIfPathExists("GST_PLUGIN_SYSTEM_PATH_1_0", pluginDir);
        setEnvIfPathExists("GIO_EXTRA_MODULES", gioModulesDir);
        setEnvIfPathExists("GST_PLUGIN_SCANNER", scannerPath);
        setEnvIfPathExists("GST_PLUGIN_SCANNER_1_0", scannerPath);
    }

    qputenv("GST_REGISTRY_FORK", QByteArrayLiteral("no"));
    qputenv("GST_REGISTRY_REUSE_PLUGIN_SCANNER", QByteArrayLiteral("no"));
}

bool ensureGStreamerReady()
{
    static bool attempted = false;
    static bool initialized = false;
    if (attempted) {
        return initialized;
    }

    attempted = true;
    prepareGStreamerEnvironment();

    GError* error = nullptr;
    initialized = gst_init_check(nullptr, nullptr, &error);
    if (error != nullptr) {
        g_error_free(error);
    }
    return initialized;
}

QString valueStringFromStructure(const GstStructure* structure, const char* field)
{
    const char* stringValue = gst_structure_get_string(structure, field);
    if (stringValue != nullptr) {
        return QString::fromUtf8(stringValue);
    }

    const GValue* value = gst_structure_get_value(structure, field);
    if (value == nullptr) {
        return {};
    }

    gchar* text = g_strdup_value_contents(value);
    const QString result = QString::fromUtf8(text).remove('"');
    g_free(text);
    return result;
}

bool intFromStructure(const GstStructure* structure, const char* field, int& result)
{
    const GValue* value = gst_structure_get_value(structure, field);
    if (value == nullptr) {
        return false;
    }
    if (G_VALUE_HOLDS_INT(value)) {
        result = g_value_get_int(value);
        return true;
    }
    if (GST_VALUE_HOLDS_INT_RANGE(value)) {
        result = gst_value_get_int_range_min(value);
        return true;
    }

    return false;
}

bool fractionFromStructure(const GstStructure* structure, int& numerator, int& denominator)
{
    const GValue* value = gst_structure_get_value(structure, "framerate");
    if (value == nullptr) {
        return false;
    }
    if (GST_VALUE_HOLDS_FRACTION(value)) {
        numerator = gst_value_get_fraction_numerator(value);
        denominator = gst_value_get_fraction_denominator(value);
        return denominator != 0;
    }
    if (GST_VALUE_HOLDS_FRACTION_RANGE(value)) {
        const GValue* maximum = gst_value_get_fraction_range_max(value);
        numerator = gst_value_get_fraction_numerator(maximum);
        denominator = gst_value_get_fraction_denominator(maximum);
        return denominator != 0;
    }

    return false;
}

#ifdef Q_OS_WIN
class ComScope
{
public:
    ComScope()
        : m_result(CoInitializeEx(nullptr, COINIT_MULTITHREADED))
        , m_uninitialize(SUCCEEDED(m_result))
    {
    }

    ~ComScope()
    {
        if (m_uninitialize) {
            CoUninitialize();
        }
    }

    bool ok() const
    {
        return SUCCEEDED(m_result) || m_result == RPC_E_CHANGED_MODE;
    }

private:
    HRESULT m_result = E_FAIL;
    bool m_uninitialize = false;
};

template <typename T>
void releaseCom(T*& pointer)
{
    if (pointer != nullptr) {
        pointer->Release();
        pointer = nullptr;
    }
}

QString stringFromOleString(LPOLESTR text)
{
    if (text == nullptr) {
        return {};
    }

    const QString result = QString::fromWCharArray(text);
    CoTaskMemFree(text);
    return result;
}

QString monikerDisplayName(IMoniker* moniker)
{
    if (moniker == nullptr) {
        return {};
    }

    IBindCtx* bindContext = nullptr;
    if (FAILED(CreateBindCtx(0, &bindContext)) || bindContext == nullptr) {
        return {};
    }

    LPOLESTR displayName = nullptr;
    if (FAILED(moniker->GetDisplayName(bindContext, nullptr, &displayName))) {
        releaseCom(bindContext);
        return {};
    }
    releaseCom(bindContext);

    return stringFromOleString(displayName);
}

QString monikerFriendlyName(IMoniker* moniker)
{
    if (moniker == nullptr) {
        return {};
    }

    IPropertyBag* propertyBag = nullptr;
    if (FAILED(moniker->BindToStorage(nullptr, nullptr, IID_IPropertyBag, reinterpret_cast<void**>(&propertyBag)))
        || propertyBag == nullptr) {
        return {};
    }

    VARIANT value;
    VariantInit(&value);
    QString result;
    if (SUCCEEDED(propertyBag->Read(L"FriendlyName", &value, nullptr)) && value.vt == VT_BSTR) {
        result = QString::fromWCharArray(value.bstrVal);
    }
    VariantClear(&value);
    releaseCom(propertyBag);
    return result;
}

IEnumMoniker* videoDeviceEnumerator()
{
    ICreateDevEnum* deviceEnumerator = nullptr;
    if (FAILED(CoCreateInstance(
            CLSID_SystemDeviceEnum,
            nullptr,
            CLSCTX_INPROC_SERVER,
            IID_ICreateDevEnum,
            reinterpret_cast<void**>(&deviceEnumerator)))
        || deviceEnumerator == nullptr) {
        return nullptr;
    }

    IEnumMoniker* enumMoniker = nullptr;
    const HRESULT result = deviceEnumerator->CreateClassEnumerator(
        CLSID_VideoInputDeviceCategory,
        &enumMoniker,
        0
    );
    releaseCom(deviceEnumerator);

    return result == S_OK ? enumMoniker : nullptr;
}

IMoniker* findVideoDeviceMoniker(const QString& deviceId)
{
    IEnumMoniker* enumMoniker = videoDeviceEnumerator();
    if (enumMoniker == nullptr) {
        return nullptr;
    }

    IMoniker* moniker = nullptr;
    ULONG fetched = 0;
    while (enumMoniker->Next(1, &moniker, &fetched) == S_OK) {
        const QString displayName = monikerDisplayName(moniker);
        const QString friendlyName = monikerFriendlyName(moniker);
        if (deviceId.isEmpty()
            || displayName == deviceId
            || friendlyName == deviceId) {
            releaseCom(enumMoniker);
            return moniker;
        }

        releaseCom(moniker);
    }

    releaseCom(enumMoniker);
    return nullptr;
}

IBaseFilter* bindVideoFilter(const QString& deviceId)
{
    IMoniker* moniker = findVideoDeviceMoniker(deviceId);
    if (moniker == nullptr) {
        return nullptr;
    }

    IBaseFilter* filter = nullptr;
    moniker->BindToObject(nullptr, nullptr, IID_IBaseFilter, reinterpret_cast<void**>(&filter));
    releaseCom(moniker);
    return filter;
}

IAMStreamConfig* findStreamConfig(IBaseFilter* filter)
{
    if (filter == nullptr) {
        return nullptr;
    }

    IEnumPins* enumPins = nullptr;
    if (FAILED(filter->EnumPins(&enumPins)) || enumPins == nullptr) {
        return nullptr;
    }

    IPin* pin = nullptr;
    ULONG fetched = 0;
    while (enumPins->Next(1, &pin, &fetched) == S_OK) {
        PIN_DIRECTION direction = PINDIR_INPUT;
        if (SUCCEEDED(pin->QueryDirection(&direction)) && direction == PINDIR_OUTPUT) {
            IAMStreamConfig* streamConfig = nullptr;
            if (SUCCEEDED(pin->QueryInterface(IID_IAMStreamConfig, reinterpret_cast<void**>(&streamConfig)))
                && streamConfig != nullptr) {
                releaseCom(pin);
                releaseCom(enumPins);
                return streamConfig;
            }
        }

        releaseCom(pin);
    }

    releaseCom(enumPins);
    return nullptr;
}

void freeMediaType(AM_MEDIA_TYPE* mediaType)
{
    if (mediaType == nullptr) {
        return;
    }

    if (mediaType->cbFormat != 0 && mediaType->pbFormat != nullptr) {
        CoTaskMemFree(mediaType->pbFormat);
    }
    if (mediaType->pUnk != nullptr) {
        mediaType->pUnk->Release();
    }
    CoTaskMemFree(mediaType);
}

QString fourccFromGuid(const GUID& guid)
{
    const char chars[] = {
        static_cast<char>(guid.Data1 & 0xff),
        static_cast<char>((guid.Data1 >> 8) & 0xff),
        static_cast<char>((guid.Data1 >> 16) & 0xff),
        static_cast<char>((guid.Data1 >> 24) & 0xff),
        '\0'
    };

    for (int i = 0; i < 4; ++i) {
        if (chars[i] < 32 || chars[i] > 126) {
            return {};
        }
    }

    return QString::fromLatin1(chars, 4).trimmed();
}

QString formatNameFromSubtype(const GUID& subtype)
{
    if (IsEqualGUID(subtype, MEDIASUBTYPE_RGB24)) {
        return QStringLiteral("BGR");
    }
    if (IsEqualGUID(subtype, MEDIASUBTYPE_RGB32)) {
        return QStringLiteral("BGRx");
    }
    if (IsEqualGUID(subtype, MEDIASUBTYPE_RGB565)) {
        return QStringLiteral("BGR16");
    }
    if (IsEqualGUID(subtype, MEDIASUBTYPE_RGB555)) {
        return QStringLiteral("BGR15");
    }
    if (IsEqualGUID(subtype, MEDIASUBTYPE_YUY2)) {
        return QStringLiteral("YUY2");
    }
    if (IsEqualGUID(subtype, MEDIASUBTYPE_UYVY)) {
        return QStringLiteral("UYVY");
    }
    if (IsEqualGUID(subtype, MEDIASUBTYPE_MJPG)) {
        return QStringLiteral("MJPG");
    }

    const QString fourcc = fourccFromGuid(subtype);
    if (fourcc.compare(QStringLiteral("I420"), Qt::CaseInsensitive) == 0) {
        return QStringLiteral("I420");
    }
    return fourcc.isEmpty() ? QStringLiteral("Video") : fourcc;
}

QString capsMediaTypeForFormat(const QString& format)
{
    if (format.compare(QStringLiteral("MJPG"), Qt::CaseInsensitive) == 0
        || format.compare(QStringLiteral("JPEG"), Qt::CaseInsensitive) == 0) {
        return QStringLiteral("image/jpeg");
    }
    if (format.compare(QStringLiteral("H264"), Qt::CaseInsensitive) == 0
        || format.compare(QStringLiteral("AVC1"), Qt::CaseInsensitive) == 0) {
        return QStringLiteral("video/x-h264");
    }

    return QStringLiteral("video/x-raw");
}

QString capsStringForMode(const QString& format, int width, int height, int fpsNumerator, int fpsDenominator)
{
    QString caps = capsMediaTypeForFormat(format);
    if (caps == QStringLiteral("video/x-raw") && !format.isEmpty() && format != QStringLiteral("Video")) {
        caps += QStringLiteral(",format=%1").arg(format);
    }
    if (width > 0) {
        caps += QStringLiteral(",width=%1").arg(width);
    }
    if (height > 0) {
        caps += QStringLiteral(",height=%1").arg(height);
    }
    if (fpsNumerator > 0 && fpsDenominator > 0) {
        caps += QStringLiteral(",framerate=%1/%2").arg(fpsNumerator).arg(fpsDenominator);
    }

    return caps;
}

void reduceFraction(int& numerator, int& denominator)
{
    if (numerator <= 0 || denominator <= 0) {
        return;
    }

    const int divisor = std::gcd(numerator, denominator);
    if (divisor > 1) {
        numerator /= divisor;
        denominator /= divisor;
    }
}

UsbCameraMode modeFromGstStructure(const GstStructure* structure)
{
    UsbCameraMode mode;
    if (structure == nullptr) {
        return mode;
    }

    const QString mediaType = QString::fromUtf8(gst_structure_get_name(structure));
    if (mediaType != QStringLiteral("video/x-raw") && mediaType != QStringLiteral("image/jpeg")) {
        return mode;
    }

    if (!intFromStructure(structure, "width", mode.width)
        || !intFromStructure(structure, "height", mode.height)
        || !fractionFromStructure(structure, mode.fpsNumerator, mode.fpsDenominator)) {
        return mode;
    }

    reduceFraction(mode.fpsNumerator, mode.fpsDenominator);

    mode.format = mediaType == QStringLiteral("image/jpeg")
        ? QStringLiteral("MJPEG")
        : valueStringFromStructure(structure, "format");
    if (mode.format.isEmpty()) {
        mode.format = QStringLiteral("RAW");
    }

    if (mediaType == QStringLiteral("image/jpeg")) {
        mode.caps = QStringLiteral("image/jpeg");
    } else {
        mode.caps = QStringLiteral("video/x-raw,format=%1").arg(mode.format);
    }
    mode.caps += QStringLiteral(",width=%1,height=%2,framerate=%3/%4")
        .arg(mode.width)
        .arg(mode.height)
        .arg(mode.fpsNumerator)
        .arg(mode.fpsDenominator);
    mode.id = mode.caps;

    const double fps = mode.fpsDenominator > 0
        ? static_cast<double>(mode.fpsNumerator) / static_cast<double>(mode.fpsDenominator)
        : 0.0;
    mode.label = QStringLiteral("%1 x %2 %3 @ %4 fps")
        .arg(mode.width)
        .arg(mode.height)
        .arg(mode.format)
        .arg(fps, 0, 'f', fps == static_cast<int>(fps) ? 0 : 2);

    return mode;
}

void sortModes(QVector<UsbCameraMode>& modes)
{
    std::sort(modes.begin(), modes.end(), [](const UsbCameraMode& lhs, const UsbCameraMode& rhs) {
        const int lhsPixels = lhs.width * lhs.height;
        const int rhsPixels = rhs.width * rhs.height;
        if (lhsPixels != rhsPixels) {
            return lhsPixels > rhsPixels;
        }
        const double lhsFps = lhs.fpsDenominator > 0
            ? static_cast<double>(lhs.fpsNumerator) / lhs.fpsDenominator
            : 0.0;
        const double rhsFps = rhs.fpsDenominator > 0
            ? static_cast<double>(rhs.fpsNumerator) / rhs.fpsDenominator
            : 0.0;
        if (lhsFps != rhsFps) {
            return lhsFps > rhsFps;
        }
        return lhs.format < rhs.format;
    });
}

QVector<UsbCameraMode> modesFromGStreamerDeviceCaps(const QString& preferredName, int preferredIndex)
{
    QVector<UsbCameraMode> result;
    if (!ensureGStreamerReady()) {
        return result;
    }

    GstDeviceMonitor* monitor = gst_device_monitor_new();
    if (monitor == nullptr) {
        return result;
    }

    gst_device_monitor_add_filter(monitor, "Video/Source", nullptr);
    if (!gst_device_monitor_start(monitor)) {
        gst_object_unref(monitor);
        return result;
    }

    GList* devices = gst_device_monitor_get_devices(monitor);
    int mediaFoundationIndex = 0;
    GstDevice* matchedDevice = nullptr;
    for (GList* item = devices; item != nullptr; item = item->next) {
        GstDevice* device = GST_DEVICE(item->data);
        GstStructure* properties = gst_device_get_properties(device);
        const char* api = properties != nullptr
            ? gst_structure_get_string(properties, "device.api")
            : nullptr;
        const bool isMediaFoundation = api != nullptr && qstrcmp(api, "mediafoundation") == 0;
        if (isMediaFoundation) {
            gchar* displayNameRaw = gst_device_get_display_name(device);
            const QString displayName = QString::fromUtf8(displayNameRaw != nullptr ? displayNameRaw : "");
            g_free(displayNameRaw);
            const bool nameMatches = !preferredName.isEmpty()
                && displayName.compare(preferredName, Qt::CaseInsensitive) == 0;
            const bool indexMatches = preferredIndex >= 0 && preferredIndex == mediaFoundationIndex;
            if (nameMatches || indexMatches || (preferredName.isEmpty() && preferredIndex < 0 && matchedDevice == nullptr)) {
                matchedDevice = GST_DEVICE(gst_object_ref(device));
                if (properties != nullptr) {
                    gst_structure_free(properties);
                }
                break;
            }
            ++mediaFoundationIndex;
        }
        if (properties != nullptr) {
            gst_structure_free(properties);
        }
    }

    if (matchedDevice != nullptr) {
        GstCaps* caps = gst_device_get_caps(matchedDevice);
        if (caps != nullptr) {
            QSet<QString> seenCaps;
            for (guint index = 0; index < gst_caps_get_size(caps); ++index) {
                const GstStructure* structure = gst_caps_get_structure(caps, index);
                UsbCameraMode mode = modeFromGstStructure(structure);
                if (!mode.caps.isEmpty() && !seenCaps.contains(mode.caps)) {
                    seenCaps.insert(mode.caps);
                    result.append(mode);
                }
            }
            gst_caps_unref(caps);
        }
        gst_object_unref(matchedDevice);
    }

    g_list_free_full(devices, reinterpret_cast<GDestroyNotify>(gst_object_unref));
    gst_device_monitor_stop(monitor);
    gst_object_unref(monitor);

    sortModes(result);
    return result;
}

UsbCameraMode modeFromMediaType(AM_MEDIA_TYPE* mediaType)
{
    UsbCameraMode mode;
    if (mediaType == nullptr || mediaType->pbFormat == nullptr) {
        return mode;
    }

    BITMAPINFOHEADER bitmapInfo = {};
    REFERENCE_TIME frameTime = 0;
    if (mediaType->formattype == FORMAT_VideoInfo && mediaType->cbFormat >= sizeof(VIDEOINFOHEADER)) {
        const auto* videoInfo = reinterpret_cast<const VIDEOINFOHEADER*>(mediaType->pbFormat);
        bitmapInfo = videoInfo->bmiHeader;
        frameTime = videoInfo->AvgTimePerFrame;
    } else if (mediaType->formattype == FORMAT_VideoInfo2 && mediaType->cbFormat >= sizeof(VIDEOINFOHEADER2)) {
        const auto* videoInfo = reinterpret_cast<const VIDEOINFOHEADER2*>(mediaType->pbFormat);
        bitmapInfo = videoInfo->bmiHeader;
        frameTime = videoInfo->AvgTimePerFrame;
    } else {
        return mode;
    }

    mode.width = bitmapInfo.biWidth;
    mode.height = std::abs(bitmapInfo.biHeight);
    mode.format = formatNameFromSubtype(mediaType->subtype);

    if (frameTime > 0) {
        constexpr int kDirectShowTimeUnitsPerSecond = 10000000;
        mode.fpsNumerator = kDirectShowTimeUnitsPerSecond;
        mode.fpsDenominator = static_cast<int>(std::min<REFERENCE_TIME>(
            frameTime,
            std::numeric_limits<int>::max()
        ));
        reduceFraction(mode.fpsNumerator, mode.fpsDenominator);
    }

    mode.caps = capsStringForMode(
        mode.format,
        mode.width,
        mode.height,
        mode.fpsNumerator,
        mode.fpsDenominator
    );
    mode.id = mode.caps;

    const QString fpsLabel = mode.fpsNumerator > 0 && mode.fpsDenominator > 0
        ? QStringLiteral(" @ %1 fps").arg(
            static_cast<double>(mode.fpsNumerator) / static_cast<double>(mode.fpsDenominator),
            0,
            'f',
            2)
        : QString();
    mode.label = QStringLiteral("%1 x %2 %3%4")
        .arg(mode.width)
        .arg(mode.height)
        .arg(mode.format)
        .arg(fpsLabel);

    return mode;
}

struct ControlDescriptor
{
    const char* id;
    const char* displayName;
    long property;
    bool cameraControl;
};

constexpr ControlDescriptor kControlDescriptors[] = {
    {"procamp:brightness", "Brightness", VideoProcAmp_Brightness, false},
    {"procamp:contrast", "Contrast", VideoProcAmp_Contrast, false},
    {"procamp:hue", "Hue", VideoProcAmp_Hue, false},
    {"procamp:saturation", "Saturation", VideoProcAmp_Saturation, false},
    {"procamp:sharpness", "Sharpness", VideoProcAmp_Sharpness, false},
    {"procamp:gamma", "Gamma", VideoProcAmp_Gamma, false},
    {"procamp:color-enable", "Color Enable", VideoProcAmp_ColorEnable, false},
    {"procamp:white-balance", "White Balance", VideoProcAmp_WhiteBalance, false},
    {"procamp:backlight-compensation", "Backlight Compensation", VideoProcAmp_BacklightCompensation, false},
    {"procamp:gain", "Gain", VideoProcAmp_Gain, false},
    {"camera:pan", "Pan", CameraControl_Pan, true},
    {"camera:tilt", "Tilt", CameraControl_Tilt, true},
    {"camera:roll", "Roll", CameraControl_Roll, true},
    {"camera:zoom", "Zoom", CameraControl_Zoom, true},
    {"camera:exposure", "Exposure", CameraControl_Exposure, true},
    {"camera:iris", "Iris", CameraControl_Iris, true},
    {"camera:focus", "Focus", CameraControl_Focus, true}
};

const ControlDescriptor* descriptorForControlId(const QString& controlId)
{
    const QByteArray id = controlId.toLatin1();
    for (const ControlDescriptor& descriptor : kControlDescriptors) {
        if (id == descriptor.id) {
            return &descriptor;
        }
    }

    return nullptr;
}

UsbCameraControl controlFromProcAmp(IAMVideoProcAmp* procAmp, const ControlDescriptor& descriptor)
{
    UsbCameraControl control;
    if (procAmp == nullptr) {
        return control;
    }

    long minimum = 0;
    long maximum = 0;
    long step = 0;
    long defaultValue = 0;
    long capabilities = 0;
    if (FAILED(procAmp->GetRange(
            descriptor.property,
            &minimum,
            &maximum,
            &step,
            &defaultValue,
            &capabilities))) {
        return control;
    }

    long value = defaultValue;
    long flags = 0;
    procAmp->Get(descriptor.property, &value, &flags);

    control.id = QString::fromLatin1(descriptor.id);
    control.displayName = QString::fromLatin1(descriptor.displayName);
    control.minimum = static_cast<int>(minimum);
    control.maximum = static_cast<int>(maximum);
    control.step = std::max(1, static_cast<int>(step));
    control.defaultValue = static_cast<int>(defaultValue);
    control.supportsAuto = (capabilities & VideoProcAmp_Flags_Auto) != 0;
    control.state.value = static_cast<int>(value);
    control.state.automatic = (flags & VideoProcAmp_Flags_Auto) != 0;
    return control;
}

UsbCameraControl controlFromCameraControl(IAMCameraControl* cameraControl, const ControlDescriptor& descriptor)
{
    UsbCameraControl control;
    if (cameraControl == nullptr) {
        return control;
    }

    long minimum = 0;
    long maximum = 0;
    long step = 0;
    long defaultValue = 0;
    long capabilities = 0;
    if (FAILED(cameraControl->GetRange(
            descriptor.property,
            &minimum,
            &maximum,
            &step,
            &defaultValue,
            &capabilities))) {
        return control;
    }

    long value = defaultValue;
    long flags = 0;
    cameraControl->Get(descriptor.property, &value, &flags);

    control.id = QString::fromLatin1(descriptor.id);
    control.displayName = QString::fromLatin1(descriptor.displayName);
    control.minimum = static_cast<int>(minimum);
    control.maximum = static_cast<int>(maximum);
    control.step = std::max(1, static_cast<int>(step));
    control.defaultValue = static_cast<int>(defaultValue);
    control.supportsAuto = (capabilities & CameraControl_Flags_Auto) != 0;
    control.state.value = static_cast<int>(value);
    control.state.automatic = (flags & CameraControl_Flags_Auto) != 0;
    return control;
}

bool isValidControl(const UsbCameraControl& control)
{
    return !control.id.isEmpty() && control.minimum <= control.maximum;
}
#endif
} // namespace

QVector<UsbCameraDevice> UsbCameraManager::devices()
{
    QVector<UsbCameraDevice> result;
#ifdef Q_OS_WIN
    ComScope com;
    if (!com.ok()) {
        return result;
    }

    IEnumMoniker* enumMoniker = videoDeviceEnumerator();
    if (enumMoniker == nullptr) {
        return result;
    }

    int index = 0;
    IMoniker* moniker = nullptr;
    ULONG fetched = 0;
    while (enumMoniker->Next(1, &moniker, &fetched) == S_OK) {
        UsbCameraDevice device;
        device.id = monikerDisplayName(moniker);
        device.displayName = monikerFriendlyName(moniker);
        if (device.displayName.isEmpty()) {
            device.displayName = device.id.isEmpty()
                ? QStringLiteral("USB camera %1").arg(index + 1)
                : device.id;
        }
        device.backend = QStringLiteral("DirectShow");
        device.index = index;
        result.append(device);

        releaseCom(moniker);
        ++index;
    }

    releaseCom(enumMoniker);
#endif
    return result;
}

QVector<UsbCameraMode> UsbCameraManager::modes(const QString& deviceId, int deviceIndex)
{
    QVector<UsbCameraMode> result;
#ifdef Q_OS_WIN
    ComScope com;
    if (com.ok()) {
        IMoniker* moniker = findVideoDeviceMoniker(deviceId);
        const QString friendlyName = monikerFriendlyName(moniker);
        releaseCom(moniker);
        result = modesFromGStreamerDeviceCaps(friendlyName, deviceIndex);
    }
    return result;
#else
    Q_UNUSED(deviceId)
    Q_UNUSED(deviceIndex)
    if (!ensureGStreamerReady()) {
        return result;
    }

    return result;
#endif
}

QVector<UsbCameraControl> UsbCameraManager::controls(const QString& deviceId)
{
    QVector<UsbCameraControl> result;
#ifdef Q_OS_WIN
    ComScope com;
    if (!com.ok()) {
        return result;
    }

    IBaseFilter* filter = bindVideoFilter(deviceId);
    if (filter == nullptr) {
        return result;
    }

    IAMVideoProcAmp* procAmp = nullptr;
    filter->QueryInterface(IID_IAMVideoProcAmp, reinterpret_cast<void**>(&procAmp));
    IAMCameraControl* cameraControl = nullptr;
    filter->QueryInterface(IID_IAMCameraControl, reinterpret_cast<void**>(&cameraControl));

    for (const ControlDescriptor& descriptor : kControlDescriptors) {
        UsbCameraControl control = descriptor.cameraControl
            ? controlFromCameraControl(cameraControl, descriptor)
            : controlFromProcAmp(procAmp, descriptor);
        if (isValidControl(control)) {
            result.append(control);
        }
    }

    releaseCom(cameraControl);
    releaseCom(procAmp);
    releaseCom(filter);
#endif
    return result;
}

QString UsbCameraManager::sourceFactoryName()
{
#ifdef Q_OS_WIN
    return QStringLiteral("mfvideosrc");
#elif defined(Q_OS_LINUX)
    return QStringLiteral("v4l2src");
#elif defined(Q_OS_MACOS)
    return QStringLiteral("avfvideosrc");
#else
    return QStringLiteral("autovideosrc");
#endif
}

bool UsbCameraManager::setControl(
    const QString& deviceId,
    const QString& controlId,
    const UsbCameraControlState& state,
    QString* errorMessage)
{
#ifdef Q_OS_WIN
    const ControlDescriptor* descriptor = descriptorForControlId(controlId);
    if (descriptor == nullptr) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("Unknown UVC control: %1").arg(controlId);
        }
        return false;
    }

    ComScope com;
    if (!com.ok()) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("Unable to initialize COM for UVC control access.");
        }
        return false;
    }

    IBaseFilter* filter = bindVideoFilter(deviceId);
    if (filter == nullptr) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("Unable to open the selected USB camera.");
        }
        return false;
    }

    long minimum = 0;
    long maximum = 0;
    long step = 0;
    long defaultValue = 0;
    long capabilities = 0;
    HRESULT result = E_FAIL;
    if (descriptor->cameraControl) {
        IAMCameraControl* cameraControl = nullptr;
        filter->QueryInterface(IID_IAMCameraControl, reinterpret_cast<void**>(&cameraControl));
        if (cameraControl != nullptr) {
            result = cameraControl->GetRange(
                descriptor->property,
                &minimum,
                &maximum,
                &step,
                &defaultValue,
                &capabilities
            );
            if (SUCCEEDED(result)) {
                const long flags = state.automatic && (capabilities & CameraControl_Flags_Auto) != 0
                    ? CameraControl_Flags_Auto
                    : CameraControl_Flags_Manual;
                const long value = std::clamp<long>(state.value, minimum, maximum);
                result = cameraControl->Set(descriptor->property, value, flags);
            }
        }
        releaseCom(cameraControl);
    } else {
        IAMVideoProcAmp* procAmp = nullptr;
        filter->QueryInterface(IID_IAMVideoProcAmp, reinterpret_cast<void**>(&procAmp));
        if (procAmp != nullptr) {
            result = procAmp->GetRange(
                descriptor->property,
                &minimum,
                &maximum,
                &step,
                &defaultValue,
                &capabilities
            );
            if (SUCCEEDED(result)) {
                const long flags = state.automatic && (capabilities & VideoProcAmp_Flags_Auto) != 0
                    ? VideoProcAmp_Flags_Auto
                    : VideoProcAmp_Flags_Manual;
                const long value = std::clamp<long>(state.value, minimum, maximum);
                result = procAmp->Set(descriptor->property, value, flags);
            }
        }
        releaseCom(procAmp);
    }

    releaseCom(filter);
    if (FAILED(result)) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("Unable to set UVC control %1.").arg(controlId);
        }
        return false;
    }

    return true;
#else
    Q_UNUSED(deviceId)
    Q_UNUSED(controlId)
    Q_UNUSED(state)
    if (errorMessage != nullptr) {
        *errorMessage = QStringLiteral("UVC control editing is not implemented on this platform.");
    }
    return false;
#endif
}
