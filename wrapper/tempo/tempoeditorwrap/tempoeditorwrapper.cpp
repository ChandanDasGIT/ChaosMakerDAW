#include "tempoeditorwrapper.h"

void TempoEditorWrapper::setValue(double value)
{
    m_editor.setValue(value);
    emit stateChanged();
}

void TempoEditorWrapper::setText(const QString& text)
{
    m_editor.setText(text.toStdU16String());
    emit stateChanged();
}

void TempoEditorWrapper::setEditing(bool editing)
{
    m_editor.setEditing(editing);
    emit stateChanged();
}

void TempoEditorWrapper::commit()
{
    if (const auto value = m_editor.commit()) emit valueEdited(*value);
    m_editor.restoreText();
    emit stateChanged();
}

void TempoEditorWrapper::adjust(int steps)
{
    if (const auto value = m_editor.adjust(steps)) emit valueEdited(*value);
    m_editor.restoreText();
    emit stateChanged();
}

void TempoEditorWrapper::tap()
{
    tapAt(double(m_clock.nsecsElapsed()) * 1e-9);
}

void TempoEditorWrapper::tapAt(double seconds)
{
    if (const auto bpm = m_tap.tap(seconds)) emit valueEdited(*bpm);
    emit stateChanged();
}
