#pragma once

#include "plugins/chaoseq/chaoseqcurve.h"
#include "dsp/spectrumanalyzer.h"

#include <QElapsedTimer>
#include <QObject>
#include <QPointer>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

#include <vector>

class TrackListWrapper;

// Chaos EQ (plan 6.2) in QML: one track's bands, the display curves and the live spectrum.
// Band edits go to the track through TrackListWrapper::setChaosEq; the engine follows its
// chaosEqChanged signal. Display maths (frequency <-> x) belongs to the QML view.
class ChaosEqWrapper : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QObject* trackModel READ trackModel WRITE setTrackModel NOTIFY trackModelChanged)
    Q_PROPERTY(int trackId READ trackId WRITE setTrackId NOTIFY trackIdChanged)
    // The EngineController, for the analyzer feed. Without it the spectrum stays empty.
    Q_PROPERTY(QObject* engine READ engine WRITE setEngine NOTIFY engineChanged)
    // Poll the analyzer about 30 times a second while true (the panel is visible).
    Q_PROPERTY(bool analyzerEnabled READ analyzerEnabled WRITE setAnalyzerEnabled NOTIFY analyzerEnabledChanged)

    // Bands in use: [{slot, shape, slope, frequency, gain, q, placement, bypass, solo,
    //                 usesGain, usesSlope}], in slot order.
    Q_PROPERTY(QVariantList bands READ bands NOTIFY settingsChanged)
    Q_PROPERTY(int bandCount READ bandCount NOTIFY settingsChanged)
    Q_PROPERTY(bool canAddBand READ canAddBand NOTIFY settingsChanged)
    Q_PROPERTY(double outputGain READ outputGain WRITE setOutputGain NOTIFY settingsChanged)
    Q_PROPERTY(bool autoGain READ autoGain WRITE setAutoGain NOTIFY settingsChanged)
    Q_PROPERTY(double autoGainAmount READ autoGainAmount NOTIFY settingsChanged)
    // Display: frequencies (Hz, log spaced 20 Hz - 20 kHz), the summed curve (dB) and each
    // band's own curve ({slot: [dB...]}), all at those frequencies.
    Q_PROPERTY(QVariantList curveFrequencies READ curveFrequencies CONSTANT)
    Q_PROPERTY(QVariantList totalCurve READ totalCurve NOTIFY settingsChanged)
    Q_PROPERTY(QVariantMap bandCurves READ bandCurves NOTIFY settingsChanged)
    // Live spectrum (dBFS) at curveFrequencies; empty when there is nothing to show.
    Q_PROPERTY(QVariantList spectrum READ spectrum NOTIFY spectrumChanged)

    // Choices and limits for the controls.
    Q_PROPERTY(QVariantList shapes READ shapes CONSTANT)          // [{id, title}]
    Q_PROPERTY(QVariantList placements READ placements CONSTANT)  // [{id, title}]
    Q_PROPERTY(QVariantList slopes READ slopes CONSTANT)          // [6, 12, 24, 48]
    Q_PROPERTY(int maxBands READ maxBands CONSTANT)
    Q_PROPERTY(double minFrequency READ minFrequency CONSTANT)
    Q_PROPERTY(double maxFrequency READ maxFrequency CONSTANT)
    Q_PROPERTY(double minGain READ minGain CONSTANT)
    Q_PROPERTY(double maxGain READ maxGain CONSTANT)
    Q_PROPERTY(double minQ READ minQ CONSTANT)
    Q_PROPERTY(double maxQ READ maxQ CONSTANT)
    Q_PROPERTY(double minOutputGain READ minOutputGain CONSTANT)
    Q_PROPERTY(double maxOutputGain READ maxOutputGain CONSTANT)

public:
    static constexpr int CurvePoints = 256;

    explicit ChaosEqWrapper(QObject* parent = nullptr);
    ~ChaosEqWrapper() override;

    QObject* trackModel() const;
    void setTrackModel(QObject* model);
    int trackId() const { return m_trackId; }
    void setTrackId(int id);
    QObject* engine() const { return m_engine; }
    void setEngine(QObject* engine);
    bool analyzerEnabled() const { return m_analyzerEnabled; }
    void setAnalyzerEnabled(bool enabled);

    QVariantList bands() const;
    int bandCount() const { return chaoseq::bandsInUse(m_settings); }
    bool canAddBand() const { return validTrack() && chaoseq::firstFreeBand(m_settings) >= 0; }
    double outputGain() const { return m_settings.outputGainDb; }
    void setOutputGain(double db);
    bool autoGain() const { return m_settings.autoGain; }
    void setAutoGain(bool enabled);
    double autoGainAmount() const { return chaoseq::autoGainDb(m_settings); }
    QVariantList curveFrequencies() const;
    QVariantList totalCurve() const;
    QVariantMap bandCurves() const;
    QVariantList spectrum() const { return m_spectrum; }

    QVariantList shapes() const;
    QVariantList placements() const;
    QVariantList slopes() const;
    int maxBands() const { return chaoseq::MaxBands; }
    double minFrequency() const { return eqband::MinFrequencyHz; }
    double maxFrequency() const { return std::min(eqband::MaxFrequencyHz, sampleRate() * 0.49); }
    double minGain() const { return eqband::MinGainDb; }
    double maxGain() const { return eqband::MaxGainDb; }
    double minQ() const { return eqband::MinQ; }
    double maxQ() const { return eqband::MaxQ; }
    double minOutputGain() const { return chaoseq::MinOutputDb; }
    double maxOutputGain() const { return chaoseq::MaxOutputDb; }

    // A new bell band at this point; returns its slot, or -1 (no track, or all 24 in use).
    Q_INVOKABLE int addBand(double frequencyHz, double gainDb);
    Q_INVOKABLE bool removeBand(int slot);
    // Changes any of: shape (id), slope (dB/oct), frequency, gain, q, placement (id), bypass,
    // solo. Values are clamped into range; unknown shape/placement ids or slopes refuse the
    // whole change. Solo is exclusive: soloing one band un-solos the others.
    Q_INVOKABLE bool setBand(int slot, const QVariantMap& changes);
    Q_INVOKABLE void removeAllBands();

    Q_INVOKABLE QString formatFrequency(double hz) const;
    Q_INVOKABLE QString formatGain(double db) const;
    Q_INVOKABLE QString formatQ(double q) const;
    Q_INVOKABLE QString shapeTitle(const QString& id) const;

    // Test hook: one analyzer poll with a given elapsed time (the timer calls this with the real one).
    void pollAnalyzer(double elapsedSeconds);

signals:
    void trackModelChanged();
    void trackIdChanged();
    void engineChanged();
    void analyzerEnabledChanged();
    void settingsChanged();
    void spectrumChanged();

private:
    bool validTrack() const;
    double sampleRate() const;
    void reload();
    bool commit(const chaoseq::Settings& settings);
    void updateCurves();
    void updateAnalyzerTimer();
    void stopListening();

    QPointer<TrackListWrapper> m_trackModel;
    QPointer<QObject> m_engine;
    int m_trackId = -1;
    bool m_analyzerEnabled = false;
    chaoseq::Settings m_settings;
    chaoseq::Curve m_curve;
    QVariantList m_spectrum;
    SpectrumAnalyzer m_analyzer{4096};
    std::vector<float> m_readBuffer;
    std::vector<double> m_frequencies;
    QTimer m_pollTimer;
    QElapsedTimer m_sincePoll;
    bool m_hadAudio = false;
};
