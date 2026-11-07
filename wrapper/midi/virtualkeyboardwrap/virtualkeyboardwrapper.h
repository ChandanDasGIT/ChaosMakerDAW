#pragma once

#include <QObject>
#include <QPointer>
#include <QtQml/qqmlregistration.h>

#include <array>

// The on-screen piano (a virtual MIDI keyboard). Its notes go into the engine's keyboard input,
// the same way as the KEYS typing keyboard and a MIDI keyboard's, so they play the instrument
// tracks with MON or REC on (or the only instrument track). It remembers which notes it holds:
// releaseAll() (closing the window) and a new engine end them, so no note hangs.
class VirtualKeyboardWrapper : public QObject {
    Q_OBJECT
    QML_ELEMENT
    // The EngineController (or any AudioEngineWrapper); without it the notes go nowhere.
    Q_PROPERTY(QObject* engine READ engine WRITE setEngine NOTIFY engineChanged)

public:
    explicit VirtualKeyboardWrapper(QObject* parent = nullptr);
    ~VirtualKeyboardWrapper() override;

    QObject* engine() const { return m_engine; }
    void setEngine(QObject* engine);

    Q_INVOKABLE void noteOn(int note, int velocity);
    Q_INVOKABLE void noteOff(int note);
    Q_INVOKABLE void releaseAll();
    Q_INVOKABLE bool isHeld(int note) const { return note >= 0 && note < 128 && m_held[std::size_t(note)]; }
    Q_INVOKABLE QString noteName(int note) const;          // "C4" for 60

signals:
    void engineChanged();
    void noteSent(int note, int velocity, bool on);         // every note that went to the engine

private:
    void send(int note, int velocity, bool on);

    QPointer<QObject> m_engine;
    std::array<bool, 128> m_held{};
};
