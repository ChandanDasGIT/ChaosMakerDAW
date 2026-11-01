#include "examplecontroller.h"

#include <QCoreApplication>
#include <QJSEngine>
#include <QQmlEngine>

ExampleController::ExampleController(QObject* parent) : QObject(parent) {}

ExampleController* ExampleController::instance()
{
    static auto* controller = new ExampleController(QCoreApplication::instance());
    return controller;
}

ExampleController* ExampleController::create(QQmlEngine*, QJSEngine*)
{
    auto* controller = instance();
    QQmlEngine::setObjectOwnership(controller, QQmlEngine::CppOwnership);   // QML must not delete it
    return controller;
}

void ExampleController::setValue(int value)
{
    if (value == m_value) return;
    m_value = value;
    emit valueChanged();
}

void ExampleController::reset()
{
    setValue(0);
}
