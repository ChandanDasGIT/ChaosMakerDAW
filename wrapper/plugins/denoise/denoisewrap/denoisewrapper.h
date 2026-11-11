#pragma once

#include <QObject>
#include <QPointer>
#include <QTimer>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

#include <array>
#include <memory>
#include <vector>

class TrackListWrapper;
class Denoise;

// The Noise Reduction page's model (restMap G13): the learned noise as a curve, the live sound
// before and after the effect (measured the same way, so they line up), and learning the noise
// from the time selection. The controls themselves go through MixEffectWrapper ("denoise").
class DenoiseWrapper : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QObject* trackModel READ trackModel WRITE setTrackModel NOTIFY trackModelChanged)
    // A track id, or a clip's effects id (Project::clipEffectsId) for a clip effect.
    Q_PROPERTY(int effectsId READ effectsId WRITE setEffectsId NOTIFY effectsIdChanged)
    // The EngineController, for the live spectra. Without it they stay empty.
    Q_PROPERTY(QObject* engine READ engine WRITE setEngine NOTIFY engineChanged)
    // Poll the spectra about 30 times a second while true (the page is visible).
    Q_PROPERTY(bool analyzerEnabled READ analyzerEnabled WRITE setAnalyzerEnabled NOTIFY analyzerEnabledChanged)
    // Display frequencies (Hz, 20 Hz .. 20 kHz, log spaced); every curve has one dB per entry.
    Q_PROPERTY(QVariantList frequencies READ frequencies CONSTANT)
    Q_PROPERTY(QVariantList profile READ profile NOTIFY profileChanged)      // empty: nothing learned
    Q_PROPERTY(bool hasProfile READ hasProfile NOTIFY profileChanged)
    Q_PROPERTY(QVariantList input READ input NOTIFY spectrumChanged)         // empty: no sound yet
    Q_PROPERTY(QVariantList output READ output NOTIFY spectrumChanged)
    // What the last Learn said (a reason it could not), "" after a learn that worked.
    Q_PROPERTY(QString message READ message NOTIFY messageChanged)

public:
    explicit DenoiseWrapper(QObject* parent = nullptr);
    ~DenoiseWrapper() override;

    QObject* trackModel() const;
    void setTrackModel(QObject* model);
    int effectsId() const { return m_effectsId; }
    void setEffectsId(int id);
    QObject* engine() const { return m_engine; }
    void setEngine(QObject* engine);
    bool analyzerEnabled() const { return m_analyzerEnabled; }
    void setAnalyzerEnabled(bool on);

    QVariantList frequencies() const;
    QVariantList profile() const;
    bool hasProfile() const;
    QVariantList input() const { return m_input; }
    QVariantList output() const { return m_output; }
    QString message() const { return m_message; }

    // Learns the noise from [startBeat, endBeat) of the owner's recorded audio.
    Q_INVOKABLE bool learn(double startBeat, double endBeat);
    Q_INVOKABLE bool forget();
    // Test hook: one poll of the spectra with a given elapsed time (the timer uses the real one).
    Q_INVOKABLE void pollAnalyzer(double elapsedSeconds);

signals:
    void trackModelChanged();
    void effectsIdChanged();
    void engineChanged();
    void analyzerEnabledChanged();
    void profileChanged();
    void spectrumChanged();
    void messageChanged();

private:
    std::vector<std::shared_ptr<Denoise>> processors() const;
    void listen(bool on);
    void clearSpectra();
    double sampleRate() const;
    struct Feed {
        std::vector<float> history;   // the last Denoise::Size samples
        std::vector<double> shown;    // dB per display frequency, falling slowly
        bool heard = false;
    };
    QVariantList update(Feed& feed, double elapsedSeconds);

    QPointer<TrackListWrapper> m_model;
    QPointer<QObject> m_engine;
    int m_effectsId = -1;
    bool m_analyzerEnabled = false;
    std::vector<double> m_frequencies;
    std::vector<std::weak_ptr<Denoise>> m_listening;
    Feed m_in, m_out;
    QVariantList m_input, m_output;
    QString m_message;
    QTimer m_timer;
    QMetaObject::Connection m_changeConnection;
};
