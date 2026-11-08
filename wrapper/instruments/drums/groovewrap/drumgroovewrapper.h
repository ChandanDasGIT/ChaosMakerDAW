#pragma once

#include "persistence/groovestore.h"

#include <QObject>
#include <QPointer>
#include <QString>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

#include <memory>

class TrackListWrapper;
class EngineController;

// The drum grooves on a Drums / Electronic Drums FX page. Lists the factory grooves and the
// user's own (GrooveStore, in the app data folder's "grooves"), puts one on the track as a new
// clip at the start of the bar the playhead is in (one undo step; the grooves are in beats, so
// they follow the project's tempo, and on the shared drum keys, so either kit plays them), and
// turns the selected clip on the track into a new groove under the project's time signature.
class DrumGrooveWrapper : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QObject* trackModel READ trackModel WRITE setTrackModel NOTIFY trackModelChanged)
    Q_PROPERTY(int trackId READ trackId WRITE setTrackId NOTIFY trackIdChanged)
    Q_PROPERTY(bool valid READ valid NOTIFY trackChanged)              // a drum track
    // The EngineController: the project's time signature and the playhead. Without one the
    // meter counts as 4/4 and the playhead as the start.
    Q_PROPERTY(QObject* engine READ engine WRITE setEngine NOTIFY engineChanged)
    // Where the user's grooves live; empty = the app data folder's "grooves" (tests set their own).
    Q_PROPERTY(QString folder READ folder WRITE setFolder NOTIFY groovesChanged)
    // [{name, meter: "7/8", numerator, denominator, bars, factory, matchesProject}], the
    // project's time signature first, then the others; factory before user within each.
    Q_PROPERTY(QVariantList grooves READ grooves NOTIFY groovesChanged)
    Q_PROPERTY(QString projectMeter READ projectMeter NOTIFY groovesChanged)
    // A pattern clip on this track is selected and holds notes: it can become a groove.
    Q_PROPERTY(bool canSaveClip READ canSaveClip NOTIFY trackChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged)

public:
    explicit DrumGrooveWrapper(QObject* parent = nullptr);
    ~DrumGrooveWrapper() override;

    QObject* trackModel() const;
    void setTrackModel(QObject* model);
    int trackId() const { return m_trackId; }
    void setTrackId(int id);
    bool valid() const;
    QObject* engine() const;
    void setEngine(QObject* engine);
    QString folder() const;
    void setFolder(const QString& folder);
    QVariantList grooves() const;
    QString projectMeter() const;
    bool canSaveClip() const;
    QString lastError() const { return m_lastError; }

    // A new clip of `bars` bars (the groove repeats) at the start of the bar the playhead is in.
    // Returns the clip id, or -1 (lastError says why).
    Q_INVOKABLE int addToTrack(const QString& name, int bars);
    // The selected clip on this track becomes a user groove named `name`; replace = overwrite
    // a user groove of that name.
    Q_INVOKABLE bool saveSelectedClip(const QString& name, bool replace = false);
    Q_INVOKABLE bool renameGroove(const QString& name, const QString& newName);
    Q_INVOKABLE bool removeGroove(const QString& name);
    Q_INVOKABLE bool exists(const QString& name) const;     // any groove, factory or user
    Q_INVOKABLE void refresh();                              // re-read the folder

signals:
    void trackModelChanged();
    void trackIdChanged();
    void engineChanged();
    void trackChanged();
    void groovesChanged();
    void lastErrorChanged();

private:
    struct Meter { int numerator = 4, denominator = 4; double barBeats() const { return numerator * 4.0 / denominator; } };
    Meter meter() const;
    GrooveStore& store() const;
    int selectedClipOnTrack() const;
    bool fail(const QString& message);
    void succeed();

    QPointer<TrackListWrapper> m_trackModel;
    QPointer<EngineController> m_engine;
    int m_trackId = -1;
    QString m_folder;
    mutable std::unique_ptr<GrooveStore> m_store;
    QString m_lastError;
};
