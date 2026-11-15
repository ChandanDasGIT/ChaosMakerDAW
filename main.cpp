#include <QCoreApplication>
#include <QGuiApplication>
#include <QIcon>
#include <QQmlApplicationEngine>

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    QCoreApplication::setOrganizationName("ChaosMaker");
    QCoreApplication::setApplicationName("ChaosMaker");
    // The desktop matches the window to its launcher entry and icon by this id.
    QGuiApplication::setDesktopFileName("io.github.chandandasgit.ChaosMaker");
    QGuiApplication::setWindowIcon(QIcon(QStringLiteral(":/icons/icon.svg")));

    QQmlApplicationEngine engine;
    QObject::connect(
        &engine,
        &QQmlApplicationEngine::objectCreationFailed,
        &app,
        []() { QCoreApplication::exit(-1); },
        Qt::QueuedConnection);
    engine.loadFromModule("ChaosMaker", "Main");

    return QGuiApplication::exec();
}
