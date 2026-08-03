#include <QApplication>

#include <QColor>
#include <QCoreApplication>
#include <QDBusInterface>
#include <QDBusMessage>
#include <QDBusMetaType>
#include <QDBusReply>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyCombination>
#include <QKeySequence>
#include <QLabel>
#include <QMessageBox>
#include <QPalette>
#include <QProcess>
#include <QPushButton>
#include <QStyleFactory>
#include <QVBoxLayout>

#include <KGlobalAccel>
#include <KGlobalShortcutInfo>
#include <KKeySequenceRecorder>
#include <KKeySequenceWidget>

#include <array>
#include <iostream>
#include <utility>

namespace {
constexpr auto kComponent = "quick-swap-tools";

QPalette darkPalette() {
    QPalette palette;
    palette.setColor(QPalette::Window, QColor(QStringLiteral("#0b0f14")));
    palette.setColor(QPalette::WindowText, QColor(QStringLiteral("#e6edf3")));
    palette.setColor(QPalette::Base, QColor(QStringLiteral("#0d1117")));
    palette.setColor(QPalette::AlternateBase, QColor(QStringLiteral("#161b22")));
    palette.setColor(QPalette::Text, QColor(QStringLiteral("#e6edf3")));
    palette.setColor(QPalette::Button, QColor(QStringLiteral("#21262d")));
    palette.setColor(QPalette::ButtonText, QColor(QStringLiteral("#e6edf3")));
    palette.setColor(QPalette::Highlight, QColor(QStringLiteral("#1f6feb")));
    palette.setColor(QPalette::HighlightedText, QColor(QStringLiteral("#ffffff")));
    palette.setColor(QPalette::PlaceholderText, QColor(QStringLiteral("#8b949e")));
    palette.setColor(QPalette::ToolTipBase, QColor(QStringLiteral("#161b22")));
    palette.setColor(QPalette::ToolTipText, QColor(QStringLiteral("#e6edf3")));
    palette.setColor(QPalette::BrightText, QColor(QStringLiteral("#f85149")));
    palette.setColor(QPalette::Link, QColor(QStringLiteral("#58a6ff")));
    return palette;
}

QString darkStyleSheet() {
    return QStringLiteral(R"(
        QWidget {
            background-color: #0b0f14;
            color: #e6edf3;
            font-family: "Noto Sans", "Segoe UI", sans-serif;
            font-size: 14px;
        }
        QDialog { background-color: #0b0f14; }
        QLabel#eyebrow {
            color: #58a6ff;
            font-size: 11px;
            font-weight: 700;
        }
        QLabel#pageTitle {
            color: #f0f6fc;
            font-size: 26px;
            font-weight: 700;
        }
        QLabel#sectionTitle {
            color: #f0f6fc;
            font-size: 16px;
            font-weight: 650;
        }
        QLabel#muted, QLabel#statusText { color: #8b949e; }
        QLabel#statusText {
            background-color: #0d1117;
            border: 1px solid #21262d;
            border-radius: 8px;
            padding: 10px 12px;
        }
        QFrame#controlCard, QFrame#deviceCard {
            background-color: #161b22;
            border: 1px solid #30363d;
            border-radius: 12px;
        }
        QFrame#controlCard QLabel, QFrame#deviceCard QLabel {
            background-color: transparent;
        }
        QPushButton {
            min-height: 36px;
            padding: 0 16px;
            background-color: #21262d;
            border: 1px solid #30363d;
            border-radius: 8px;
            color: #e6edf3;
            font-weight: 600;
        }
        QPushButton:hover { background-color: #30363d; border-color: #484f58; }
        QPushButton:pressed { background-color: #161b22; }
        QPushButton:focus { border: 2px solid #58a6ff; }
        QPushButton[primary="true"] {
            background-color: #1f6feb;
            border-color: #388bfd;
            color: #ffffff;
        }
        QPushButton[primary="true"]:hover { background-color: #388bfd; }
        KKeySequenceWidget {
            min-height: 40px;
            background-color: #0d1117;
            border: 1px solid #30363d;
            border-radius: 8px;
        }
        KKeySequenceWidget QPushButton {
            background-color: #0d1117;
            border: none;
        }
        QMessageBox { background-color: #0b0f14; }
    )");
}

struct ShortcutAction {
    QString uniqueName;
    QString friendlyName;
    QKeySequence defaultSequence;
};

const ShortcutAction kAuction{
    QStringLiteral("next-auction"),
    QStringLiteral("Start next Whatnot auction"),
    QKeySequence(Qt::META | Qt::Key_A),
};
const ShortcutAction kGiveaway{
    QStringLiteral("next-giveaway"),
    QStringLiteral("Start next Whatnot giveaway"),
    QKeySequence(Qt::META | Qt::Key_G),
};

QStringList actionId(const ShortcutAction &action) {
    return {
        QString::fromLatin1(kComponent),
        action.uniqueName,
        QString::fromLatin1(kComponent),
        action.friendlyName,
    };
}

QKeySequence parseSequence(const QString &text) {
    if (text.compare(QStringLiteral("none"), Qt::CaseInsensitive) == 0
        || text.trimmed().isEmpty()) {
        return {};
    }
    return QKeySequence::fromString(text, QKeySequence::PortableText);
}

bool isRiskyUnmodifiedKey(const QKeySequence &sequence) {
    if (sequence.count() != 1) {
        return false;
    }
    const QKeyCombination combination = sequence[0];
    if (combination.keyboardModifiers() != Qt::NoModifier) {
        return false;
    }
    const int key = static_cast<int>(combination.key());
    return key < Qt::Key_F13 || key > Qt::Key_F24;
}

QJsonObject validateShortcuts(
    const QKeySequence &auction, const QKeySequence &giveaway) {
    QJsonObject result;
    QJsonArray warnings;
    if (auction.isEmpty() || giveaway.isEmpty()) {
        result.insert(QStringLiteral("valid"), false);
        result.insert(QStringLiteral("error"), QStringLiteral("both shortcuts are required"));
        result.insert(QStringLiteral("warnings"), warnings);
        return result;
    }
    if (auction == giveaway) {
        result.insert(QStringLiteral("valid"), false);
        result.insert(
            QStringLiteral("error"),
            QStringLiteral("auction and giveaway shortcuts must differ"));
        result.insert(QStringLiteral("warnings"), warnings);
        return result;
    }
    if (isRiskyUnmodifiedKey(auction)) {
        warnings.append(QStringLiteral("auction shortcut is an unmodified global key"));
    }
    if (isRiskyUnmodifiedKey(giveaway)) {
        warnings.append(QStringLiteral("giveaway shortcut is an unmodified global key"));
    }
    result.insert(QStringLiteral("valid"), true);
    result.insert(QStringLiteral("warnings"), warnings);
    return result;
}

enum class ShortcutSlot {
    Auction = 0,
    Giveaway = 1,
};

struct TransactionResult {
    bool applied = false;
    bool rollbackComplete = true;
    QString error;
};

template<typename Writer>
TransactionResult replaceShortcutsTransaction(
    Writer write,
    const QKeySequence &auction,
    const QKeySequence &giveaway,
    const QList<QKeySequence> &savedAuction,
    const QList<QKeySequence> &savedGiveaway) {
    TransactionResult result;
    QString operationError;
    result.applied =
        write(ShortcutSlot::Auction, {}, &operationError)
        && write(ShortcutSlot::Giveaway, {}, &operationError)
        && write(ShortcutSlot::Auction, {auction}, &operationError)
        && write(ShortcutSlot::Giveaway, {giveaway}, &operationError);
    if (result.applied) {
        return result;
    }

    QString auctionClearError;
    QString giveawayClearError;
    QString auctionRollbackError;
    QString giveawayRollbackError;
    const bool auctionCleared = write(
        ShortcutSlot::Auction, {}, &auctionClearError);
    const bool giveawayCleared = write(
        ShortcutSlot::Giveaway, {}, &giveawayClearError);
    const bool auctionRestored = write(
        ShortcutSlot::Auction, savedAuction, &auctionRollbackError);
    const bool giveawayRestored = write(
        ShortcutSlot::Giveaway, savedGiveaway, &giveawayRollbackError);
    result.rollbackComplete = auctionCleared && giveawayCleared
        && auctionRestored && giveawayRestored;
    result.error = operationError.isEmpty()
        ? QStringLiteral("KDE rejected the shortcut transaction.")
        : operationError;
    if (!result.rollbackComplete) {
        result.error += QStringLiteral(
            " Rollback was incomplete; open KDE Shortcuts to restore the mappings.");
        const QStringList rollbackErrors{
            auctionClearError,
            giveawayClearError,
            auctionRollbackError,
            giveawayRollbackError,
        };
        QString rollbackDetail;
        for (const QString &candidate : rollbackErrors) {
            if (!candidate.isEmpty()) {
                rollbackDetail = candidate;
                break;
            }
        }
        if (!rollbackDetail.isEmpty()) {
            result.error += QStringLiteral(" (%1)").arg(rollbackDetail);
        }
    }
    return result;
}

class ShortcutStore {
public:
    ShortcutStore()
        : interface_(
              QStringLiteral("org.kde.kglobalaccel"),
              QStringLiteral("/kglobalaccel"),
              QStringLiteral("org.kde.KGlobalAccel")) {
        qDBusRegisterMetaType<QKeySequence>();
        qDBusRegisterMetaType<QList<QKeySequence>>();
    }

    bool isAvailable() const {
        return interface_.isValid();
    }

    bool read(
        const ShortcutAction &action,
        QList<QKeySequence> *sequences,
        QString *error = nullptr) {
        QDBusReply<QList<QKeySequence>> reply = interface_.call(
            QStringLiteral("shortcutKeys"), actionId(action));
        if (!reply.isValid()) {
            if (error) {
                *error = reply.error().message();
            }
            return false;
        }
        *sequences = reply.value();
        return true;
    }

    bool writeAll(
        const ShortcutAction &action,
        const QList<QKeySequence> &sequences,
        QString *error = nullptr) {
        const QVariantList arguments{
            QVariant::fromValue(actionId(action)),
            QVariant::fromValue(sequences),
        };
        const QDBusMessage reply = interface_.callWithArgumentList(
            QDBus::Block, QStringLiteral("setForeignShortcutKeys"), arguments);
        if (reply.type() == QDBusMessage::ErrorMessage) {
            if (error) {
                *error = reply.errorMessage();
            }
            return false;
        }
        QList<QKeySequence> stored;
        if (!read(action, &stored, error)) {
            return false;
        }
        return sameSequences(stored, sequences);
    }

    bool replacePair(
        const QKeySequence &auction,
        const QKeySequence &giveaway,
        const QList<QKeySequence> &savedAuction,
        const QList<QKeySequence> &savedGiveaway,
        QString *error) {
        const TransactionResult result = replaceShortcutsTransaction(
            [this](
                ShortcutSlot slot,
                const QList<QKeySequence> &sequences,
                QString *writeError) {
                return writeAll(
                    slot == ShortcutSlot::Auction ? kAuction : kGiveaway,
                    sequences,
                    writeError);
            },
            auction,
            giveaway,
            savedAuction,
            savedGiveaway);
        if (error) {
            *error = result.error;
        }
        return result.applied;
    }

private:
    static bool sameSequences(
        const QList<QKeySequence> &left,
        const QList<QKeySequence> &right) {
        if (left.size() != right.size()) {
            return false;
        }
        for (const QKeySequence &sequence : left) {
            if (!right.contains(sequence)) {
                return false;
            }
        }
        return true;
    }

    QDBusInterface interface_;
};

QString externalConflict(const QKeySequence &sequence) {
    const auto shortcuts = KGlobalAccel::globalShortcutsByKey(sequence);
    for (const KGlobalShortcutInfo &shortcut : shortcuts) {
        if (shortcut.componentUniqueName() == QString::fromLatin1(kComponent)
            && (shortcut.uniqueName() == kAuction.uniqueName
                || shortcut.uniqueName() == kGiveaway.uniqueName)) {
            continue;
        }
        return QStringLiteral("%1 — %2")
            .arg(shortcut.componentFriendlyName(), shortcut.friendlyName());
    }
    return {};
}

void configureRecorder(KKeySequenceWidget *widget) {
    widget->setMultiKeyShortcutsAllowed(false);
    widget->setPatterns(
        KKeySequenceRecorder::Key | KKeySequenceRecorder::ModifierAndKey);
    widget->setCheckForConflictsAgainst(KKeySequenceWidget::None);
    widget->setClearButtonShown(false);
    widget->setComponentName(QString::fromLatin1(kComponent));
}

int runValidationCli(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    const QStringList arguments = app.arguments();
    if (arguments.size() != 4) {
        std::cerr << "usage: quick-swap-config --validate-shortcuts <auction> <giveaway>\n";
        return 64;
    }
    const QJsonObject validation = validateShortcuts(
        parseSequence(arguments[2]), parseSequence(arguments[3]));
    std::cout << QJsonDocument(validation).toJson(QJsonDocument::Compact).toStdString()
              << '\n';
    return validation.value(QStringLiteral("valid")).toBool() ? 0 : 2;
}

int runTransactionSelfTest(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    const QList<QKeySequence> oldAuction{QKeySequence(Qt::Key_F20)};
    const QList<QKeySequence> oldGiveaway{QKeySequence(Qt::Key_F21)};
    const QKeySequence newAuction(Qt::Key_F21);
    const QKeySequence newGiveaway(Qt::Key_F20);

    auto run = [&](int failAfterWriteAt) {
        std::array<QList<QKeySequence>, 2> state{oldAuction, oldGiveaway};
        int callCount = 0;
        const TransactionResult result = replaceShortcutsTransaction(
            [&](ShortcutSlot slot, const QList<QKeySequence> &sequences, QString *error) {
                ++callCount;
                const int index = static_cast<int>(slot);
                const int other = index == 0 ? 1 : 0;
                if (!sequences.isEmpty()
                    && state[other].contains(sequences.first())) {
                    if (error) {
                        *error = QStringLiteral("conflicting sequence");
                    }
                    return false;
                }
                state[index] = sequences;
                if (callCount == failAfterWriteAt) {
                    if (error) {
                        *error = QStringLiteral("injected read-back failure");
                    }
                    return false;
                }
                return true;
            },
            newAuction,
            newGiveaway,
            oldAuction,
            oldGiveaway);
        return std::pair{result, state};
    };

    const auto [swapResult, swapState] = run(-1);
    const auto [rollbackResult, rollbackState] = run(4);
    const bool swapSucceeded = swapResult.applied
        && swapState[0] == QList<QKeySequence>{newAuction}
        && swapState[1] == QList<QKeySequence>{newGiveaway};
    const bool rollbackRestored = !rollbackResult.applied
        && rollbackResult.rollbackComplete
        && rollbackState[0] == oldAuction
        && rollbackState[1] == oldGiveaway;
    const QJsonObject output{
        {QStringLiteral("swapSucceeded"), swapSucceeded},
        {QStringLiteral("rollbackRestored"), rollbackRestored},
    };
    std::cout << QJsonDocument(output).toJson(QJsonDocument::Compact).toStdString()
              << '\n';
    return swapSucceeded && rollbackRestored ? 0 : 1;
}

int runGui(int argc, char **argv) {
    QApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("quick-swap-config"));
    QCoreApplication::setOrganizationName(QStringLiteral("OniByts"));
    app.setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
    app.setPalette(darkPalette());
    app.setStyleSheet(darkStyleSheet());

    ShortcutStore store;
    if (!store.isAvailable()) {
        QMessageBox::critical(
            nullptr,
            QStringLiteral("Quick Swap Tools"),
            QStringLiteral("KDE Global Shortcuts is unavailable. Log into a KDE Plasma session and try again."));
        return 1;
    }

    QString readError;
    QList<QKeySequence> savedAuction;
    QList<QKeySequence> savedGiveaway;
    if (!store.read(kAuction, &savedAuction, &readError)
        || !store.read(kGiveaway, &savedGiveaway, &readError)) {
        QMessageBox::critical(
            nullptr,
            QStringLiteral("Quick Swap Tools"),
            QStringLiteral("Could not read the current KDE shortcuts: %1").arg(readError));
        return 1;
    }
    const QKeySequence displayedAuction = savedAuction.isEmpty()
        ? kAuction.defaultSequence
        : savedAuction.first();
    const QKeySequence displayedGiveaway = savedGiveaway.isEmpty()
        ? kGiveaway.defaultSequence
        : savedGiveaway.first();

    QDialog dialog;
    dialog.setWindowTitle(QStringLiteral("Quick Swap Tools — Controls"));
    dialog.setMinimumSize(660, 590);
    dialog.resize(700, 640);

    auto *layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(28, 26, 28, 24);
    layout->setSpacing(14);

    auto *eyebrow = new QLabel(QStringLiteral("QUICK SWAP TOOLS"));
    eyebrow->setObjectName(QStringLiteral("eyebrow"));
    layout->addWidget(eyebrow);

    auto *title = new QLabel(QStringLiteral("Controls"));
    title->setObjectName(QStringLiteral("pageTitle"));
    layout->addWidget(title);

    auto *instructions = new QLabel(QStringLiteral(
        "Choose the shortcuts that start your next auction and giveaway. "
        "Changes stay local until you apply them."));
    instructions->setObjectName(QStringLiteral("muted"));
    instructions->setWordWrap(true);
    layout->addWidget(instructions);

    auto *controlCard = new QFrame();
    controlCard->setObjectName(QStringLiteral("controlCard"));
    auto *controlLayout = new QVBoxLayout(controlCard);
    controlLayout->setContentsMargins(20, 18, 20, 20);
    controlLayout->setSpacing(14);
    auto *controlTitle = new QLabel(QStringLiteral("Live action shortcuts"));
    controlTitle->setObjectName(QStringLiteral("sectionTitle"));
    controlLayout->addWidget(controlTitle);
    auto *controlHelp = new QLabel(QStringLiteral(
        "Select a binding, then press a keyboard, macro-pad, or mapped controller key."));
    controlHelp->setObjectName(QStringLiteral("muted"));
    controlHelp->setWordWrap(true);
    controlLayout->addWidget(controlHelp);

    auto *form = new QFormLayout();
    form->setContentsMargins(0, 4, 0, 0);
    form->setHorizontalSpacing(24);
    form->setVerticalSpacing(14);
    form->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    auto *auctionWidget = new KKeySequenceWidget();
    auto *giveawayWidget = new KKeySequenceWidget();
    configureRecorder(auctionWidget);
    configureRecorder(giveawayWidget);
    auctionWidget->setKeySequence(displayedAuction);
    giveawayWidget->setKeySequence(displayedGiveaway);
    form->addRow(QStringLiteral("Next auction"), auctionWidget);
    form->addRow(QStringLiteral("Next giveaway"), giveawayWidget);
    controlLayout->addLayout(form);
    layout->addWidget(controlCard);

    auto *deviceCard = new QFrame();
    deviceCard->setObjectName(QStringLiteral("deviceCard"));
    auto *deviceLayout = new QVBoxLayout(deviceCard);
    deviceLayout->setContentsMargins(20, 16, 20, 16);
    deviceLayout->setSpacing(6);
    auto *deviceTitle = new QLabel(QStringLiteral("Device tip"));
    deviceTitle->setObjectName(QStringLiteral("sectionTitle"));
    deviceLayout->addWidget(deviceTitle);
    auto *deviceNote = new QLabel(QStringLiteral(
        "Keyboard-style devices such as the Razer Tartarus work directly. For a "
        "gamepad, map its button to an unused key such as F13–F24. Normal letters "
        "also fire from your main keyboard."));
    deviceNote->setObjectName(QStringLiteral("muted"));
    deviceNote->setWordWrap(true);
    deviceLayout->addWidget(deviceNote);
    layout->addWidget(deviceCard);

    auto *status = new QLabel(QStringLiteral("Current mappings loaded from KDE."));
    status->setObjectName(QStringLiteral("statusText"));
    status->setWordWrap(true);
    layout->addWidget(status);

    auto *tools = new QHBoxLayout();
    auto *resetButton = new QPushButton(QStringLiteral("Reset to Super+A / Super+G"));
    auto *kdeButton = new QPushButton(QStringLiteral("Open KDE Shortcuts…"));
    tools->addWidget(resetButton);
    tools->addStretch();
    tools->addWidget(kdeButton);
    layout->addLayout(tools);

    auto *buttons = new QDialogButtonBox(
        QDialogButtonBox::Apply | QDialogButtonBox::Close);
    buttons->button(QDialogButtonBox::Apply)->setProperty("primary", true);
    layout->addWidget(buttons);

    QObject::connect(resetButton, &QPushButton::clicked, &dialog, [=] {
        auctionWidget->setKeySequence(kAuction.defaultSequence);
        giveawayWidget->setKeySequence(kGiveaway.defaultSequence);
        status->setText(QStringLiteral("Defaults selected. Click Apply to save them."));
    });
    QObject::connect(kdeButton, &QPushButton::clicked, &dialog, [] {
        QProcess::startDetached(QStringLiteral("kcmshell6"), {QStringLiteral("kcm_keys")});
    });
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    QObject::connect(buttons->button(QDialogButtonBox::Apply), &QPushButton::clicked, &dialog, [&] {
        const QKeySequence auction = auctionWidget->keySequence();
        const QKeySequence giveaway = giveawayWidget->keySequence();
        const QJsonObject validation = validateShortcuts(auction, giveaway);
        if (!validation.value(QStringLiteral("valid")).toBool()) {
            QMessageBox::warning(
                &dialog,
                QStringLiteral("Invalid controls"),
                validation.value(QStringLiteral("error")).toString());
            return;
        }

        const QJsonArray warnings = validation.value(QStringLiteral("warnings")).toArray();
        if (!warnings.isEmpty()) {
            const auto answer = QMessageBox::warning(
                &dialog,
                QStringLiteral("Unmodified global key"),
                QStringLiteral(
                    "At least one binding has no modifier. KDE cannot tell "
                    "your Tartarus from your main keyboard, so that key would trigger "
                    "Quick Swap Tools everywhere. Save it anyway?"),
                QMessageBox::Save | QMessageBox::Cancel,
                QMessageBox::Cancel);
            if (answer != QMessageBox::Save) {
                return;
            }
        }

        for (const auto &[name, sequence] : std::array{
                 std::pair{QStringLiteral("Next auction"), auction},
                 std::pair{QStringLiteral("Next giveaway"), giveaway},
             }) {
            const QString conflict = externalConflict(sequence);
            if (!conflict.isEmpty()) {
                QMessageBox::warning(
                    &dialog,
                    QStringLiteral("Shortcut already in use"),
                    QStringLiteral("%1 cannot use %2 because it belongs to %3.")
                        .arg(name, sequence.toString(QKeySequence::NativeText), conflict));
                return;
            }
        }

        QString error;
        if (!store.replacePair(
                auction, giveaway, savedAuction, savedGiveaway, &error)) {
            QMessageBox::critical(
                &dialog,
                QStringLiteral("Could not save controls"),
                error.isEmpty() ? QStringLiteral("KDE rejected the shortcut change.") : error);
            return;
        }
        savedAuction = {auction};
        savedGiveaway = {giveaway};
        status->setText(QStringLiteral("Saved. The new controls are active immediately."));
    });

    dialog.show();
    return app.exec();
}
} // namespace

int main(int argc, char **argv) {
    if (argc >= 2 && QString::fromLocal8Bit(argv[1]) == QStringLiteral("--validate-shortcuts")) {
        return runValidationCli(argc, argv);
    }
    if (argc == 2 && QString::fromLocal8Bit(argv[1]) == QStringLiteral("--self-test-transaction")) {
        return runTransactionSelfTest(argc, argv);
    }
    return runGui(argc, argv);
}
