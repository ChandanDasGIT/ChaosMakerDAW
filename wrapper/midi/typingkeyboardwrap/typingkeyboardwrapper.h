#pragma once

#include "midi/typingkeyboard.h"

#include <QObject>
#include <QPointer>
#include <QtQml/qqmlregistration.h>

// The KEYS switch: while on, the computer keyboard plays notes into the engine's MIDI input,
// like a MIDI keyboard (TypingKeyboard's sargam layout: Tab .. Backspace, lower Pa to upper Ma).
// It listens app-wide, takes only its own keys, and leaves them alone when Ctrl, Alt or Meta is
// held or a text field has the focus (a track name, the BPM). Its keys win over single-key
// shortcuts (Q, Tab focus moves) while it is on. Turning it off or leaving the window ends
// every held note.
class TypingKeyboardWrapper : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY enabledChanged)
    // The EngineController (or any AudioEngineWrapper); without it the notes go nowhere.
    Q_PROPERTY(QObject* engine READ engine WRITE setEngine NOTIFY engineChanged)
    Q_PROPERTY(int octave READ octave NOTIFY layoutChanged)
    Q_PROPERTY(int velocity READ velocity NOTIFY layoutChanged)
    Q_PROPERTY(QString saNote READ saNote NOTIFY layoutChanged)   // what Sa ('E') plays, e.g. "C4"

public:
    explicit TypingKeyboardWrapper(QObject* parent = nullptr);
    ~TypingKeyboardWrapper() override;

    bool enabled() const { return m_enabled; }
    void setEnabled(bool enabled);
    QObject* engine() const { return m_engine; }
    void setEngine(QObject* engine);
    int octave() const { return m_keys.octave(); }
    int velocity() const { return m_keys.velocity(); }
    QString saNote() const { return QStringLiteral("C%1").arg(m_keys.octave()); }

signals:
    void enabledChanged();
    void engineChanged();
    void layoutChanged();
    void noteSent(int note, int velocity, bool on);          // every note that went to the engine

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void send(int note, int velocity, bool on);
    void releaseAll();

    TypingKeyboard m_keys;
    QPointer<QObject> m_engine;
    bool m_enabled = false;
};
