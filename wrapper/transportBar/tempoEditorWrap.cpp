#include "tempoEditorWrap.h"

void TempoEditorWrap::setValue(double value)
{
    m_editor.setValue(value);
    emit stateChanged();
}

void TempoEditorWrap::setText(const QString& text)
{
    m_editor.setText(text.toStdU16String());
    emit stateChanged();
}

void TempoEditorWrap::setEditing(bool editing)
{
    m_editor.setEditing(editing);
    emit stateChanged();
}

void TempoEditorWrap::commit()
{
    if (const auto value = m_editor.commit()) emit valueEdited(*value);
    m_editor.restoreText();
    emit stateChanged();
}

void TempoEditorWrap::adjust(int steps)
{
    if (const auto value = m_editor.adjust(steps)) emit valueEdited(*value);
    m_editor.restoreText();
    emit stateChanged();
}

void TempoEditorWrap::tap()
{
    tapAt(double(m_clock.nsecsElapsed()) * 1e-9);
}

void TempoEditorWrap::tapAt(double seconds)
{
    if (const auto bpm = m_tap.tap(seconds)) emit valueEdited(*bpm);
    emit stateChanged();
}
