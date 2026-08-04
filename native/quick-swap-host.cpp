#include <QAction>
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QKeySequence>
#include <QProcess>
#include <QRegularExpression>
#include <QSocketNotifier>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTimer>
#include <QDebug>
#include <QUuid>
#include <QtEndian>

#include <KGlobalAccel>
#include <KGlobalShortcutInfo>

#include <array>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fcntl.h>
#include <linux/input.h>
#include <functional>
#include <memory>
#include <sys/file.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <unistd.h>

namespace {
constexpr qint64 kDebounceMs = 150;
constexpr quint32 kMaxMessageBytes = 1024 * 1024;

bool isLogitechR400(const input_id &device, const QString &name) {
    return device.bustype == BUS_USB
        && device.vendor == 0x046d
        && device.product == 0xc538
        && name == QStringLiteral("Logitech USB Receiver");
}

bool isLogitechR400Interface(const QByteArray &number) {
    return number.trimmed() == QByteArrayLiteral("00");
}

bool isLogitechR400Interface(dev_t deviceNumber) {
    QString current = QFileInfo(QStringLiteral("/sys/dev/char/%1:%2/device")
        .arg(major(deviceNumber)).arg(minor(deviceNumber))).canonicalFilePath();
    while (!current.isEmpty() && current != QStringLiteral("/")) {
        QFile number(QDir(current).filePath(QStringLiteral("bInterfaceNumber")));
        if (number.open(QIODevice::ReadOnly)) {
            return isLogitechR400Interface(number.readAll());
        }
        current = QFileInfo(current).dir().absolutePath();
    }
    return false;
}

bool hasLogitechR400Keys(const unsigned long *bits, std::size_t count) {
    constexpr std::size_t wordBits = sizeof(unsigned long) * 8;
    const auto present = [=](unsigned int key) {
        return key / wordBits < count
            && (bits[key / wordBits] & (1UL << (key % wordBits))) != 0;
    };
    return present(KEY_PAGEUP) && present(KEY_PAGEDOWN);
}

QString decodeEvdevName(const char *bytes, int length) {
    while (length > 0 && bytes[length - 1] == '\0') {
        --length;
    }
    return QString::fromLocal8Bit(bytes, length);
}

QString logitechR400Action(
    const input_id &device,
    const QString &name,
    const input_event &event) {
    if (!isLogitechR400(device, name) || event.type != EV_KEY || event.value != 1) {
        return {};
    }
    if (event.code == KEY_PAGEUP) {
        return QStringLiteral("auction");
    }
    if (event.code == KEY_PAGEDOWN) {
        return QStringLiteral("giveaway");
    }
    return {};
}

enum class R400ReadResult {
    drained,
    disconnected,
    invalid,
};

R400ReadResult readLogitechR400Events(
    int descriptor,
    const input_id &device,
    const QString &name,
    const std::function<void(const QString &)> &onAction) {
    std::array<input_event, 16> events{};
    while (true) {
        const ssize_t count = ::read(descriptor, events.data(), sizeof(events));
        if (count > 0) {
            if (count % static_cast<ssize_t>(sizeof(input_event)) != 0) {
                return R400ReadResult::invalid;
            }
            const std::size_t eventCount = static_cast<std::size_t>(count)
                / sizeof(input_event);
            for (std::size_t index = 0; index < eventCount; ++index) {
                const QString action = logitechR400Action(device, name, events[index]);
                if (!action.isEmpty()) {
                    onAction(action);
                }
            }
            continue;
        }
        if (count == 0) {
            return R400ReadResult::disconnected;
        }
        if (errno == EINTR) {
            continue;
        }
        return errno == EAGAIN || errno == EWOULDBLOCK
            ? R400ReadResult::drained
            : R400ReadResult::disconnected;
    }
}

int runLogitechR400ProfileSelfTest() {
    const input_id r400{BUS_USB, 0x046d, 0xc538, 0x0111};
    const input_id other{BUS_USB, 0x046d, 0xc539, 0x0111};
    const QString name = QStringLiteral("Logitech USB Receiver");
    const auto action = [&](const input_id &device, quint16 code, qint32 value) {
        input_event event{};
        event.type = EV_KEY;
        event.code = code;
        event.value = value;
        return logitechR400Action(device, name, event);
    };
    const QString previous = action(r400, KEY_PAGEUP, 1);
    const QString next = action(r400, KEY_PAGEDOWN, 1);
    constexpr char kernelName[] = "Logitech USB Receiver";
    const bool kernelNameMatched = isLogitechR400(
        r400, decodeEvdevName(kernelName, sizeof(kernelName)));
    const bool interfaceMatched = isLogitechR400Interface("00\n");
    const bool otherInterfaceIgnored = !isLogitechR400Interface("01\n");
    std::array<unsigned long, (KEY_MAX / (sizeof(unsigned long) * 8)) + 1> keyBits{};
    constexpr std::size_t wordBits = sizeof(unsigned long) * 8;
    keyBits[KEY_PAGEUP / wordBits] |= 1UL << (KEY_PAGEUP % wordBits);
    keyBits[KEY_PAGEDOWN / wordBits] |= 1UL << (KEY_PAGEDOWN % wordBits);
    const bool capabilitiesMatched = hasLogitechR400Keys(keyBits.data(), keyBits.size());
    const bool matchedDevice = previous == QStringLiteral("auction")
        && next == QStringLiteral("giveaway");
    const bool otherDeviceIgnored = action(other, KEY_PAGEUP, 1).isEmpty();
    const bool otherKeyIgnored = action(r400, KEY_F5, 1).isEmpty();
    const bool releaseIgnored = action(r400, KEY_PAGEUP, 0).isEmpty();
    const bool repeatIgnored = action(r400, KEY_PAGEUP, 2).isEmpty();
    const QJsonObject result{
        {QStringLiteral("matchedDevice"), matchedDevice},
        {QStringLiteral("kernelNameMatched"), kernelNameMatched},
        {QStringLiteral("interfaceMatched"), interfaceMatched},
        {QStringLiteral("otherInterfaceIgnored"), otherInterfaceIgnored},
        {QStringLiteral("capabilitiesMatched"), capabilitiesMatched},
        {QStringLiteral("previous"), previous},
        {QStringLiteral("next"), next},
        {QStringLiteral("otherDeviceIgnored"), otherDeviceIgnored},
        {QStringLiteral("otherKeyIgnored"), otherKeyIgnored},
        {QStringLiteral("releaseIgnored"), releaseIgnored},
        {QStringLiteral("repeatIgnored"), repeatIgnored},
    };
    const QByteArray output = QJsonDocument(result).toJson(QJsonDocument::Compact) + '\n';
    std::fwrite(output.constData(), 1, output.size(), stdout);
    return matchedDevice && kernelNameMatched && otherDeviceIgnored && otherKeyIgnored
            && releaseIgnored && repeatIgnored
        ? 0
        : 1;
}

class R400OwnerLease final {
public:
    explicit R400OwnerLease(const QByteArray &path) {
        descriptor_ = ::open(
            path.constData(),
            O_CREAT | O_RDWR | O_CLOEXEC | O_NOFOLLOW,
            S_IRUSR | S_IWUSR);
        if (descriptor_ < 0) {
            return;
        }
        struct stat metadata{};
        if (::fstat(descriptor_, &metadata) != 0
            || !S_ISREG(metadata.st_mode)
            || metadata.st_uid != ::getuid()
            || (metadata.st_mode & 0777) != (S_IRUSR | S_IWUSR)) {
            ::close(descriptor_);
            descriptor_ = -1;
            return;
        }
        ownerOnly_ = true;
        if (::flock(descriptor_, LOCK_EX | LOCK_NB) != 0) {
            ::close(descriptor_);
            descriptor_ = -1;
        }
    }

    ~R400OwnerLease() {
        if (descriptor_ >= 0) {
            ::flock(descriptor_, LOCK_UN);
            ::close(descriptor_);
        }
    }

    R400OwnerLease(const R400OwnerLease &) = delete;
    R400OwnerLease &operator=(const R400OwnerLease &) = delete;

    [[nodiscard]] bool acquired() const {
        return descriptor_ >= 0;
    }

    [[nodiscard]] bool ownerOnly() const {
        return ownerOnly_;
    }

private:
    int descriptor_ = -1;
    bool ownerOnly_ = false;
};

int runLogitechR400OwnerSelfTest() {
    QTemporaryDir directory;
    if (!directory.isValid()) {
        return 2;
    }
    const QByteArray path = QFile::encodeName(directory.filePath(
        QStringLiteral("r400-owner.lock")));
    auto first = std::make_unique<R400OwnerLease>(path);
    R400OwnerLease second(path);
    const bool exclusive = first->acquired() && !second.acquired();
    const bool ownerOnly = first->ownerOnly();
    first.reset();
    R400OwnerLease third(path);
    const bool takeover = third.acquired();
    const QJsonObject result{
        {QStringLiteral("exclusive"), exclusive},
        {QStringLiteral("takeover"), takeover},
        {QStringLiteral("ownerOnly"), ownerOnly},
    };
    const QByteArray output = QJsonDocument(result).toJson(QJsonDocument::Compact) + '\n';
    std::fwrite(output.constData(), 1, output.size(), stdout);
    return exclusive && takeover && ownerOnly ? 0 : 1;
}

int runLogitechR400StreamSelfTest() {
    int descriptors[2] = {-1, -1};
    if (::pipe2(descriptors, O_CLOEXEC | O_NONBLOCK) != 0) {
        return 2;
    }
    const input_id r400{BUS_USB, 0x046d, 0xc538, 0x0111};
    const QString name = QStringLiteral("Logitech USB Receiver");
    std::array<input_event, 6> events{};
    events[0] = input_event{{}, EV_KEY, KEY_PAGEUP, 0};
    events[1] = input_event{{}, EV_KEY, KEY_PAGEUP, 2};
    events[2] = input_event{{}, EV_KEY, KEY_F5, 1};
    events[3] = input_event{{}, EV_SYN, SYN_REPORT, 0};
    events[4] = input_event{{}, EV_KEY, KEY_PAGEUP, 1};
    events[5] = input_event{{}, EV_KEY, KEY_PAGEDOWN, 1};
    const ssize_t expected = static_cast<ssize_t>(sizeof(events));
    const ssize_t written = ::write(descriptors[1], events.data(), sizeof(events));
    QJsonArray actions;
    const R400ReadResult readResult = readLogitechR400Events(
        descriptors[0], r400, name, [&actions](const QString &action) {
            actions.append(action);
        });
    ::close(descriptors[0]);
    ::close(descriptors[1]);
    const bool completeFrames = written == expected
        && readResult == R400ReadResult::drained;
    const bool expectedActions = actions == QJsonArray{
        QStringLiteral("auction"), QStringLiteral("giveaway")};
    const QJsonObject result{
        {QStringLiteral("actions"), actions},
        {QStringLiteral("completeFrames"), completeFrames},
    };
    const QByteArray output = QJsonDocument(result).toJson(QJsonDocument::Compact) + '\n';
    std::fwrite(output.constData(), 1, output.size(), stdout);
    return completeFrames && expectedActions ? 0 : 1;
}

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

        if (forceRegistration_) {
            auctionRegistered_ = registerShortcut(
                &auctionAction_, QKeySequence(Qt::META | Qt::Key_A));
            giveawayRegistered_ = registerShortcut(
                &giveawayAction_, QKeySequence(Qt::META | Qt::Key_G));
            KGlobalAccel::self()->setShortcut(&inspectAction_, {});
        }

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
        if (!forceRegistration_) {
            r400RetryTimer_.setInterval(1500);
            connect(&r400RetryTimer_, &QTimer::timeout, this, [this] {
                ensureLogitechR400Input();
            });
            ensureLogitechR400Input();
            r400RetryTimer_.start();
        }
    }

    ~NativeHost() override {
        closeLogitechR400Input();
    }

    bool shortcutsRegistered() const {
        return auctionRegistered_ && giveawayRegistered_;
    }

private:
    void ensureLogitechR400Input() {
        if (!r400Owner_) {
            const QString runtimeDirectory = QStandardPaths::writableLocation(
                QStandardPaths::RuntimeLocation);
            if (runtimeDirectory.isEmpty()) {
                return;
            }
            const QByteArray lockPath = QFile::encodeName(
                QDir(runtimeDirectory).filePath(
                    QStringLiteral("quick-swap-tools-input-owner.lock")));
            auto owner = std::make_unique<R400OwnerLease>(lockPath);
            if (!owner->acquired()) {
                return;
            }
            r400Owner_ = std::move(owner);
        }
        if (!auctionRegistered_) {
            auctionRegistered_ = registerShortcut(
                &auctionAction_, QKeySequence(Qt::META | Qt::Key_A));
        }
        if (!giveawayRegistered_) {
            giveawayRegistered_ = registerShortcut(
                &giveawayAction_, QKeySequence(Qt::META | Qt::Key_G));
        }
        KGlobalAccel::self()->setShortcut(&inspectAction_, {});
        if (qEnvironmentVariableIsSet("QUICK_SWAP_NO_R400")) {
            return;
        }
        if (r400Descriptor_ >= 0) {
            return;
        }

        QDir inputDirectory(QStringLiteral("/dev/input"));
        const QStringList candidates = inputDirectory.entryList(
            {QStringLiteral("event*")}, QDir::System | QDir::Files, QDir::Name);
        for (const QString &candidate : candidates) {
            const QByteArray path = QFile::encodeName(inputDirectory.filePath(candidate));
            const int descriptor = ::open(
                path.constData(), O_RDONLY | O_NONBLOCK | O_CLOEXEC | O_NOFOLLOW);
            if (descriptor < 0) {
                continue;
            }
            struct stat metadata{};
            input_id device{};
            std::array<char, 256> nameBytes{};
            std::array<unsigned long, (KEY_MAX / (sizeof(unsigned long) * 8)) + 1> keyBits{};
            const int nameLength = ::ioctl(
                descriptor, EVIOCGNAME(nameBytes.size()), nameBytes.data());
            if (::fstat(descriptor, &metadata) != 0
                || !S_ISCHR(metadata.st_mode)
                || ::ioctl(descriptor, EVIOCGID, &device) != 0
                || ::ioctl(descriptor, EVIOCGBIT(EV_KEY, sizeof(keyBits)), keyBits.data()) < 0
                || nameLength <= 0) {
                ::close(descriptor);
                continue;
            }
            const QString name = decodeEvdevName(
                nameBytes.data(), std::min<int>(nameLength, nameBytes.size()));
            if (!isLogitechR400(device, name)
                || !isLogitechR400Interface(metadata.st_rdev)
                || !hasLogitechR400Keys(keyBits.data(), keyBits.size())) {
                ::close(descriptor);
                continue;
            }
            r400Descriptor_ = descriptor;
            r400Device_ = device;
            r400Name_ = name;
            r400Notifier_ = std::make_unique<QSocketNotifier>(
                r400Descriptor_, QSocketNotifier::Read);
            connect(r400Notifier_.get(), &QSocketNotifier::activated, this, [this] {
                handleLogitechR400Input();
            });
            return;
        }
    }

    void handleLogitechR400Input() {
        const R400ReadResult result = readLogitechR400Events(
            r400Descriptor_, r400Device_, r400Name_, [this](const QString &action) {
                if (action == QStringLiteral("auction")) {
                    trigger(action, auctionLastRun_);
                } else if (action == QStringLiteral("giveaway")) {
                    trigger(action, giveawayLastRun_);
                }
            });
        if (result != R400ReadResult::drained) {
            closeLogitechR400Input();
        }
    }

    void closeLogitechR400Input() {
        r400Notifier_.reset();
        if (r400Descriptor_ >= 0) {
            ::close(r400Descriptor_);
            r400Descriptor_ = -1;
        }
        r400Device_ = {};
        r400Name_.clear();
    }

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
                {QStringLiteral("logitechR400"), r400Descriptor_ >= 0},
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

    // Declared first so it is destroyed last, after QAction unregisters inputs.
    std::unique_ptr<R400OwnerLease> r400Owner_;
    QAction auctionAction_;
    QAction giveawayAction_;
    QAction inspectAction_;
    QSocketNotifier stdinNotifier_;
    QTimer r400RetryTimer_;
    std::unique_ptr<QSocketNotifier> r400Notifier_;
    int r400Descriptor_ = -1;
    input_id r400Device_{};
    QString r400Name_;
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
    if (app.arguments().contains(QStringLiteral("--r400-profile-self-test"))) {
        return runLogitechR400ProfileSelfTest();
    }
    if (app.arguments().contains(QStringLiteral("--r400-owner-self-test"))) {
        return runLogitechR400OwnerSelfTest();
    }
    if (app.arguments().contains(QStringLiteral("--r400-stream-self-test"))) {
        return runLogitechR400StreamSelfTest();
    }
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
