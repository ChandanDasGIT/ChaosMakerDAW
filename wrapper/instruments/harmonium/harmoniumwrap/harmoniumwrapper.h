#pragma once

#include "instruments/harmonium/harmoniumsettings.h"

#include <QObject>
#include <QPointer>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

class TrackListWrapper;

// One Harmonium track's controls in QML. Edits go to the track through
// TrackListWrapper::setHarmonium; the engine follows its harmoniumChanged signal, and so does
// this wrapper (a second panel on the same track stays in step).
class HarmoniumWrapper : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QObject* trackModel READ trackModel WRITE setTrackModel NOTIFY trackModelChanged)
    Q_PROPERTY(int trackId READ trackId WRITE setTrackId NOTIFY trackIdChanged)
    Q_PROPERTY(bool valid READ valid NOTIFY settingsChanged)            // a Harmonium track

    Q_PROPERTY(QString style READ style WRITE setStyle NOTIFY settingsChanged)   // "indian", "western"
    Q_PROPERTY(bool bass READ bass WRITE setBass NOTIFY settingsChanged)
    Q_PROPERTY(bool male READ male WRITE setMale NOTIFY settingsChanged)
    Q_PROPERTY(bool female READ female WRITE setFemale NOTIFY settingsChanged)
    Q_PROPERTY(bool bourdon16 READ bourdon16 WRITE setBourdon16 NOTIFY settingsChanged)
    Q_PROPERTY(bool diapason8 READ diapason8 WRITE setDiapason8 NOTIFY settingsChanged)
    Q_PROPERTY(bool dulciana8 READ dulciana8 WRITE setDulciana8 NOTIFY settingsChanged)
    Q_PROPERTY(bool principal4 READ principal4 WRITE setPrincipal4 NOTIFY settingsChanged)
    Q_PROPERTY(bool voxHumana READ voxHumana WRITE setVoxHumana NOTIFY settingsChanged)
    Q_PROPERTY(double bellows READ bellows WRITE setBellows NOTIFY settingsChanged)         // 0..1
    Q_PROPERTY(double pumping READ pumping WRITE setPumping NOTIFY settingsChanged)         // 0..1
    Q_PROPERTY(double beating READ beating WRITE setBeating NOTIFY settingsChanged)         // cents
    Q_PROPERTY(double airNoise READ airNoise WRITE setAirNoise NOTIFY settingsChanged)      // 0..1
    Q_PROPERTY(double keyClick READ keyClick WRITE setKeyClick NOTIFY settingsChanged)      // 0..1
    Q_PROPERTY(double caseResonance READ caseResonance WRITE setCaseResonance NOTIFY settingsChanged)
    Q_PROPERTY(double sustain READ sustain WRITE setSustain NOTIFY settingsChanged)         // seconds
    Q_PROPERTY(int transpose READ transpose WRITE setTranspose NOTIFY settingsChanged)      // semitones
    Q_PROPERTY(double volume READ volume WRITE setVolume NOTIFY settingsChanged)            // dB
    Q_PROPERTY(QVariantList drones READ drones NOTIFY settingsChanged)  // 4 MIDI keys, -1 = off

    Q_PROPERTY(QVariantList styles READ styles CONSTANT)                // [{id, title}]
    Q_PROPERTY(int droneCount READ droneCount CONSTANT)
    Q_PROPERTY(int maxTranspose READ maxTranspose CONSTANT)
    Q_PROPERTY(double maxBeating READ maxBeating CONSTANT)
    Q_PROPERTY(double maxSustain READ maxSustain CONSTANT)
    Q_PROPERTY(double minVolume READ minVolume CONSTANT)
    Q_PROPERTY(double maxVolume READ maxVolume CONSTANT)

public:
    explicit HarmoniumWrapper(QObject* parent = nullptr);

    QObject* trackModel() const;
    void setTrackModel(QObject* model);
    int trackId() const { return m_trackId; }
    void setTrackId(int id);
    bool valid() const;

    QString style() const;
    void setStyle(const QString& style);
    bool bass() const { return m_s.indian.bass; }
    void setBass(bool on) { edit([on](auto& s) { s.indian.bass = on; }); }
    bool male() const { return m_s.indian.male; }
    void setMale(bool on) { edit([on](auto& s) { s.indian.male = on; }); }
    bool female() const { return m_s.indian.female; }
    void setFemale(bool on) { edit([on](auto& s) { s.indian.female = on; }); }
    bool bourdon16() const { return m_s.western.bourdon16; }
    void setBourdon16(bool on) { edit([on](auto& s) { s.western.bourdon16 = on; }); }
    bool diapason8() const { return m_s.western.diapason8; }
    void setDiapason8(bool on) { edit([on](auto& s) { s.western.diapason8 = on; }); }
    bool dulciana8() const { return m_s.western.dulciana8; }
    void setDulciana8(bool on) { edit([on](auto& s) { s.western.dulciana8 = on; }); }
    bool principal4() const { return m_s.western.principal4; }
    void setPrincipal4(bool on) { edit([on](auto& s) { s.western.principal4 = on; }); }
    bool voxHumana() const { return m_s.western.voxHumana; }
    void setVoxHumana(bool on) { edit([on](auto& s) { s.western.voxHumana = on; }); }
    double bellows() const { return m_s.bellows; }
    void setBellows(double v) { edit([v](auto& s) { s.bellows = v; }); }
    double pumping() const { return m_s.pumping; }
    void setPumping(double v) { edit([v](auto& s) { s.pumping = v; }); }
    double beating() const { return m_s.beatingCents; }
    void setBeating(double v) { edit([v](auto& s) { s.beatingCents = v; }); }
    double airNoise() const { return m_s.airNoise; }
    void setAirNoise(double v) { edit([v](auto& s) { s.airNoise = v; }); }
    double keyClick() const { return m_s.keyClick; }
    void setKeyClick(double v) { edit([v](auto& s) { s.keyClick = v; }); }
    double caseResonance() const { return m_s.caseResonance; }
    void setCaseResonance(double v) { edit([v](auto& s) { s.caseResonance = v; }); }
    double sustain() const { return m_s.sustainSeconds; }
    void setSustain(double v) { edit([v](auto& s) { s.sustainSeconds = v; }); }
    int transpose() const { return m_s.transpose; }
    void setTranspose(int v) { edit([v](auto& s) { s.transpose = v; }); }
    double volume() const { return m_s.volumeDb; }
    void setVolume(double v) { edit([v](auto& s) { s.volumeDb = v; }); }
    QVariantList drones() const;

    QVariantList styles() const;
    int droneCount() const { return harmonium::DroneCount; }
    int maxTranspose() const { return harmonium::MaxTranspose; }
    double maxBeating() const { return harmonium::MaxBeatingCents; }
    double maxSustain() const { return harmonium::MaxSustainSeconds; }
    double minVolume() const { return harmonium::MinVolumeDb; }
    double maxVolume() const { return harmonium::MaxVolumeDb; }

    // A drone slot's key (0..127), or -1 to turn that drone off.
    Q_INVOKABLE bool setDrone(int index, int key);
    Q_INVOKABLE bool resetToDefaults();

signals:
    void trackModelChanged();
    void trackIdChanged();
    void settingsChanged();

private:
    template <typename Change>
    bool edit(Change change)
    {
        if (!valid()) return false;
        auto next = m_s;
        change(next);
        return commit(next);
    }
    bool commit(const harmonium::Settings& settings);
    void reload();

    QPointer<TrackListWrapper> m_trackModel;
    int m_trackId = -1;
    harmonium::Settings m_s;
};
