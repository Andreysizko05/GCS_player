#include "MainWindow.h"
#include "GstVideoReceiver.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QDateTime>
#include <QLocale>
#include <QTextStream>
#include <QTimer>
#include <QTranslator>

#include <cstdio>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace {

FILE* gLogFile = nullptr;

void fileMessageHandler(QtMsgType type, const QMessageLogContext&, const QString& message)
{
    if (gLogFile == nullptr) {
        return;
    }
    const char* level = "DEBUG";
    switch (type) {
    case QtInfoMsg: level = "INFO"; break;
    case QtWarningMsg: level = "WARNING"; break;
    case QtCriticalMsg: level = "CRITICAL"; break;
    case QtFatalMsg: level = "FATAL"; break;
    default: break;
    }
    std::fprintf(gLogFile, "%s [%s] %s\n",
        qUtf8Printable(QDateTime::currentDateTime().toString(Qt::ISODateWithMs)),
        level,
        qUtf8Printable(message));
    std::fflush(gLogFile);
}

} // namespace

#ifdef Q_OS_WIN
extern "C" __declspec(dllexport) int gcsPlayerMain(int argc, char *argv[])
#else
int main(int argc, char *argv[])
#endif
{
    gLogFile = std::fopen("gcs_player_debug.log", "a");
    if (gLogFile != nullptr) {
        qInstallMessageHandler(fileMessageHandler);
    }
#ifdef Q_OS_WIN
    const UINT previousErrorMode = SetErrorMode(0);
    SetErrorMode(previousErrorMode
        | SEM_FAILCRITICALERRORS
        | SEM_NOGPFAULTERRORBOX
        | SEM_NOOPENFILEERRORBOX);
#endif

    QApplication a(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("GCS_player"));

    QCommandLineParser parser;
    parser.setSingleDashWordOptionMode(QCommandLineParser::ParseAsLongOptions);
    parser.addHelpOption();

    const QCommandLineOption smokeTestOption(
        QStringLiteral("smoke-test"),
        QStringLiteral("Exit automatically after a short delay for CI/local smoke tests.")
    );
    const QCommandLineOption smokeTestMsOption(
        QStringLiteral("smoke-test-ms"),
        QStringLiteral("Milliseconds before an automatic smoke-test exit."),
        QStringLiteral("milliseconds"),
        QStringLiteral("250")
    );

    parser.addOption(smokeTestOption);
    parser.addOption(smokeTestMsOption);
    parser.process(a);
    const bool smokeTest = parser.isSet(smokeTestOption);
    if (smokeTest) {
        QString errorMessage;
        if (!GstVideoReceiver::validateRuntime(errorMessage)) {
            std::fprintf(stderr, "GStreamer smoke test failed: %s\n", qUtf8Printable(errorMessage));
            return 1;
        }
    }

    QTranslator translator;
    const QStringList uiLanguages = QLocale::system().uiLanguages();
    for (const QString &locale : uiLanguages) {
        const QString baseName = "GCS_player_" + QLocale(locale).name();
        if (translator.load(":/i18n/" + baseName)) {
            a.installTranslator(&translator);
            break;
        }
    }
    MainWindow w(nullptr, !smokeTest);
    w.show();

    if (smokeTest) {
        bool ok = false;
        const int delayMs = parser.value(smokeTestMsOption).toInt(&ok);
        QTimer::singleShot(ok ? delayMs : 250, &a, &QCoreApplication::quit);
    }

    return a.exec();
}
