#include <QAction>
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeySequence>
#include <QProcess>
#include <QRegularExpression>
#include <QSocketNotifier>
#include <QDebug>
#include <QUuid>
#include <QtEndian>

#include <KGlobalAccel>
#include <KGlobalShortcutInfo>

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <unistd.h>

namespace {
constexpr qint64 kDebounceMs = 150;
constexpr quint32 kMaxMessageBytes = 1024 * 1024;

bool isExpectedOwner(const KGlobalShortcutInfo &shortcut, const QKeySequence &sequence) {
    const QString component = shortcut.componentUniqueName();
    const QString action = shortcut.uniqueName();
    if (component == QStringLiteral("quick-swap-tools")) {
        return (sequence == QKeySequence(Qt::META | Qt::Key_A)
                && action == QStringLiteral("next-auction"))
            || (sequence == QKeySequence(Qt::META | Qt::Key_G)
                && action == QStringLiteral("next-giveaway"));
    }
    if (sequence == QKeySequence(Qt::META | Qt::Key_A)) {
        return component == QStringLiteral("plasmashell")
            && action == QStringLiteral("next activity");
    }
    if (sequence == QKeySequence(Qt::META | Qt::Key_G)) {
        return component == QStringLiteral("kwin")
            && action == QStringLiteral("Grid View");
    }
    return false;
}

bool shortcutsCanBeClaimed() {
    const std::array<QKeySequence, 2> sequences = {
        QKeySequence(Qt::META | Qt::Key_A),
        QKeySequence(Qt::META | Qt::Key_G),
    };
    for (const QKeySequence &sequence : sequences) {
        const auto shortcuts = KGlobalAccel::globalShortcutsByKey(sequence);
        for (const KGlobalShortcutInfo &shortcut : shortcuts) {
            if (isExpectedOwner(shortcut, sequence)) {
                continue;
            }
            qCritical().noquote()
                << QStringLiteral("Refusing to replace %1: already used by %2 / %3")
                       .arg(sequence.toString(QKeySequence::NativeText),
                            shortcut.componentFriendlyName(),
                            shortcut.friendlyName());
            return false;
        }
    }
    return true;
}

class NativeHost final : public QObject {
public:
    explicit NativeHost(bool forceRegistration, QObject *parent = nullptr)
        : QObject(parent),
          stdinNotifier_(STDIN_FILENO, QSocketNotifier::Read, this),
          forceRegistration_(forceRegistration) {
        auctionAction_.setObjectName(QStringLiteral("next-auction"));
        auctionAction_.setText(QStringLiteral("Start next Whatnot auction"));
        giveawayAction_.setObjectName(QStringLiteral("next-giveaway"));
        giveawayAction_.setText(QStringLiteral("Start next Whatnot giveaway"));
        inspectAction_.setObjectName(QStringLiteral("inspect-controls"));
        inspectAction_.setText(QStringLiteral("Inspect Whatnot controls (no click)"));

        auctionRegistered_ = registerShortcut(
            &auctionAction_, QKeySequence(Qt::META | Qt::Key_A));
        giveawayRegistered_ = registerShortcut(
            &giveawayAction_, QKeySequence(Qt::META | Qt::Key_G));
        KGlobalAccel::self()->setShortcut(&inspectAction_, {});

        connect(&auctionAction_, &QAction::triggered, this, [this] {
            trigger(QStringLiteral("auction"), auctionLastRun_);
        });
        connect(&giveawayAction_, &QAction::triggered, this, [this] {
            trigger(QStringLiteral("giveaway"), giveawayLastRun_);
        });
        connect(&inspectAction_, &QAction::triggered, this, [this] {
            writeCommand(QStringLiteral("inspect"));
        });
        connect(&stdinNotifier_, &QSocketNotifier::activated, this, [this] {
            readStdin();
        });
    }

    bool shortcutsRegistered() const {
        return auctionRegistered_ && giveawayRegistered_;
    }

private:
    bool registerShortcut(QAction *action, const QKeySequence &sequence) {
        if (qEnvironmentVariableIsSet("QUICK_SWAP_NO_SHORTCUTS")) {
            return false;
        }
        if (forceRegistration_) {
            KGlobalAccel::stealShortcutSystemwide(sequence);
            KGlobalAccel::self()->setDefaultShortcut(
                action, {sequence}, KGlobalAccel::NoAutoloading);
            return KGlobalAccel::self()->setShortcut(
                action, {sequence}, KGlobalAccel::NoAutoloading);
        }
        KGlobalAccel::self()->setDefaultShortcut(action, {sequence});
        return KGlobalAccel::self()->setShortcut(action, {sequence});
    }

    void trigger(const QString &action, QElapsedTimer &timer) {
        if (timer.isValid() && timer.elapsed() < kDebounceMs) {
            return;
        }
        timer.restart();
        writeCommand(action);
    }

    void writeCommand(const QString &action) {
        writeMessage({
            {QStringLiteral("type"), QStringLiteral("command")},
            {QStringLiteral("id"), QUuid::createUuid().toString(QUuid::WithoutBraces)},
            {QStringLiteral("action"), action},
        });
    }

    void readStdin() {
        std::array<char, 65536> chunk{};
        const ssize_t count = ::read(STDIN_FILENO, chunk.data(), chunk.size());
        if (count <= 0) {
            QCoreApplication::quit();
            return;
        }
        inputBuffer_.append(chunk.data(), static_cast<qsizetype>(count));
        processFrames();
    }

    void processFrames() {
        while (inputBuffer_.size() >= 4) {
            const auto *bytes = reinterpret_cast<const uchar *>(inputBuffer_.constData());
            const quint32 length = qFromLittleEndian<quint32>(bytes);
            if (length > kMaxMessageBytes) {
                QCoreApplication::exit(2);
                return;
            }
            if (inputBuffer_.size() < static_cast<qsizetype>(4 + length)) {
                return;
            }
            const QByteArray payload = inputBuffer_.mid(4, length);
            inputBuffer_.remove(0, 4 + length);
            const QJsonDocument document = QJsonDocument::fromJson(payload);
            if (!document.isObject()) {
                continue;
            }
            handleMessage(document.object());
        }
    }

    void handleMessage(const QJsonObject &message) {
        const QString type = message.value(QStringLiteral("type")).toString();
        if (type == QStringLiteral("hello")) {
            writeMessage({
                {QStringLiteral("type"), QStringLiteral("ready")},
                {QStringLiteral("auctionShortcut"), auctionRegistered_},
                {QStringLiteral("giveawayShortcut"), giveawayRegistered_},
            });
            return;
        }
        if (type == QStringLiteral("result") && !message.value(QStringLiteral("ok")).toBool()) {
            const QString error = message.value(QStringLiteral("error")).toString(
                QStringLiteral("Unknown Whatnot control error"));
            QProcess::startDetached(
                QStringLiteral("notify-send"),
                {QStringLiteral("--urgency"), QStringLiteral("critical"),
                 QStringLiteral("Quick Swap failed"), error});
        }
        if (type == QStringLiteral("result")) {
            QJsonObject persistedMessage{
                {QStringLiteral("type"), message.value(QStringLiteral("type"))},
                {QStringLiteral("id"), message.value(QStringLiteral("id"))},
                {QStringLiteral("action"), message.value(QStringLiteral("action"))},
                {QStringLiteral("ok"), message.value(QStringLiteral("ok"))},
            };
            if (message.contains(QStringLiteral("error"))) {
                QString error = message.value(QStringLiteral("error")).toString();
                error.replace(
                    QRegularExpression(QStringLiteral(R"(https?://\S+)")),
                    QStringLiteral("[redacted-url]"));
                persistedMessage.insert(QStringLiteral("error"), error);
            }
            const QString stateRoot = qEnvironmentVariable(
                "XDG_STATE_HOME", QDir::homePath() + QStringLiteral("/.local/state"));
            const QString stateDirectory = stateRoot + QStringLiteral("/quick-swap-tools");
            QDir().mkpath(stateDirectory);
            QFile resultFile(stateDirectory + QStringLiteral("/last-result.json"));
            if (resultFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
                resultFile.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
                resultFile.write(QJsonDocument(persistedMessage).toJson(QJsonDocument::Indented));
            }
        }
    }

    void writeMessage(const QJsonObject &message) {
        const QByteArray payload = QJsonDocument(message).toJson(QJsonDocument::Compact);
        const quint32 littleEndianLength = qToLittleEndian<quint32>(payload.size());
        std::fwrite(&littleEndianLength, sizeof(littleEndianLength), 1, stdout);
        std::fwrite(payload.constData(), 1, payload.size(), stdout);
        std::fflush(stdout);
    }

    QAction auctionAction_;
    QAction giveawayAction_;
    QAction inspectAction_;
    QSocketNotifier stdinNotifier_;
    QByteArray inputBuffer_;
    QElapsedTimer auctionLastRun_;
    QElapsedTimer giveawayLastRun_;
    bool forceRegistration_ = false;
    bool auctionRegistered_ = false;
    bool giveawayRegistered_ = false;
};
} // namespace

int main(int argc, char **argv) {
    QGuiApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("quick-swap-tools"));
    QCoreApplication::setOrganizationName(QStringLiteral("OniByts"));
    QGuiApplication::setDesktopFileName(QStringLiteral("quick-swap-tools"));

    const bool checkOnly = app.arguments().contains(QStringLiteral("--check-shortcuts"));
    const bool registerOnly = app.arguments().contains(QStringLiteral("--register-only"));
    if ((checkOnly || registerOnly) && !shortcutsCanBeClaimed()) {
        return 2;
    }
    if (checkOnly) {
        return 0;
    }
    NativeHost host(registerOnly);
    if (registerOnly) {
        return host.shortcutsRegistered() ? 0 : 1;
    }
    return app.exec();
}
