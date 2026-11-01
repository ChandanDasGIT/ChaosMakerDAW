#pragma once
#include <QObject>
#include <QtQml/qqmlregistration.h>

class QQmlEngine;
class QJSEngine;

// One line: what this exposes to QML. It holds no audio logic: it calls the backend and reports back.
class ExampleController : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(int value READ value WRITE setValue NOTIFY valueChanged)

public:
    static ExampleController* instance();
    static ExampleController* create(QQmlEngine*, QJSEngine*);

    int value() const { return m_value; }
    void setValue(int value);

    Q_INVOKABLE void reset();

signals:
    void valueChanged();

private:
    explicit ExampleController(QObject* parent);

    int m_value = 0;
};
