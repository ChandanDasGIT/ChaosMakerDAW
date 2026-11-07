#include "typingkeyboardwrapper.h"
#include "wrapper/engine/audioenginewrap/audioenginewrapper.h"
#include "engine/audioengine.h"

#include <QGuiApplication>
#include <QInputMethodQueryEvent>
#include <QKeyEvent>
#include <QWindow>

#include <array>

namespace {

// The layout's character for a key event, or 0.
char layoutKey(const QKeyEvent* event)
{
    const int key = event->key();
    char c = 0;
    if (key >= Qt::Key_A && key <= Qt::Key_Z) c = char('a' + (key - Qt::Key_A));
    else if (key >= Qt::Key_0 && key <= Qt::Key_9) c = char('0' + (key - Qt::Key_0));
    else switch (key) {
        case Qt::Key_Tab: c = '\t'; break;
        case Qt::Key_Backspace: c = '\b'; break;
        case Qt::Key_Minus: c = '-'; break;
        case Qt::Key_Equal: c = '='; break;
        case Qt::Key_BracketLeft: c = '['; break;
        case Qt::Key_BracketRight: c = ']'; break;
        case Qt::Key_Backslash: c = '\\'; break;
        default: break;
    }
    return TypingKeyboard::handles(c) ? c : 0;
}

bool playable(const QKeyEvent* event)
{
    return !(event->modifiers() & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier));
}

// A text field (TextInput, TextEdit...) has the focus: typing must reach it.
bool typingIntoText()
{
    QObject* focus = QGuiApplication::focusObject();
    if (!focus) return false;
    QInputMethodQueryEvent query(Qt::ImEnabled);
    QCoreApplication::sendEvent(focus, &query);
    return query.value(Qt::ImEnabled).toBool();
}

} // namespace

TypingKeyboardWrapper::TypingKeyboardWrapper(QObject* parent)
    : QObject(parent)
{
    // Leaving the window (another app, a dialog elsewhere): nothing may keep sounding.
    if (auto* app = qobject_cast<QGuiApplication*>(QCoreApplication::instance()))
        connect(app, &QGuiApplication::focusWindowChanged, this, [this](QWindow* window) {
            if (!window) releaseAll();
        });
}

TypingKeyboardWrapper::~TypingKeyboardWrapper()
{
    setEnabled(false);
}

void TypingKeyboardWrapper::setEnabled(bool enabled)
{
    if (enabled == m_enabled) return;
    m_enabled = enabled;
    if (auto* app = QCoreApplication::instance()) {
        if (enabled) app->installEventFilter(this);
        else app->removeEventFilter(this);
    }
    if (!enabled) releaseAll();
    emit enabledChanged();
}

void TypingKeyboardWrapper::setEngine(QObject* engine)
{
    if (engine == m_engine) return;
    releaseAll();                                           // the old engine gets its note-offs
    m_engine = engine;
    emit engineChanged();
}

void TypingKeyboardWrapper::send(int note, int velocity, bool on)
{
    if (auto* wrapper = qobject_cast<AudioEngineWrapper*>(m_engine.data()))
        if (auto* engine = wrapper->engine())
            engine->sendKeyboardMidi(MidiEvent{0, std::uint8_t(on ? 0x90 : 0x80), std::uint8_t(note), std::uint8_t(velocity)});
    emit noteSent(note, velocity, on);
}

void TypingKeyboardWrapper::releaseAll()
{
    std::array<int, 128> notes{};
    const int count = m_keys.releaseAll(notes);
    for (int i = 0; i < count; ++i) send(notes[std::size_t(i)], 0, false);
}

bool TypingKeyboardWrapper::eventFilter(QObject* watched, QEvent* event)
{
    const auto type = event->type();
    if (type != QEvent::KeyPress && type != QEvent::KeyRelease && type != QEvent::ShortcutOverride)
        return false;
    const auto* key = static_cast<QKeyEvent*>(event);
    const char c = layoutKey(key);
    if (!c) return false;
    // A key release always ends its note, even if Ctrl went down meanwhile or a field took focus.
    if (type == QEvent::KeyRelease) {
        if (!watched->isWindowType() || key->isAutoRepeat()) return false;
        const auto action = m_keys.release(c);
        if (action.kind != TypingKeyboard::Action::Kind::NoteOff) return false;
        send(action.note, 0, false);
        return true;
    }
    if (!playable(key) || typingIntoText()) return false;
    if (type == QEvent::ShortcutOverride) {                 // claim the key before a shortcut fires
        event->accept();
        return true;
    }
    if (!watched->isWindowType()) return false;             // seen once, at the window
    if (key->isAutoRepeat()) return true;                   // held: no new note
    const auto action = m_keys.press(c);
    if (action.kind == TypingKeyboard::Action::Kind::NoteOn) send(action.note, action.velocity, true);
    else if (action.kind == TypingKeyboard::Action::Kind::Changed) emit layoutChanged();
    return true;
}
