#include "MainWindow.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QLocale>
#include <QTimer>
#include <QTranslator>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

int main(int argc, char *argv[])
{
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
